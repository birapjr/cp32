#include "kernel.h"
#include "proc.h"
#include <string.h>

/* 
 * numap: translate virtual address to physical address.
 * Returns 0 if address is out of bounds for the process.
 */
PUBLIC phys_bytes numap(int proc_nr, vir_bytes vir, vir_bytes len)
{
    struct proc *rp = &proc[proc_nr];
    int i;

    for (i = 0; i < NR_SEGS; i++) {
        if (rp->p_map[i].mem_len > 0 &&
            vir >= (vir_bytes)rp->p_map[i].mem_vir &&
            vir + len <= (vir_bytes)(rp->p_map[i].mem_vir + rp->p_map[i].mem_len)) {
            
            phys_bytes phys = (phys_bytes)rp->p_map[i].mem_phys << CLICK_SHIFT;
            phys += (vir - (vir_bytes)rp->p_map[i].mem_vir);
            return phys;
        }
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
        usbj_print("[MM] mem_copy: BOUNDS ERROR\r\n");
        return EFAULT;
    }

    usbj_print("[MM] mem_copy: src=");
    usbj_print_u32(src_phys);
    usbj_print(" dst=");
    usbj_print_u32(dst_phys);
    usbj_print(" len=");
    usbj_print_u32(len);
    usbj_print("\r\n");

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


