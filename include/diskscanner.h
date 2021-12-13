//
//

#ifndef SUPERBADBLOCK_DISKSCANNER_H
#define SUPERBADBLOCK_DISKSCANNER_H

#include "filesystem.h"
#include "scanner.h"

#include <geek/core-logger.h>

class DiskScanner : public Geek::Logger
{
 private:
    std::string m_disk;
    std::string m_dbPath;
    int m_fd;
    FileSystem* m_fileSystem = nullptr;
    std::vector<FileSystemScanner*> m_scanners;
    volatile bool m_running = false;

 public:
    DiskScanner(std::string path, std::string dbPath);
    ~DiskScanner();

    bool open();
    bool close();

    bool scan();
    void stop();

    int getFD()
    {
        return m_fd;
    }

    FileSystem* getFileSystem()
    {
        return m_fileSystem;
    }
};


#endif //SUPERBADBLOCK_DISKSCANNER_H
