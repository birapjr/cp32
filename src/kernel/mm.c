#include "kernel.h"
#include "proc.h"
#include <string.h>

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
    usbj_print("[MM] mm_task starting...\r\n");
    
    /* In a real MINIX system, this would manage process memory allocation.
     * For CP32, we start with static mapping handled by the kernel. */
    
    usbj_print("[MM] MM foundation initialized\r\n");
    
    /* Idle loop for the MM task */
    while(1) {
        // The MM task typically waits for messages from the kernel/processes.
        // We'll implement the message loop once IPC is operational.
    }
}
