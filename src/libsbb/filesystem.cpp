//
//

#include "filesystem.h"

#include <unistd.h>
#include <vector>
#include <geek/core-string.h>

using namespace std;
using namespace Geek;
using namespace Geek::Core;

#define MIN(_a, _b) (((_a) < (_b)) ? (_a) : (_b))

FileSystem::FileSystem(string dbPath) : Logger("FileSystem")
{
    m_dbPath = dbPath;
    m_databaseMutex = Thread::createMutex();
}

FileSystem::~FileSystem()
{
    delete m_database;
}

bool FileSystem::init()
{
    m_database = new Database(m_dbPath, false);

    vector<Table> tables;

    {
        Table entry;
        entry.name = "entry";
        entry.columns.insert(Column("inode", true, false));
        entry.columns.insert(Column("parent_inode"));
        entry.columns.insert(Column("versionId"));
        entry.columns.insert(Column("type"));
        entry.columns.insert(Column("name"));
        entry.columns.insert(Column("length"));
        entry.columns.insert(Column("extentVersionId"));
        tables.push_back(entry);
    }
    {
        Table extent;
        extent.name = "extent";
        extent.columns.insert(Column("inode"));
        extent.columns.insert(Column("logical_address"));
        extent.columns.insert(Column("physical_block"));
        extent.columns.insert(Column("length"));
        tables.push_back(extent);
    }

    m_database->checkSchema(tables);
    m_database->execute("CREATE INDEX IF NOT EXISTS entry_parent_idx ON entry (parent_inode)");
    m_database->execute("CREATE INDEX IF NOT EXISTS extent_inode_idx ON entry (inode)");

    return true;
}

void FileSystem::dumpTree()
{
    printf("Tree:\n");
    dumpTree(m_diskEntries, 2, 0);

    /*
    for (auto it : m_diskEntries)
    {
        printf("0x%llx: %s (parent=0x%llx)\n", it.first, it.second->name.c_str(), it.second->parentId);
    }
    */
}

static bool compareOffsets(EntryExtent e1, EntryExtent e2)
{
    return (e1.logicalAddress < e2.logicalAddress);
}

ssize_t FileSystem::read(int fd, Entry* entry, void* buffer, off_t offset, size_t size)
{
    uint8_t* fileBuffer = new uint8_t[entry->length];

    sort(entry->extents.begin(), entry->extents.end(), compareOffsets);
    uint64_t remaining = entry->length;
    for (auto extentIt : entry->extents)
    {
        int readLen = MIN(extentIt.length, remaining);
        //off_t off = 0x5000 + (extentIt.physicalBlock * 4096);// + pos - extentIt.logicalAddress;
        off_t off = 0xc805000 + (extentIt.physicalBlock * 4096);
        log(DEBUG, "read: 0x%llx: 0x%llx (0x%llx) length=%lld readLen=%d", extentIt.logicalAddress, extentIt.physicalBlock, off, extentIt.length, readLen);

        ssize_t r = ::pread(fd, fileBuffer + extentIt.logicalAddress, readLen, off);
        log(DEBUG, "read:  -> r=%d", r);
        if (r <= 0)
        {
            return 0;
        }

        remaining -= r;
    }

    if (offset < entry->length)
    {
        if (offset + size > entry->length)
        {
            size = entry->length - offset;
        }
        memcpy(buffer, fileBuffer + offset, size);
    }
    else
    {
        size = 0;
    }

    delete[] fileBuffer;

    return size;
}



void FileSystem::dumpTree(std::__1::map<uint64_t,Entry*>& diskEntries, uint64_t dirId, int level)
{
    if (dirId == 0)
    {
        return;
    }
    std::__1::string spaces;
    for (int i = 0; i < level; i++)
    {
        spaces += "  ";
    }

    Entry* dir = findByInode(dirId);
    if (dir == nullptr)
    {
        printf("%s0x%llx: Not found\n", spaces.c_str(), dirId);
        return;
    }
    printf("%s0x%llx: %s, type=%d length=%lld\n", spaces.c_str(), dirId, dir->name.c_str(), dir->type, dir->length);
    for (auto extent : dir->extents)
    {
        printf("%s-> extent: 0x%llx: block=0x%llx, valueLength=%lld\n", spaces.c_str(), extent.logicalAddress, extent.physicalBlock, extent.length);
    }

    vector<Entry*> children = findByParentInode(dirId);
    for (auto child : children)
    {
        if (child->inode == dirId)
        {
            continue;
        }
        dumpTree(diskEntries, child->inode, level + 1);
    }
}

void FileSystem::clear()
{
    m_database->execute("DELETE FROM entry");
    m_database->execute("DELETE FROM extent");
}

void FileSystem::addEntry(Entry* entry)
{
    printf("addEntry: 0x%llx -> %s\n", entry->inode, entry->name.c_str());
    m_diskEntries.insert(make_pair(entry->inode, entry));

    string sql = "INSERT INTO entry (inode, parent_inode, versionId, type, name, length, extentVersionId) values (?,?,?,?,?,?,?)";
    PreparedStatement* ps = m_database->prepareStatement(sql);
    ps->bindInt64(1, entry->inode);
    ps->bindInt64(2, entry->parentId);
    ps->bindInt64(3, entry->versionId);
    ps->bindInt64(4, entry->type);
    ps->bindString(5, entry->name);
    ps->bindInt64(6, entry->length);
    ps->bindInt64(7, entry->extentVersionId);
    ps->execute();
    delete ps;

    entry->isNew = false;
}

