//
//

#ifndef SUPERBADBLOCK_APFSSCANNER_H
#define SUPERBADBLOCK_APFSSCANNER_H

#include <map>

#include "scanner.h"
#include "apfs.h"
#include "filesystem.h"

struct KeyValuePair
{
    const j_key_t* key = nullptr;
    int valueLength = 0;
    const uint8_t* value = nullptr;
};

struct TreeEntryValues
{
    std::vector<KeyValuePair> keyValues;
};

class APFSScanner : public FileSystemScanner
{
 private:
    std::map<uint64_t, uint64_t> m_objId2inode;
    unsigned int m_currentBlockSize = 4096;

 public:
    APFSScanner(DiskScanner* fh);
    ~APFSScanner() override;

    bool init() override;

    void scanBlock(uint64_t pos, const uint8_t* data, int sectorSize) override;

    void dumpFile(uint64_t inode);

    void scanDirRec(const btree_node_phys_t* btreeNode, Entry* fileEntry, const j_key_t* jKey, const uint8_t* valPtr);

    void
    scanFileExtent(
        const btree_node_phys_t* btreeNode,
        Entry* fileEntry,
        const j_key_t* jKey,
        const uint8_t* valPtr);

    Entry* scanInode(
        const btree_node_phys_t* btreeNode,
        const KeyValuePair &pair,
        const j_key_t* jKey,
        uint64_t valueId,
        const uint8_t* valPtr);

    void scanBTreeBlock(const uint8_t* block, const obj_phys_t* objHeader, unsigned int type);
};

#endif //SUPERBADBLOCK_APFSSCANNER_H
