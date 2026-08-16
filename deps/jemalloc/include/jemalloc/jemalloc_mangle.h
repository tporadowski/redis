#ifdef JEMALLOC_MANGLE
#  ifndef JEMALLOC_NO_DEMANGLE
#    define JEMALLOC_NO_DEMANGLE
#  endif
#  define aligned_alloc je_aligned_alloc
#  define calloc je_calloc
#  define dallocx je_dallocx
#  define free je_free
#  define mallctl je_mallctl
#  define mallctlbymib je_mallctlbymib
#  define mallctlnametomib je_mallctlnametomib
#  define malloc je_malloc
#  define malloc_conf je_malloc_conf
#  define malloc_message je_malloc_message
#  define malloc_stats_print je_malloc_stats_print
#  define malloc_usable_size je_malloc_usable_size
#  define mallocx je_mallocx
#  define nallocx je_nallocx
#  define posix_memalign je_posix_memalign
#  define rallocx je_rallocx
#  define realloc je_realloc
#  define sallocx je_sallocx
#  define sdallocx je_sdallocx
#  define xallocx je_xallocx
#  define get_defrag_hint je_get_defrag_hint
#  define malloc_with_usize je_malloc_with_usize
#  define calloc_with_usize je_calloc_with_usize
#  define realloc_with_usize je_realloc_with_usize
#  define free_with_usize je_free_with_usize
#endif

#ifndef JEMALLOC_NO_DEMANGLE
#  undef je_aligned_alloc
#  undef je_calloc
#  undef je_dallocx
#  undef je_free
#  undef je_mallctl
#  undef je_mallctlbymib
#  undef je_mallctlnametomib
#  undef je_malloc
#  undef je_malloc_conf
#  undef je_malloc_message
#  undef je_malloc_stats_print
#  undef je_malloc_usable_size
#  undef je_mallocx
#  undef je_nallocx
#  undef je_posix_memalign
#  undef je_rallocx
#  undef je_realloc
#  undef je_sallocx
#  undef je_sdallocx
#  undef je_xallocx
#  undef je_get_defrag_hint
#  undef je_malloc_with_usize
#  undef je_calloc_with_usize
#  undef je_realloc_with_usize
#  undef je_free_with_usize
#endif
