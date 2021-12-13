//
//

#ifndef SUPERBADBLOCK_FILESYSTEM_H
#define SUPERBADBLOCK_FILESYSTEM_H

#include <geek/core-database.h>
#include <geek/core-logger.h>
#include <geek/core-thread.h>

#include <cstdint>
#include <map>

struct EntryExtent
{
    uint64_t logicalAddress = 0;
    uint64_t physicalBlock = 0;
    uint64_t length = 0;
};

struct Entry
{
    uint64_t inode = 0;
    uint64_t parentId = 0;
    uint64_t versionId = 0;
    int type;
    std::string name;
    uint64_t length;
    std::vector<EntryExtent> extents;
    uint64_t extentVersionId = 0;

    bool isNew = true;
    bool extentsUpdated = false;
};

class FileSystem : public Geek::Logger
{
 private:
    std::string m_dbPath;
    Geek::Core::Database* m_database = nullptr;
    Geek::Mutex* m_databaseMutex = nullptr;

 private:

    std::map<uint64_t, Entry*> m_diskEntries;

    void dumpTree(std::__1::map<uint64_t,Entry*>& diskEntries, uint64_t dirInode, int level);

 public:
    FileSystem(std::string dbPath);
    ~FileSystem();

    bool init();

    void clear();
    void addEntry(Entry* entry);
    void updateEntry(Entry* entry);
    Entry* findByInode(uint64_t inode);
    std::vector<Entry*> findByParentInode(uint64_t inode);

    Entry* findByName(uint64_t parentInode, std::string name);

    Entry* findByPath(std::string path);

    ssize_t read(int fd, Entry* entry, void* buffer, off_t offset, size_t size);

    void dumpTree();

    Geek::Core::Database* getDatabase() const
    {
        return m_database;
    }

    Entry* getEntryFromQuery(Geek::Core::PreparedStatement* ps);
};


#endif //SUPERBADBLOCK_FILESYSTEM_H
