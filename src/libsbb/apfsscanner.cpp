//
//

#include <cstdio>

#include "apfsscanner.h"
#include "apfs.h"
#include "utils.h"
#include "diskscanner.h"
#include "filesystem.h"

#include <vector>
#include <unistd.h>

using namespace std;
using namespace Geek;
using namespace Geek::Core;

#define MIN(_a, _b) (((_a) < (_b)) ? (_a) : (_b))

#define NX_MAGIC_NO 0x4253584e

//#define APFS_DEBUG

APFSScanner::APFSScanner(DiskScanner* fh) : FileSystemScanner("APFSScanner", fh)
{
}

APFSScanner::~APFSScanner() = default;

bool APFSScanner::init()
{
    vector<Table> tables;

    {
        Table objId2inodeTable;
        objId2inodeTable.name = "objid2inode";
        objId2inodeTable.columns.insert(Column("obj_id", true, false));
        objId2inodeTable.columns.insert(Column("inode"));
        tables.push_back(objId2inodeTable);
    }
    return m_fileHunter->getFileSystem()->getDatabase()->checkSchema(tables);
}

void APFSScanner::scanBlock(uint64_t pos, const uint8_t* block, int sectorSize)
{
    auto* objHeader = (obj_phys_t*) block;
    uint64_t cksum = fletcher64((block + MAX_CKSUM_SIZE), m_currentBlockSize - MAX_CKSUM_SIZE);

    if (objHeader->o_type != 0 && cksum == objHeader->o_cksum)
    {
        unsigned int type = objHeader->o_type & 0xffff;
#ifdef APFS_DEBUG
        log(DEBUG, "APFS Block: 0x%llx: oid=0x%llx, versionId=0x%llx, type=0x%x, sub type=0x%x", pos, objHeader->o_oid, objHeader->o_xid, type, objHeader->o_subtype);
#endif

        switch (type)
        {
            case OBJECT_TYPE_NX_SUPERBLOCK:
            {
                auto* nxSuperBlock = (const nx_superblock_t*) block;
                if (nxSuperBlock->nx_magic == NX_MAGIC_NO)
                {
#ifdef APFS_DEBUG
                    log(DEBUG,
                        "  -> NX Super Block: magic=0x%x, block_size=%d",
                        nxSuperBlock->nx_magic,
                        nxSuperBlock->nx_block_size);
#endif
                    m_currentBlockSize = nxSuperBlock->nx_block_size;
                    if (m_fsOffset == 0)
                    {
                        m_fsOffset = pos;
                        log(DEBUG, "m_fsOffset=0x%llx", m_fsOffset);
                    }
                }
            } break;

            case OBJECT_TYPE_BTREE_NODE:
            case OBJECT_TYPE_BTREE:
                scanBTreeBlock(block, objHeader, type);
                break;

#ifdef APFS_DEBUG
            case OBJECT_TYPE_FS:
                log(DEBUG, "  -> Super Block");
                break;
            case OBJECT_TYPE_OMAP:
                log(DEBUG, "  -> OMAP!");
                break;

            default:
                log(DEBUG, "  -> Unhandled block, type=0x%x!", type);
                break;
#else
            default:
                // Ignore
                break;
#endif
        }
    }
}

