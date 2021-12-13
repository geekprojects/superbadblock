#include <vector>
#include <signal.h>

#include "diskscanner.h"

using namespace std;

static DiskScanner* g_fileHunter = NULL;

void signalHandler(int sig)
{
    if (g_fileHunter != NULL)
    {
        g_fileHunter->stop();
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        return -1;
    }
    const char* image = argv[1];
    const char* dbPath = argv[2];

    struct sigaction act;
    memset (&act, 0, sizeof(act));
    act.sa_handler = signalHandler;
    act.sa_flags = 0;
    sigaction(SIGINT, &act, 0);

    g_fileHunter = new DiskScanner(image, dbPath);

    g_fileHunter->open();
    g_fileHunter->scan();

    g_fileHunter->getFileSystem()->dumpTree();

    delete g_fileHunter;

    //apfsScanner->dumpTree();
    //apfsScanner->dumpFile(fd, 0x30);

    //close(fd);
    return 0;
}
