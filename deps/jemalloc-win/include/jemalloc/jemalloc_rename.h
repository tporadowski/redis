/*
 * Name mangling for public symbols is controlled by --with-mangling and
 * --with-jemalloc-prefix.  With default settings the je_ prefix is stripped by
 * these macro definitions.
 */
#ifndef JEMALLOC_NO_RENAME
#  define je_malloc_conf je_malloc_conf
#  define je_malloc_message je_malloc_message
#  define je_malloc je_malloc
#  define je_calloc je_calloc
#  define je_posix_memalign je_posix_memalign
#  define je_aligned_alloc je_aligned_alloc
#  define je_realloc je_realloc
#  define je_free je_free
#  define je_mallocx je_mallocx
#  define je_rallocx je_rallocx
#  define je_xallocx je_xallocx
#  define je_sallocx je_sallocx
#  define je_dallocx je_dallocx
#  define je_sdallocx je_sdallocx
#  define je_nallocx je_nallocx
#  define je_mallctl je_mallctl
#  define je_mallctlnametomib je_mallctlnametomib
#  define je_mallctlbymib je_mallctlbymib
#  define je_malloc_stats_print je_malloc_stats_print
#  define je_malloc_usable_size je_malloc_usable_size
#endif
