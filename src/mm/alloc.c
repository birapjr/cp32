/* MINIX mm/alloc.c: physical allocation, release and coalescing.
 * CP32 uses a bounded owner-tagged block table over its internal SRAM heap. */
#include "mm.h"

#define CP32_MAX_MEM_BLOCKS 64
struct cp32_mem_block { phys_clicks base, size; int owner, used; };
static struct cp32_mem_block cp32_mem_blocks[CP32_MAX_MEM_BLOCKS];
static int cp32_mem_allocator_ready;

CP32_IRAM_EXT static void cp32_mem_allocator_init(void)
{
    int i;
    for (i = 0; i < CP32_MAX_MEM_BLOCKS; i++) {
        cp32_mem_blocks[i].base = 0;
        cp32_mem_blocks[i].size = 0;
        cp32_mem_blocks[i].owner = -1;
        cp32_mem_blocks[i].used = FALSE;
    }
    cp32_mem_blocks[0].base = mem[1].base;
    cp32_mem_blocks[0].size = mem[1].size;
    cp32_mem_allocator_ready = TRUE;
}

CP32_IRAM_EXT PUBLIC phys_clicks cp32_mem_alloc(phys_clicks clicks, int owner)
{
    int i, j;
    if (clicks == 0 || owner < -NR_TASKS || owner >= NR_PROCS ||
        (uint64_t)clicks > 0x100000ULL) return 0;
    if (!cp32_mem_allocator_ready) cp32_mem_allocator_init();
    for (i = 0; i < CP32_MAX_MEM_BLOCKS; i++) {
        if (cp32_mem_blocks[i].used || cp32_mem_blocks[i].size < clicks) continue;
        for (j = 1; j < CP32_MAX_MEM_BLOCKS; j++)
            if (cp32_mem_blocks[j].size == 0) break;
        if (j == CP32_MAX_MEM_BLOCKS) return 0;
        cp32_mem_blocks[j] = cp32_mem_blocks[i];
        cp32_mem_blocks[j].base += clicks;
        cp32_mem_blocks[j].size -= clicks;
        cp32_mem_blocks[i].size = clicks;
        cp32_mem_blocks[i].owner = owner;
        cp32_mem_blocks[i].used = TRUE;
        return cp32_mem_blocks[i].base;
    }
    return 0;
}

CP32_IRAM_EXT PUBLIC int cp32_mem_free(phys_clicks base, int owner)
{
    int i, j;
    for (i = 0; i < CP32_MAX_MEM_BLOCKS; i++) {
        if (base == 0) return EINVAL;
        if (cp32_mem_blocks[i].used && cp32_mem_blocks[i].base == base) {
            if (cp32_mem_blocks[i].owner != owner) return EACCES;
            cp32_mem_blocks[i].used = FALSE;
            cp32_mem_blocks[i].owner = -1;
            /* Coalesce adjacent free blocks so repeated IPC allocation and
             * release does not permanently fragment the CP32 heap. */
            for (j = 0; j < CP32_MAX_MEM_BLOCKS; j++) {
                if (j == i || cp32_mem_blocks[j].used ||
                    cp32_mem_blocks[j].size == 0) continue;
                if (cp32_mem_blocks[j].base + cp32_mem_blocks[j].size ==
                    cp32_mem_blocks[i].base) {
                    cp32_mem_blocks[j].size += cp32_mem_blocks[i].size;
                    cp32_mem_blocks[i].base = 0;
                    cp32_mem_blocks[i].size = 0;
                    i = j;
                    j = -1;
                } else if (cp32_mem_blocks[i].base + cp32_mem_blocks[i].size ==
                           cp32_mem_blocks[j].base) {
                    cp32_mem_blocks[i].size += cp32_mem_blocks[j].size;
                    cp32_mem_blocks[j].base = 0;
                    cp32_mem_blocks[j].size = 0;
                    j = -1;
                }
            }
            return OK;
        }
    }
    return EINVAL;
}

/* Keep the allocator's public boundary strict: callers may only release the
 * beginning of a block they own.  The coalescing loop above deliberately
 * leaves zero-sized slots reusable, so this is safe under repeated IPC use. */

CP32_IRAM_EXT PUBLIC int cp32_mem_owned(phys_clicks base, phys_clicks clicks, int owner)
{
    int i;
    if (clicks == 0 || owner < -NR_TASKS || owner >= NR_PROCS) return FALSE;
    for (i = 0; i < CP32_MAX_MEM_BLOCKS; i++) {
        if (cp32_mem_blocks[i].used && cp32_mem_blocks[i].owner == owner &&
            base >= cp32_mem_blocks[i].base &&
            clicks <= cp32_mem_blocks[i].size &&
            (uint64_t)base + clicks <= 0x100000000ULL &&
            base - cp32_mem_blocks[i].base <=
                cp32_mem_blocks[i].size - clicks)
            return TRUE;
    }
    return FALSE;
}