void FileSystem::updateEntry(Entry* entry)
{
    if (entry->isNew)
    {
        addEntry(entry);
    }
    else
    {
        string sql = "UPDATE entry SET parent_inode = ?, versionId = ?, type = ?, name = ?, length = ?, extentVersionId = ? WHERE inode = ?";
        PreparedStatement* ps = m_database->prepareStatement(sql);
        ps->bindInt64(1, entry->parentId);
        ps->bindInt64(2, entry->versionId);
        ps->bindInt64(3, entry->type);
        ps->bindString(4, entry->name);
        ps->bindInt64(5, entry->length);
        ps->bindInt64(6, entry->extentVersionId);
        ps->bindInt64(7, entry->inode);
        if (!ps->execute())
        {
            printf("XXX: Failed to update!\n");
        }
        delete ps;
    }

    if (entry->extentsUpdated)
    {
        PreparedStatement* deletePs = m_database->prepareStatement("DELETE FROM extent WHERE inode=?");
        deletePs->bindInt64(1, entry->inode);
        deletePs->execute();
        delete deletePs;
        for (auto extent: entry->extents)
        {
            string insertExtent = "INSERT INTO extent (inode, logical_address, physical_block, length) VALUES (?, ?, ?, ?)";
            PreparedStatement* ps = m_database->prepareStatement(insertExtent);
            ps->bindInt64(1, entry->inode);
            ps->bindInt64(2, extent.logicalAddress);
            ps->bindInt64(3, extent.physicalBlock);
            ps->bindInt64(4, extent.length);
            ps->execute();
        }
        entry->extentsUpdated = false;
    }
}

Entry* FileSystem::findByInode(uint64_t inode)
{
    auto it = m_diskEntries.find(inode);
    if (it != m_diskEntries.end())
    {
        return it->second;
    }

    string sql = "SELECT inode, parent_inode, versionId, type, name, length, extentVersionId FROM entry WHERE inode=?";
    PreparedStatement* ps = m_database->prepareStatement(sql);
    ps->bindInt64(1, inode);
    ps->executeQuery();
    if (ps->step())
    {
        Entry* entry = getEntryFromQuery(ps);
        delete ps;
        return entry;
    }

    return nullptr;
}

vector<Entry*> FileSystem::findByParentInode(uint64_t inode)
{
    vector<Entry*> entries;

    string sql = "SELECT inode, parent_inode, versionId, type, name, length, extentVersionId FROM entry WHERE parent_inode=?";
    PreparedStatement* ps = m_database->prepareStatement(sql);
    ps->bindInt64(1, inode);
    if (ps->executeQuery())
    {
        while (ps->step())
        {
            Entry* entry = getEntryFromQuery(ps);
            entries.push_back(entry);
        }
    }
    delete ps;

    return entries;
}

Entry* FileSystem::findByName(uint64_t inode, std::string name)
{
    Entry* entry = nullptr;

    string sql = "SELECT inode, parent_inode, versionId, type, name, length, extentVersionId FROM entry WHERE parent_inode=? AND name=?";
    PreparedStatement* ps = m_database->prepareStatement(sql);
    ps->bindInt64(1, inode);
    ps->bindString(2, name);
    if (ps->executeQuery())
    {
        if (ps->step())
        {
            entry = getEntryFromQuery(ps);
        }
    }
    delete ps;

    return entry;
}

Entry* FileSystem::getEntryFromQuery(PreparedStatement* ps)
{
    Entry* entry = new Entry();
    entry->inode = ps->getInt64(0);
    entry->parentId = ps->getInt64(1);
    entry->versionId = ps->getInt64(2);
    entry->type = ps->getInt64(3);
    entry->name = ps->getString(4);
    entry->length = ps->getInt64(5);
    entry->extentVersionId = ps->getInt64(6);
    entry->isNew = false;
    entry->extentsUpdated = false;

    string extentSql = "SELECT logical_address, physical_block, length FROM extent WHERE inode=?";
    PreparedStatement* extentPs = m_database->prepareStatement(extentSql);
    extentPs->bindInt64(1, entry->inode);
    extentPs->executeQuery();
    while (extentPs->step())
    {
        EntryExtent extent;
        extent.logicalAddress = extentPs->getInt64(0);
        extent.physicalBlock = extentPs->getInt64(1);
        extent.length = extentPs->getInt64(2);
        entry->extents.push_back(extent);
    }
    delete extentPs;

    m_diskEntries.insert(make_pair(entry->inode, entry));

    return entry;
}

Entry* FileSystem::findByPath(std::string path)
{
    m_databaseMutex->lock();
    vector<string> parts = Geek::Core::splitString(path, '/');

    Entry* partEntry = findByInode(2); // XXX TODO: Specific to APFS!
    //log(DEBUG, "findByPath: root entry: 0x%llx", partEntry->inode);
    for (string part : parts)
    {
        partEntry = findByName(partEntry->inode, part);
        if (partEntry == nullptr)
        {
            //log(DEBUG, "findByPath:  -> %s = Not found", part.c_str());
            break;
        }
        //log(DEBUG, "findByPath:  -> %s = 0x%llx", part.c_str(), partEntry->inode);
    }
    m_databaseMutex->unlock();
    return partEntry;
}