void APFSScanner::scanBTreeBlock(const uint8_t* block, const obj_phys_t* objHeader, unsigned int type)
{
    auto* btreeNode = (const btree_node_phys_t*) block;
    auto* btreeInfo = reinterpret_cast<const btree_info_t*>(block + m_currentBlockSize - sizeof(btree_info_t));

    const char* name;
    if (type == OBJECT_TYPE_BTREE)
    {
        name = "OBJECT_TYPE_BTREE";
    }
    else
    {
        name = "OBJECT_TYPE_BTREE_NODE";
    }
#ifdef APFS_DEBUG
    log(DEBUG,
        "  -> %s subtype=0x%x, level=%d, flags=0x%x, versionId=0x%llx, keys=%d",
        name,
        objHeader->o_subtype,
        btreeNode->btn_level,
        btreeNode->btn_flags,
        btreeNode->btn_o.o_xid,
        btreeNode->btn_nkeys);
#endif

    // Extract the table of contents
    vector<kvloc_t> tableOfContents;
    const uint8_t* keyBasePtr = block + sizeof(btree_node_phys_t) + btreeNode->btn_table_space.off + btreeNode->btn_table_space.len;
    const uint8_t* valBasePtr = block + m_currentBlockSize - sizeof(btree_info_t);
    if (btreeNode->btn_flags & BTNODE_ROOT)
    {
        valBasePtr = block + m_currentBlockSize - sizeof(btree_info_t);
    }
    else
    {
        valBasePtr = block + m_currentBlockSize;
    }
    if (btreeNode->btn_flags & BTNODE_FIXED_KV_SIZE)
    {
        auto* toc = (kvoff_t*)(block + sizeof(btree_node_phys_t) + btreeNode->btn_table_space.off);
        int i;
        for (i = 0; i < btreeNode->btn_nkeys; i++)
        {
            kvloc_t loc{};
            loc.k.off = toc[i].k;
            loc.k.len = btreeInfo->bt_fixed.bt_key_size;
            loc.v.off = toc[i].v;
            loc.v.len = btreeInfo->bt_fixed.bt_val_size;
            tableOfContents.push_back(loc);
        }
    }
    else
    {
        auto* toc = (kvloc_t*) (block + sizeof(btree_node_phys_t) + btreeNode->btn_table_space.off);
        int i;
        for (i = 0; i < btreeNode->btn_nkeys; i++)
        {
            tableOfContents.push_back(toc[i]);
        }
    }

    // Group values with their entities
    map<uint64_t, TreeEntryValues> entries;
    for (auto toc : tableOfContents)
    {
        const uint8_t* keyPtr = keyBasePtr + toc.k.off;
        auto* jKey = (j_key_t*)keyPtr;
        const uint8_t* jValue = valBasePtr - toc.v.off;

        uint64_t id = jKey->obj_id_and_type & OBJ_ID_MASK;
        uint64_t entryType = jKey->obj_id_and_type >> OBJ_TYPE_SHIFT;

        if (entryType == APFS_TYPE_DIR_REC)
        {
            auto* dirRecVal = (j_drec_val_t*)jValue;
            m_objId2inode.insert(make_pair(id, dirRecVal->file_id));
        }

        auto it = entries.find(id);
        KeyValuePair pair;
        pair.key = jKey;
        pair.valueLength = toc.v.len;
        pair.value = jValue;
        if (it == entries.end())
        {
            TreeEntryValues treeEntryValues;
            treeEntryValues.keyValues.push_back(pair);
            entries.insert(make_pair(id, treeEntryValues));
        }
        else
        {
            it->second.keyValues.push_back(pair);
        }
    }

    for (const auto& entry : entries)
    {
        Entry* fileEntry = nullptr;
        for (auto pair : entry.second.keyValues)
        {
            const j_key_t* jKey = pair.key;
            uint64_t valueType = jKey->obj_id_and_type >> OBJ_TYPE_SHIFT;
            uint64_t valueId = jKey->obj_id_and_type & OBJ_ID_MASK;
            if (valueType == APFS_TYPE_ANY)
            {
                continue;
            }
#ifdef APFS_DEBUG
            log(DEBUG,
                "    -> Entry 0x%llx (0x%llx): valueType=%lld",
                valueId,
                entry.first,
                valueType);
#endif
            const uint8_t* valPtr = pair.value;

            switch (valueType)
            {
                case APFS_TYPE_INODE:
                    fileEntry = scanInode(btreeNode, pair, jKey, valueId, valPtr);
                    break;

                case APFS_TYPE_FILE_EXTENT:
                    scanFileExtent(btreeNode, fileEntry, jKey, valPtr);
                    break;

                case APFS_TYPE_DIR_REC:
                    scanDirRec(btreeNode, fileEntry, jKey, valPtr);
                    break;

#ifdef APFS_DEBUG
                case APFS_TYPE_EXTENT:
                {
                    auto extentKey = (j_phys_ext_key_t*) jKey;
                    auto extentVal = (j_phys_ext_val_t*) valPtr;
                    log(DEBUG, "        EXTENT: owning_obj_id=0x%llx", extentVal->owning_obj_id);
                } break;

                case APFS_TYPE_XATTR:
                {
                    auto xattrKey = (j_xattr_key_t*) jKey;
                    auto xattrVal = (j_xattr_val_t*) valPtr;
                    log(DEBUG, "        XATTR: name=%s", xattrKey->name);
                    if (xattrVal->flags & XATTR_DATA_EMBEDDED)
                    {
                        //hexdump((char*) xattrVal->xdata, xattrVal->xdata_len);
                    }
                } break;

                case APFS_TYPE_DSTREAM_ID:
                {
                    auto dstreamIdKey = (j_dstream_id_key_t*) jKey;
                    auto dstreamIdVal = (j_dstream_id_val_t*) valPtr;
                    log(DEBUG, "        DSTREAM ID: refcnt=%d", dstreamIdVal->refcnt);
                } break;

                default:
                    log(DEBUG, "        APFS_TYPE: 0x%llx", valueType);
                    break;
#else
                default:
                    // Ignore
                    break;
#endif
            }
        }
        if (fileEntry != nullptr)
        {
            m_fileHunter->getFileSystem()->updateEntry(fileEntry);
        }
    }
}

