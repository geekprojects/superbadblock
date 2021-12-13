//
//

#ifndef SUPERBADBLOCK_SCANNER_H
#define SUPERBADBLOCK_SCANNER_H

#include <cstdint>
#include <string>
#include <set>
#include <vector>
#include <map>

#include <geek/core-logger.h>
#include "apfs.h"

class DiskScanner;


class FileSystemScanner : public Geek::Logger
{
 protected:
    DiskScanner* m_fileHunter;
    uint64_t m_fsOffset = 0;

 public:
    explicit FileSystemScanner(const char* name, DiskScanner* fh) : Logger(name)
    {
        m_fileHunter = fh;
    }
    virtual ~FileSystemScanner() = default;

    virtual bool init() = 0;
    virtual void scanBlock(uint64_t pos, const uint8_t* data, int sectorSize) = 0;
};

#endif //SUPERBADBLOCK_SCANNER_H
