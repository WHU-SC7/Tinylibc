#include "tlibc_everything.h"

int tlibc_get_file_len(char *path)
{
    int fd = openat(AT_FDCWD, path, O_RDONLY, 0644);
    if(fd < 0)
    {
        return -1;
    }
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    int ret = fstat(fd, &statbuf);
    if(ret != 0)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return statbuf.st_size;
}