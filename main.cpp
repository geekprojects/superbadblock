#include <stdio.h>
#include <stdint.h>
#include <geek/core-data.h>

#include <vector>
#include <map>
#include <set>

#include "apfs.h"

using namespace std;
using namespace Geek;

#define ALIGN(V, SIZE) ((((V) + (SIZE) - 1) / (SIZE)) * (SIZE))

#define NX_MAGIC_NO 0x4253584e

struct EntryExtent
{
    uint64_t logicalAddress = 0;
    uint64_t physicalBlock = 0;
    uint64_t length = 0;
    uint64_t xid = 0;
};

struct Entry
{
    uint64_t inode = 0;
    uint64_t xid = 0;
    uint64_t parentId = 0;
    int type;
    std::string name;
    uint64_t length;
    set<uint64_t> children;
    vector<EntryExtent> extents;
    uint64_t extentXid = 0;
};

struct KeyValuePair
{
    j_key_t* key;
    int valueLength;
    char* value;
};

struct TreeEntryValues
{
    vector<KeyValuePair> keyValues;
};

static uint64_t fletcher64(const char* data, size_t byteCount)
{
    uint64_t sum1 = 0;
    uint64_t sum2 = 0;

    const uint64_t m = 0xFFFFFFFF;

    uint32_t* wordPtr = (uint32_t*) data;

    size_t wordCount = byteCount / sizeof(*wordPtr);
    while (wordCount-- > 0)
    {
        sum1 = (sum1 + *wordPtr++);
        sum1 = (sum1 & m) + (sum1 >> 32);
        sum2 = (sum2 + sum1);
        sum2 = (sum2 & m) + (sum2 >> 32);
    }

    uint64_t check1 = m - ((sum1 + sum2) % m);
    uint64_t check2 = m - ((sum1 + check1) % m);
    return (check2 << 32) | check1;
}

void hexdump(const char* pos, int len)
{
    int i;
    for (i = 0; i < len; i += 16)
    {
        int j;
        printf("%08llx: ", (uint64_t)(pos + i));
        for (j = 0; j < 16 && (i + j) < len; j++)
        {
            printf("%02x ", (uint8_t)pos[i + j]);
        }
        for (j = 0; j < 16 && (i + j) < len; j++)
        {
            char c = pos[i + j];
            if (!isprint(c))
            {
                c = '.';
            }
            printf("%c", c);
        }
        printf("\n");
    }
}

class FileSystem
{

};

class FileSystemScanner
{
 private:

 public:
    FileSystemScanner();
    virtual ~FileSystemScanner();

    virtual bool scanBlock(const uint8_t* data, int sectorSize) = 0;
};

class APFSScanner : public FileSystemScanner
{
 private:
 public:
    APFSScanner();
    ~APFSScanner() override;

    bool scanBlock(const uint8_t* data, int sectorSize) override;
};

void dumpTree(map<uint64_t,Entry*>& diskEntries, uint64_t dirId, int level)
{
    if (dirId == 0)
    {
        return;
    }
    string spaces;
    for (int i = 0; i < level; i++)
    {
        spaces += "  ";
    }
    auto it = diskEntries.find(dirId);
    if (it == diskEntries.end())
    {
        printf("%s0x%llx: Not found\n", spaces.c_str(), dirId);
        return;
    }
    Entry* dir = it->second;
    printf("%s0x%llx: %s, type=%d (xid=0x%llx) length=%lld\n", spaces.c_str(), dirId, dir->name.c_str(), dir->type, dir->xid, dir->length);
    for (auto extent : dir->extents)
    {
        printf("%s-> extent: 0x%llx: block=0x%llx, valueLength=%d (xid=0x%llx)\n", spaces.c_str(), extent.logicalAddress, extent.physicalBlock, extent.length, extent.xid);
    }
    for (auto childId : dir->children)
    {
        if (childId == dirId)
        {
            continue;
        }
        dumpTree(diskEntries, childId, level + 1);
    }
}

class Disk
{
 private:
 public:
};

