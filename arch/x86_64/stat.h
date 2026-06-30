#ifndef _STAT_H
#define _STAT_H
struct stat {
	dev_t st_dev;          // 8  @0
	ino_t st_ino;          // 8  @8
	nlink_t st_nlink;      // 8  @16

	mode_t st_mode;        // 4  @24
	uid_t st_uid;          // 4  @28
	gid_t st_gid;          // 4  @32
	unsigned int __pad0;   // 4  @36  — 对齐 st_rdev 到 8 字节

	dev_t st_rdev;         // 8  @40
	off_t st_size;         // 8  @48
	blksize_t st_blksize;  // 8  @56
	blkcnt_t st_blocks;    // 8  @64

	struct timespec st_atim;  // 16 @72
	struct timespec st_mtim;  // 16 @88
	struct timespec st_ctim;  // 16 @104
	long __unused[3];        // 24 @120
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

/* ── statfs 结构（用于 df，x86_64 布局） ── */

struct statfs {
    long f_type;
    long f_bsize;
    long f_blocks;
    long f_bfree;
    long f_bavail;
    long f_files;
    long f_ffree;
    long f_fsid;
    long f_namelen;
    long f_frsize;
    long f_flags;
    long f_spare[4];
};

#endif