#ifndef _MMAN_H
#define _MMAN_H

/* ── 内存保护标志（来自 pthread.h） ── */
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0
#define PROT_GROWSDOWN  0x01000000
#define PROT_GROWSUP    0x02000000

/* ── mmap 映射类型（来自 pthread.h） ── */
#define MAP_SHARED       0x01
#define MAP_PRIVATE      0x02
#define MAP_SHARED_VALIDATE 0x03
#define MAP_TYPE         0x0f
#define MAP_ANONYMOUS    0x20
#define MAP_ANON         MAP_ANONYMOUS

#define MAP_FAILED ((void *) -1)

/* ── madvise 建议（原有） ── */
# define MADV_NORMAL	  0	/* No further special treatment.  */
# define MADV_RANDOM	  1	/* Expect random page references.  */
# define MADV_SEQUENTIAL  2	/* Expect sequential page references.  */
# define MADV_WILLNEED	  3	/* Will need these pages.  */
# define MADV_DONTNEED	  4	/* Don't need these pages.  */
# define MADV_FREE	  8	/* Free pages only if memory pressure.  */
# define MADV_REMOVE	  9	/* Remove these pages and resources.  */
# define MADV_DONTFORK	  10	/* Do not inherit across fork.  */
# define MADV_DOFORK	  11	/* Do inherit across fork.  */
# define MADV_MERGEABLE	  12	/* KSM may merge identical pages.  */
# define MADV_UNMERGEABLE 13	/* KSM may not merge identical pages.  */
# define MADV_HUGEPAGE	  14	/* Worth backing with hugepages.  */
# define MADV_NOHUGEPAGE  15	/* Not worth backing with hugepages.  */
# define MADV_DONTDUMP	  16    /* Explicity exclude from the core dump,
                                   overrides the coredump filter bits.  */
# define MADV_DODUMP	  17	/* Clear the MADV_DONTDUMP flag.  */
# define MADV_WIPEONFORK  18	/* Zero memory on fork, child only.  */
# define MADV_KEEPONFORK  19	/* Undo MADV_WIPEONFORK.  */
# define MADV_COLD        20	/* Deactivate these pages.  */
# define MADV_PAGEOUT     21	/* Reclaim these pages.  */
# define MADV_POPULATE_READ 22	/* Populate (prefault) page tables
				   readable.  */
# define MADV_POPULATE_WRITE 23	/* Populate (prefault) page tables
				   writable.  */
# define MADV_HWPOISON	  100	/* Poison a page for testing.  */

#endif