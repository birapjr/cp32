#ifndef CP32_APP_HEAP_H
#define CP32_APP_HEAP_H
#include "../hello/abi.h"
/* Initialize once per process, before allocation. Owns sbrk exclusively after
 * success; do not mix direct break changes with this allocator. Single-threaded.
 * malloc(0)/exhaustion returns NULL. free(NULL) is harmless. Reused bytes are
 * unspecified. Blocks have 16-byte alignment; free does not shrink the break. */
int cp32_heap_init(const struct cp32_app_services *services);
void *cp32_malloc(unsigned size);
void cp32_free(void *pointer);
/* calloc checks multiplication overflow and clears reused storage.
 * realloc(NULL,n) allocates; realloc(p,0) frees. Failure preserves p.
 * Shrinking returns reusable tail space; growing uses adjacent free space
 * when possible, otherwise may move. Old contents are preserved. */
void *cp32_calloc(unsigned count,unsigned size);
void *cp32_realloc(void *pointer,unsigned size);
/* Release a free top block: bytes returned, 0 if none, -1 on failure. */
int cp32_heap_trim(void);
#endif
