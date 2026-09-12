#include "kernel.h"
#include "proc.h"
#include <string.h>
#include <minix/com.h>
#include <minix/callnr.h>

/* Private CP32 MM protocol.  Message fields use MINIX m1 semantics. */
#define CP32_MM_ALLOCATE 1001
#define CP32_MM_RELEASE  1002

#define CP32_MAX_MEM_BLOCKS 64
struct cp32_mem_block { phys_clicks base, size; int owner, used; };
static struct cp32_mem_block cp32_mem_blocks[CP32_MAX_MEM_BLOCKS];
static int cp32_mem_allocator_ready;

static void cp32_mem_allocator_init(void)
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

PUBLIC phys_clicks cp32_mem_alloc(phys_clicks clicks, int owner)
{
    int i, j;
    if (clicks == 0 || owner < -NR_TASKS || owner >= NR_PROCS) return 0;
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

PUBLIC int cp32_mem_free(phys_clicks base, int owner)
{
    int i, j;
    for (i = 0; i < CP32_MAX_MEM_BLOCKS; i++) {
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

PUBLIC int cp32_mem_owned(phys_clicks base, phys_clicks clicks, int owner)
{
    int i;
    if (clicks == 0) return FALSE;
    for (i = 0; i < CP32_MAX_MEM_BLOCKS; i++) {
        if (cp32_mem_blocks[i].used && cp32_mem_blocks[i].owner == owner &&
            base >= cp32_mem_blocks[i].base &&
            clicks <= cp32_mem_blocks[i].size &&
            base - cp32_mem_blocks[i].base <=
                cp32_mem_blocks[i].size - clicks)
            return TRUE;
    }
    return FALSE;
}

/* 
 * numap: translate virtual address to physical address.
 * Returns 0 if address is out of bounds for the process.
 */
PUBLIC phys_bytes numap(int proc_nr, vir_bytes vir, vir_bytes len)
{
    /* proc_nr is a MINIX process number, not a raw proc[] index. */
    struct proc *rp;
    int i;

    if (!isokprocn(proc_nr) || len == 0 || len - 1 > (vir_bytes)-1 - vir)
        return 0;
    rp = proc_addr(proc_nr);
    if (rp == NIL_PROC || (rp->p_flags & P_SLOT_FREE)) return 0;

    /* Kernel tasks pass IPC buffers on their kernel stacks.  Those buffers
     * are already physical flat addresses on ESP32-S3 and do not fit the
     * synthetic MINIX segment map used for user processes. */
    if (istaskp(rp) && (uint64_t)vir >= 0x3FC00000ULL &&
        (uint64_t)vir + len <= 0x3FD00000ULL)
        return (phys_bytes)vir;

    for (i = 0; i < NR_SEGS; i++) {
        uint64_t base = rp->p_map[i].mem_vir;
        uint64_t size = (uint64_t)rp->p_map[i].mem_len << CLICK_SHIFT;
        uint64_t offset;
        uint64_t phys;
        if ((uint64_t)vir < base) continue;
        offset = (uint64_t)vir - base;
        if (offset > size || (uint64_t)len > size - offset) continue;
        phys = ((uint64_t)rp->p_map[i].mem_phys << CLICK_SHIFT) + offset;
        if (phys > (phys_bytes)-1 || len - 1 > (phys_bytes)-1 - phys)
            continue;
        return (phys_bytes)phys;
    }
    return 0;
}

/* 
 * mem_copy: copy data from one process to another.
 * Uses numap to ensure both addresses are valid.
 */
PUBLIC int mem_copy(int src_proc, vir_bytes src_vir, int dst_proc, vir_bytes dst_vir, vir_bytes len)
{
    phys_bytes src_phys = numap(src_proc, src_vir, len);
    phys_bytes dst_phys = numap(dst_proc, dst_vir, len);

    if (src_phys == 0 || dst_phys == 0) {
        usbj_print("[MM E src="); usbj_print_u32((uint32_t)src_proc);
        usbj_print(" dst="); usbj_print_u32((uint32_t)dst_proc);
        usbj_print(" sp="); usbj_print_u32(src_phys);
        usbj_print(" dp="); usbj_print_u32(dst_phys);
        usbj_print("]\r\n");
        return EFAULT;
    }

    phys_copy(src_phys, dst_phys, len);
    return OK;
}

/* Basic MM Task entry point */
PUBLIC void mm_task()
{
    message m;
    phys_clicks base;

    usbj_print("[MM_TASK]\r\n");
    for (;;) {
        /* A real server must block here.  This makes MM ownership visible to
         * the scheduler and exercises the normal IPC suspension/resumption
         * path instead of consuming CPU in a private idle loop. */
        receive(ANY, &m);

        switch (m.m_type) {
        case CP32_MM_ALLOCATE:
            base = cp32_mem_alloc((phys_clicks)m.m1_i1, m.m_source);
            m.m1_i1 = base;
            m.m_type = base != 0 ? OK : ENOMEM;
            break;
        case CP32_MM_RELEASE:
            m.m_type = cp32_mem_free((phys_clicks)m.m1_i1, m.m_source);
            break;
        default:
            m.m_type = E_BAD_FCN;
            break;
        }
        send(m.m_source, &m);
    }
}
