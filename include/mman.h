#ifndef _MMAN_H
#define _MMAN_H

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