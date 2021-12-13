//
//

#include <sys/fcntl.h>
#include <unistd.h>
#include "diskscanner.h"
#include "apfsscanner.h"

using namespace std;
using namespace Geek;

DiskScanner::DiskScanner(std::string path, string dbPath) : Logger("DiskScanner")
{
    m_disk = path;
    m_dbPath = dbPath;
}

DiskScanner::~DiskScanner()
{
    delete m_fileSystem;
}

bool DiskScanner::open()
{
    m_fileSystem = new FileSystem(m_dbPath);
    m_fileSystem->init();

    APFSScanner* scanner = new APFSScanner(this);
    m_scanners.push_back(scanner);

    m_fd = ::open(m_disk.c_str(), O_RDONLY);
    if (m_fd < 0)
    {
        log(ERROR, "open: Failed to open image: %d", errno);
        return false;
    }

    return true;
}

bool DiskScanner::scan()
{
    m_fileSystem->clear();

    int currentBlockSize = 4096;
    uint64_t pos = 0;
    m_running = true;
    while (m_running)
    {
        uint8_t block[4096];
        ssize_t res;
        res = ::read(m_fd, block, 4096);

        if (res == 0)
        {
            break;
        }
        else if (res < 0)
        {
            printf("superbadblock: Failed to read block: errno=%d", errno);
            break;
        }

        m_fileSystem->getDatabase()->startTransaction();
        for (auto scanner : m_scanners)
        {
            scanner->scanBlock(pos, block, res);
        }
        m_fileSystem->getDatabase()->endTransaction();

        pos += 4096;
    }

    return false;
}

void DiskScanner::stop()
{
    m_running = false;
}