Entry* APFSScanner::scanInode(
    const btree_node_phys_t* btreeNode,
    const KeyValuePair &pair,
    const j_key_t* jKey,
    uint64_t valueId,
    const uint8_t* valPtr)
{
    auto inodeKey = (j_inode_key_t*) jKey;
    auto inodeVal = (j_inode_val_t*) valPtr;
#ifdef APFS_DEBUG
    log(DEBUG,
        "        INODE: private_id=0x%llx, parent_id=0x%llx, mode=0%o, internal_flags=0x%llx",
        inodeVal->private_id,
        inodeVal->parent_id,
        inodeVal->mode,
        inodeVal->internal_flags);
#endif
    //auto it = m_diskEntries.find(valueId);//inodeVal->private_id);
    Entry* fileEntry = m_fileHunter->getFileSystem()->findByInode(valueId);
    if (fileEntry == nullptr)
    {
        fileEntry = new Entry();
        fileEntry->inode = valueId;//inodeIt->second;
#if 0
        auto inodeIt = m_objId2inode.find(inodeVal->private_id);
        if (inodeIt != m_objId2inode.end())
        {
        }
        else
        {
            log(WARN, "Unable to find inode for file with object valueId: 0x%llx", inodeVal->private_id);
        }
#endif
    }

    uint64_t length = 0;
    bool hasSize = false;
    int64_t xfieldLength = (int64_t)pair.valueLength - (int64_t)sizeof(j_inode_val_t);
    if (xfieldLength > (int)sizeof(xf_blob_t))
    {
        auto blob = (xf_blob_t*)(&(inodeVal->xfields[0]));
#ifdef APFS_DEBUG
        log(DEBUG, "        INODE: Found xfields, xfieldLength=%d, pair.valueLength=%d", xfieldLength, pair.valueLength);
        log(DEBUG, "        INODE: xfields blob: num=%d", blob->xf_num_exts);
#endif
        auto xfields = (x_field_t*)&(blob->xf_data);
        auto xdataptr = (const char*)(inodeVal->xfields + sizeof(xf_blob_t) + blob->xf_num_exts * sizeof(x_field_t));

        for (int i = 0; i < blob->xf_num_exts; i++)
        {
            x_field_t* field = &(xfields[i]);
#ifdef APFS_DEBUG
            log(DEBUG, "        INODE: xfield %d: valueType=0x%x, size=%d", i, field->x_type, field->x_size);
#endif

            if (field->x_type == INO_EXT_TYPE_DSTREAM)
            {
                auto dstream = (j_dstream_t*)(xdataptr);
                length = dstream->size;
                hasSize = true;
#ifdef APFS_DEBUG
                log(DEBUG, "        INODE: xfield DSTREAM: size=%lld, allocated_size=%lld", dstream->size, dstream->alloced_size);
#endif
            }

            xdataptr += ((field->x_size + 7) & ~7);
        }
    }

    if (fileEntry->versionId <= btreeNode->btn_o.o_xid)
    {
        /*
        auto inodeIt = m_objId2inode.find(inodeVal->parent_id);
        uint64_t inode = 0;
        if (fileEntry->parentId == 0)
        {
            if (inodeIt != m_objId2inode.end())
            {
                inode = inodeIt->second;
            }
            else
            {
                log(WARN, "Unable to find parent inode for file with valueId: 0x%llx", inodeVal->parent_id);
            }
        }
        if (inode != 0)
        {
            fileEntry->parentId = inode;
        }
         */
        fileEntry->parentId = inodeVal->parent_id;
        //fileEntry->valueType = inodeVal->mode;
        fileEntry->versionId = btreeNode->btn_o.o_xid;
        if (hasSize)
        {
            fileEntry->length = length;
        }
    }
    m_fileHunter->getFileSystem()->updateEntry(fileEntry);
    return fileEntry;
}

