//
//

#include "diskscanner.h"
#include "utils.h"

#define FUSE_USE_VERSION 31
#include <fuse.h>

using namespace std;

DiskScanner* g_fileHunter = nullptr;

static void *sbb_init(struct fuse_conn_info *conn)
{
    return NULL;
}

static int sbb_getattr(const char *path, struct stat *stbuf)
{
    printf("XXX: sbb_getattr: path=%s\n", path);
    Entry* entry = g_fileHunter->getFileSystem()->findByPath(path);
    printf("XXX: sbb_getattr: path=%s, entry=%p\n", path, entry);
    if (entry == nullptr)
    {
        return -ENOENT;
    }
    if (entry->type == DT_REG)
    {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = entry->length;
    }
    else if (entry->type == DT_DIR)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    return 0;
}

static int sbb_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    Entry* entry = g_fileHunter->getFileSystem()->findByPath(path);
    printf("XXX: sbb_readdir: path=%s, entry=%p\n", path, entry);
    if (entry == nullptr)
    {
        return -ENOENT;
    }
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    vector<Entry*> children = g_fileHunter->getFileSystem()->findByParentInode(entry->inode);
    for (Entry* child: children)
    {
        filler(buf, child->name.c_str(), NULL, 0);
    }
    return 0;
}

static int sbb_open(const char *path, struct fuse_file_info *fi)
{
    Entry* entry = g_fileHunter->getFileSystem()->findByPath(path);
    printf("XXX: sbb_open: path=%s, entry=%p\n", path, entry);
    if (entry == nullptr)
    {
        return -ENOENT;
    }

    if ((fi->flags & O_ACCMODE) != O_RDONLY)
    {
        return -EACCES;
    }

    return 0;
}

static int sbb_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    Entry* entry = g_fileHunter->getFileSystem()->findByPath(path);
    printf("XXX: sbb_read: path=%s, entry=%p\n", path, entry);
    if (entry == nullptr)
    {
        return -ENOENT;
    }

    return g_fileHunter->getFileSystem()->read(g_fileHunter->getFD(), entry, buf, offset, size);
}

static const struct fuse_operations sbb_oper = {
    .getattr	= sbb_getattr,
    .open		= sbb_open,
    .read		= sbb_read,
    .readdir	= sbb_readdir,
    .init       = sbb_init,
};

int main(int argc, char** argv)
{
#if 0
    if (argc < 3)
    {
        return -1;
    }
    const char* image = argv[1];
    const char* dbPath = argv[2];
#endif
    const char* image = "/dev/disk5";
    const char* dbPath = "/Users/ian/projects/superbadblock/usb.db";
    //const char* image = "disk1.dmg";
    //const char* dbPath = "disk1.db";
    g_fileHunter = new DiskScanner(image, dbPath);

    g_fileHunter->open();


    /*
    Entry* entry = g_fileHunter->getFileSystem()->findByPath("/Downloads");
    if (entry != nullptr)
    {
        vector<Entry*> children = g_fileHunter->getFileSystem()->findByParentInode(entry->inode);
        for (Entry* child: children)
        {
            printf("%s\n", child->name.c_str());
        }
    }
     */

    /*
    Entry* entry = g_fileHunter->getFileSystem()->findByPath("/Downloads/dinoshade.c");
    char* buffer = new char[entry->length];
    g_fileHunter->getFileSystem()->read(g_fileHunter->getFD(), entry, buffer, 0, entry->length);

    hexdump(buffer, entry->length);

    delete[] buffer;
     */

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int ret = fuse_main(args.argc, args.argv, &sbb_oper, NULL);

    delete g_fileHunter;
    return 0;

}