int main()
{
    Data* data = new Data();
    data->load("../disk1.dmg");

    map<uint64_t, Entry*> diskEntries;
    map<uint64_t, uint64_t> objId2inode;

    int currentBlockSize = 4096;
    while (!data->eof())
    {
        uint32_t pos = data->pos();
        char* block = data->readStruct(4096);

        obj_phys_t* objHeader = (obj_phys_t*) block;
        uint64_t cksum = fletcher64((block + MAX_CKSUM_SIZE), currentBlockSize - MAX_CKSUM_SIZE);

        if (objHeader->o_type != 0 && cksum == objHeader->o_cksum)
        {
            int type = objHeader->o_type & 0xffff;
            printf("APFS Block: 0x%x: oid=0x%llx, xid=0x%llx, type=0x%x, sub type=0x%x\n", pos, objHeader->o_oid, objHeader->o_xid, type, objHeader->o_subtype);

            switch (type)
            {
                case OBJECT_TYPE_NX_SUPERBLOCK:
                {
                    nx_superblock_t* nxSuperBlock = (nx_superblock_t*) block;
                    if (nxSuperBlock->nx_magic == NX_MAGIC_NO)
                    {
                        printf(
                            "  -> NX Super Block: magic=0x%x, block_size=%d\n",
                            nxSuperBlock->nx_magic,
                            nxSuperBlock->nx_block_size);
                        currentBlockSize = nxSuperBlock->nx_block_size;
                    }

                }
                    break;
                case OBJECT_TYPE_BTREE_NODE:
                case OBJECT_TYPE_BTREE:
                {
                    btree_node_phys_t* btreeNode = (btree_node_phys_t*) block;
                    btree_info_t* btreeInfo = reinterpret_cast<btree_info_t*>(block + currentBlockSize - sizeof(btree_info_t));

                    const char* name;
                    if (type == OBJECT_TYPE_BTREE)
                    {
                        name = "OBJECT_TYPE_BTREE";
                    }
                    else
                    {
                        name = "OBJECT_TYPE_BTREE_NODE";
                    }
                    printf(
                        "  -> %s subtype=0x%x, level=%d, flags=0x%x, xid=0x%llx, keys=%d\n",
                        name,
                        objHeader->o_subtype,
                        btreeNode->btn_level,
                        btreeNode->btn_flags,
                        btreeNode->btn_o.o_xid,
                        btreeNode->btn_nkeys);

                    // Extract the table of contents
                    vector<kvloc_t> tableOfContents;
                    char* keyBasePtr = block + sizeof(btree_node_phys_t) + btreeNode->btn_table_space.off + btreeNode->btn_table_space.len;
                    char* valBasePtr = block + currentBlockSize - sizeof(btree_info_t);
                    if (btreeNode->btn_flags & BTNODE_ROOT)
                    {
                        valBasePtr = block + currentBlockSize - sizeof(btree_info_t);
                    }
                    else
                    {
                        valBasePtr = block + currentBlockSize;
                    }
                    if (btreeNode->btn_flags & BTNODE_FIXED_KV_SIZE)
                    {
                        kvoff_t* toc = (kvoff_t*) (block + sizeof(btree_node_phys_t) + btreeNode->btn_table_space.off);
                        int i;
                        for (i = 0; i < btreeNode->btn_nkeys; i++)
                        {
                            kvloc_t loc;
                            loc.k.off = toc[i].k;
                            loc.k.len = btreeInfo->bt_fixed.bt_key_size;
                            loc.v.off = toc[i].v;
                            loc.v.len = btreeInfo->bt_fixed.bt_val_size;
                            tableOfContents.push_back(loc);
                        }
                    }
                    else
                    {
                        kvloc_t* toc = (kvloc_t*) (block + sizeof(btree_node_phys_t) + btreeNode->btn_table_space.off);
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
                        char* keyPtr = keyBasePtr + toc.k.off;
                        j_key_t* jKey = (j_key_t*)keyPtr;
                        char* jValue = valBasePtr - toc.v.off;
                        valBasePtr - toc.v.off;

                        uint64_t id = jKey->obj_id_and_type & OBJ_ID_MASK;
                        int type = jKey->obj_id_and_type >> OBJ_TYPE_SHIFT;

                        if (type == APFS_TYPE_DIR_REC)
                        {
                            j_drec_val_t* dirRecVal = (j_drec_val_t*)jValue;
                            objId2inode.insert(make_pair(id, dirRecVal->file_id));
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

                    for (auto entry : entries)
                    {
                        Entry* fileEntry = nullptr;
                        //printf("Entry 0x%llx:\n", entry.first);
                        for (auto pair : entry.second.keyValues)
                        {
                            j_key_t* jKey = pair.key;
                            int type = jKey->obj_id_and_type >> OBJ_TYPE_SHIFT;
                            uint64_t id = jKey->obj_id_and_type & OBJ_ID_MASK;
                            if (type == APFS_TYPE_ANY)
                            {
                                continue;
                            }
                            printf(
                                "    -> Entry 0x%llx (0x%llx): type=%d\n",
                                id,
                                entry.first,
                                type);
                            char* valPtr = pair.value;

                            switch (type)
                            {
                                case APFS_TYPE_EXTENT:
                                {
                                    j_phys_ext_key_t* extentKey = (j_phys_ext_key_t*) jKey;
                                    j_phys_ext_val_t* extentVal = (j_phys_ext_val_t*) valPtr;
                                    printf("        EXTENT: owning_obj_id=0x%llx\n", extentVal->owning_obj_id);
                                } break;

                                case APFS_TYPE_INODE:
                                {
                                    j_inode_key_t* inodeKey = (j_inode_key_t*) jKey;
                                    j_inode_val_t* inodeVal = (j_inode_val_t*) valPtr;
                                    printf(
                                        "        INODE: private_id=0x%llx, parent_id=0x%llx, mode=0%o, internal_flags=0x%llx\n",
                                        inodeVal->private_id,
                                        inodeVal->parent_id,
                                        inodeVal->mode,
                                        inodeVal->internal_flags);
                                    auto it = diskEntries.find(id);//inodeVal->private_id);
                                    if (it == diskEntries.end())
                                    {
                                        fileEntry = new Entry();
                                        auto it = objId2inode.find(inodeVal->private_id);
                                        uint64_t inode = 0;
                                        if (it != objId2inode.end())
                                        {
                                            inode = it->second;
                                        }
                                        else
                                        {
                                            printf("Warning: Unable to find inode for file with object id: 0x%llx\n", inodeVal->private_id);
                                        }
                                        fileEntry->inode = inode;

                                        diskEntries.insert(make_pair(fileEntry->inode, fileEntry));
                                    }
                                    else
                                    {
                                        fileEntry = it->second;
                                    }
                                    uint64_t length = 0;
                                    bool hasSize = false;
                                    int xfieldLength = pair.valueLength - sizeof(j_inode_val_t);
                                    if (xfieldLength > (int)sizeof(xf_blob_t))
                                    {
                                        printf("        INODE: FOund xfields, xfieldLength=%d, pair.valueLength=%d\n", xfieldLength, pair.valueLength);
                                        xf_blob_t* blob = (xf_blob_t*)(&(inodeVal->xfields[0]));
                                        printf("        INODE: xfields blob: num=%d\n", blob->xf_num_exts);
                                        x_field_t* xfields = (x_field_t*)&(blob->xf_data);
                                        //char* xdataptr = xptr + ALIGN((sizeof(x_field_t*) * blob->xf_num_exts), 8);
                                        const char *xdataptr = (const char*)(inodeVal->xfields + sizeof(xf_blob_t) + blob->xf_num_exts * sizeof(x_field_t));
                                        for (int i = 0; i < blob->xf_num_exts; i++)
                                        {
                                            x_field_t* field = &(xfields[i]);
                                            printf("        INODE: xfield %d: type=0x%x, size=%d\n", i,field->x_type, field->x_size);

                                            if (field->x_type == INO_EXT_TYPE_DSTREAM)
                                            {
                                                j_dstream_t* dstream = (j_dstream_t*)(xdataptr);
                                                length = dstream->size;
                                                hasSize = true;
                                                printf("        INODE: xfield DSTREAM: size=%d, allocated_size=%d\n", dstream->size, dstream->alloced_size);
                                            }

                                            xdataptr += ((field->x_size + 7) & ~7);
                                        }
                                    }

                                    if (fileEntry->xid <= btreeNode->btn_o.o_xid);
                                    {
                                        auto it = objId2inode.find(inodeVal->parent_id);
                                        uint64_t inode = 0;
                                        if (fileEntry->parentId == 0)
                                        {
                                            if (it != objId2inode.end())
                                            {
                                                inode = it->second;
                                            }
                                            else
                                            {
                                                printf("Warning: Unable to find parent inode for file with id: 0x%llx\n", inodeVal->parent_id);
                                            }
                                        }
                                        if (fileEntry->xid < btreeNode->btn_o.o_xid)
                                        {
                                            //fileEntry->extents.clear();
                                        }
                                        fileEntry->parentId = inode;
                                        //fileEntry->type = inodeVal->mode;
                                        fileEntry->xid = btreeNode->btn_o.o_xid;
                                        if (hasSize)
                                        {
                                            fileEntry->length = length;
                                        }
                                    }


                                }break;

                                case APFS_TYPE_XATTR:
                                {
                                    j_xattr_key_t* xattrKey = (j_xattr_key_t*) jKey;
                                    j_xattr_val_t* xattrVal = (j_xattr_val_t*) valPtr;
                                    printf("        XATTR: name=%s\n", xattrKey->name);
                                    if (xattrVal->flags & XATTR_DATA_EMBEDDED)
                                    {
                                        //hexdump((char*) xattrVal->xdata, xattrVal->xdata_len);
                                    }
                                } break;

                                case APFS_TYPE_DSTREAM_ID:
                                {
                                    j_dstream_id_key_t* dstreamIdKey = (j_dstream_id_key_t*) jKey;
                                    j_dstream_id_val_t* dstreamIdVal = (j_dstream_id_val_t*) valPtr;
                                    printf("        DSTREAM ID: refcnt=%d\n", dstreamIdVal->refcnt);
                                } break;

                                case APFS_TYPE_FILE_EXTENT:
                                {
                                    j_file_extent_key_t* extentKey = (j_file_extent_key_t*) jKey;
                                    j_file_extent_val_t* extentVal = (j_file_extent_val_t*) valPtr;
                                    uint64_t len = extentVal->len_and_flags & J_FILE_EXTENT_LEN_MASK;
                                    printf(
                                        "        FILE EXTENT: logical addr=0x%llx, len=%lld, physical block=0x%llx\n",
                                        extentKey->logical_addr,
                                        len,
                                        extentVal->phys_block_num);
                                    if (fileEntry != nullptr && fileEntry->extentXid <= btreeNode->btn_o.o_xid)
                                    {
                                        if (fileEntry->extentXid < btreeNode->btn_o.o_xid)
                                        {
                                            // New extents!
                                            fileEntry->extents.clear();
                                            fileEntry->extentXid = btreeNode->btn_o.o_xid;
                                        }
                                        EntryExtent extent;
                                        extent.logicalAddress = extentKey->logical_addr;
                                        extent.physicalBlock = extentVal->phys_block_num;
                                        extent.length = extentVal->len_and_flags & J_FILE_EXTENT_LEN_MASK;
                                        extent.xid = btreeNode->btn_o.o_xid;
                                        fileEntry->extents.push_back(extent);
                                    }
                                } break;

                                case APFS_TYPE_DIR_REC:
                                {
                                    j_drec_hashed_key_t* dirRecKey = (j_drec_hashed_key_t*) jKey;
                                    printf("        DIR_REC: name=%s\n", &(dirRecKey->name[0]));

                                    j_drec_val_t* dirRecVal = (j_drec_val_t*) valPtr;
                                    printf(
                                        "          -> file_id=0x%llx, type=0x%x\n",
                                        dirRecVal->file_id,
                                        dirRecVal->flags & DREC_TYPE_MASK);

                                    Entry* childEntry;
                                    auto it = diskEntries.find(dirRecVal->file_id);

                                    if (it == diskEntries.end())
                                    {
                                        childEntry = new Entry();
                                        childEntry->inode = dirRecVal->file_id;
                                        diskEntries.insert(make_pair(childEntry->inode, childEntry));
                                    }
                                    else
                                    {
                                        childEntry = it->second;
                                    }
                                    if (childEntry->xid <= btreeNode->btn_o.o_xid)
                                    {
                                        if (childEntry->xid < btreeNode->btn_o.o_xid)
                                        {
                                            //childEntry->extents.clear();
                                        }
                                        if (fileEntry != nullptr)
                                        {
                                            childEntry->parentId = fileEntry->inode;
                                            fileEntry->children.insert(childEntry->inode);
                                        }
                                        childEntry->name = string((char*)dirRecKey->name);
                                        childEntry->xid = btreeNode->btn_o.o_xid;
                                        childEntry->type = dirRecVal->flags & DREC_TYPE_MASK;
                                        childEntry->xid = btreeNode->btn_o.o_xid;
                                    }
                                } break;
                                default:
                                    printf("        APFS_TYPE: 0x%x", type);
                                    break;
                            }
                        }
                    }

                }
                    break;
                case OBJECT_TYPE_FS:
                    printf("XXX:  -> Super Block\n");
                    break;
                case OBJECT_TYPE_OMAP:
                    printf("XXX:  -> OMAP!\n");
                    break;
            }
        }
    }

    for (auto it : diskEntries)
    {
        Entry* entry = it.second;
        printf("0x%llx: %s, parent=0x%llx\n", entry->inode, entry->name.c_str(), entry->parentId);
    }

    printf("Tree:\n");
    dumpTree(diskEntries, 2, 0);

    printf("\ninode map:\n");
    for (auto it : objId2inode)
    {
        printf("  0x%llx = 0x%llx\n", it.first, it.second);
    }

    delete data;
    return 0;
}
