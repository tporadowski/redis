/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

void	*pages_map(void *addr, size_t size);
void	pages_unmap(void *addr, size_t size);
void	*pages_trim(void *addr, size_t alloc_size, size_t leadsize,
    size_t size);
bool	pages_commit(void *addr, size_t size);
bool	pages_decommit(void *addr, size_t size);
bool	pages_purge(void *addr, size_t size);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/

#ifdef USE_WIN32_EXTERNAL_HEAP_ALLOC
/*
 * Instead of using VirtualAlloc and VirtualFree we will call external
 * AllocHeapBlock, FreeHeapBlock and PurgePages that may have additional
 * logic related to memory allocations.
 * This is an extension used by Redis for Windows (https://github.com/tporadowski/redis).
 */
extern LPVOID AllocHeapBlock(LPVOID addr, size_t size, BOOL zero);
extern BOOL FreeHeapBlock(LPVOID addr, size_t size);
extern BOOL PurgePages(LPVOID addr, size_t length); 
#endif