void APFSScanner::scanFileExtent(
    const btree_node_phys_t* btreeNode,
    Entry* fileEntry,
    const j_key_t* jKey,
    const uint8_t* valPtr)
{
    auto extentKey = (j_file_extent_key_t*) jKey;
    auto extentVal = (j_file_extent_val_t*) valPtr;
    uint64_t len = extentVal->len_and_flags & J_FILE_EXTENT_LEN_MASK;
#ifdef APFS_DEBUG
    log(DEBUG,
        "        FILE EXTENT: logical addr=0x%llx, len=%lld, physical block=0x%llx",
        extentKey->logical_addr,
        len,
        extentVal->phys_block_num);
#endif
    if (fileEntry != nullptr && fileEntry->extentVersionId <= btreeNode->btn_o.o_xid)
    {
        if (fileEntry->extentVersionId < btreeNode->btn_o.o_xid)
        {
            // New extents!
            fileEntry->extents.clear();
            fileEntry->extentsUpdated = true;
            fileEntry->extentVersionId = btreeNode->btn_o.o_xid;
        }
        EntryExtent extent;
        extent.logicalAddress = extentKey->logical_addr;
        extent.physicalBlock = extentVal->phys_block_num;
        extent.length = extentVal->len_and_flags & J_FILE_EXTENT_LEN_MASK;
        fileEntry->extents.push_back(extent);
        fileEntry->extentsUpdated = true;
        m_fileHunter->getFileSystem()->updateEntry(fileEntry);
    }
}

void APFSScanner::scanDirRec(
    const btree_node_phys_t* btreeNode,
    Entry* fileEntry,
    const j_key_t* jKey,
    const uint8_t* valPtr)
{
    auto dirRecKey = (j_drec_hashed_key_t*) jKey;
    auto dirRecVal = (j_drec_val_t*) valPtr;

#ifdef APFS_DEBUG
    log(DEBUG, "        DIR_REC: name=%s", &(dirRecKey->name[0]));
    log(DEBUG,
        "          -> file_id=0x%llx, valueType=0x%x",
        dirRecVal->file_id,
        dirRecVal->flags & DREC_TYPE_MASK);
#endif

    Entry* childEntry = m_fileHunter->getFileSystem()->findByInode(dirRecVal->file_id);
    if (childEntry == nullptr)
    {
        childEntry = new Entry();
        childEntry->inode = dirRecVal->file_id;
    }

    if (childEntry->versionId <= btreeNode->btn_o.o_xid)
    {
        if (fileEntry != nullptr)
        {
            childEntry->parentId = fileEntry->inode;
            //fileEntry->children.insert(childEntry->inode);
        }
        childEntry->name = string((char*)dirRecKey->name);
        childEntry->versionId = btreeNode->btn_o.o_xid;
        childEntry->type = dirRecVal->flags & DREC_TYPE_MASK;
    }
    m_fileHunter->getFileSystem()->updateEntry(childEntry);
}

static bool compareOffsets(EntryExtent e1, EntryExtent e2)
{
    return (e1.logicalAddress < e2.logicalAddress);
}

void APFSScanner::dumpFile(uint64_t inode)
{
    Entry* entry = m_fileHunter->getFileSystem()->findByInode(inode);
    if (entry == nullptr)
    {
        return;
    }

    sort(entry->extents.begin(), entry->extents.end(), compareOffsets);
    uint64_t remaining = entry->length;
    for (auto extentIt : entry->extents)
    {
        int readLen = MIN(extentIt.length, remaining);
        off_t offset = m_fsOffset + (extentIt.physicalBlock * m_currentBlockSize);
        log(DEBUG, "dumpFile: 0x%llx: 0x%llx (0x%llx) length=%lld readLen=%d", extentIt.logicalAddress, extentIt.physicalBlock, offset, extentIt.length, readLen);

        int fd = m_fileHunter->getFD();
        lseek(fd, offset, SEEK_SET);
        auto fileBuffer = new uint8_t[readLen];

        ssize_t r = read(fd, fileBuffer, readLen);
        log(DEBUG, "dumpFile:  -> r=%d", r);
        if (r <= 0)
        {
            break;
        }

        hexdump(reinterpret_cast<const char*>(fileBuffer), r);
        delete[] fileBuffer;

        remaining -= r;
    }
}

