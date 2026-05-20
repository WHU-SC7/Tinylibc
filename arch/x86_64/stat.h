#ifndef _STAT_H
#define _STAT_H
struct stat {
	dev_t st_dev;
	ino_t st_ino;
	nlink_t st_nlink;
    unsigned int __pad0;    // 填充

	mode_t st_mode;
	uid_t st_uid;
	gid_t st_gid;
	unsigned int    __pad1;
	dev_t st_rdev;
	off_t st_size;
	blksize_t st_blksize;
	blkcnt_t st_blocks;

	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	long __unused[3];
};

// 文件类型掩码
#define S_IFMT     0170000   // 文件类型位掩码

// 文件类型
#define S_IFIFO    0010000   // FIFO/管道
#define S_IFCHR    0020000   // 字符设备
#define S_IFDIR    0040000   // 目录
#define S_IFBLK    0060000   // 块设备
#define S_IFREG    0100000   // 普通文件
#define S_IFLNK    0120000   // 符号链接
#define S_IFSOCK   0140000   // Socket

// 测试宏（GNU 兼容）
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#endif