/* MINIX mm/main.c: receive, dispatch and reply.
 * Keep mm_task as the entry symbol while MM is linked into the kernel image. */
#include "mm.h"

CP32_IRAM_EXT PRIVATE int cp32_mm_source_valid(int source)
{
    struct proc *rp;
    if (!isokprocn(source)) return FALSE;
    rp = proc_addr(source);
    return rp != NIL_PROC && !(rp->p_flags & P_SLOT_FREE) && rp->p_nr == source;
}

CP32_IRAM_EXT PUBLIC int cp32_mm_handle_request(message *m)
{
    if (m == (message *)0 || !cp32_mm_source_valid(m->m_source))
        return EINVAL;
    if (m->m_type == CP32_MM_ALLOCATE) {
        m->m1_i1 = cp32_mem_alloc((phys_clicks)m->m1_i1, m->m_source);
        m->m_type = m->m1_i1 != 0 ? OK : ENOMEM;
    } else if (m->m_type == CP32_MM_RELEASE) {
        m->m_type = cp32_mem_free((phys_clicks)m->m1_i1, m->m_source);
    } else {
        m->m_type = E_BAD_FCN;
    }
    return m->m_type;
}

/* Removable trace after an actual MM receive resumes. */
CP32_IRAM_EXT PRIVATE void cp32_trace_mm_receive(const message *m)
{
    static unsigned received;
    int saved_ps;
    extern struct proc *current_proc;
    if (++received > 2 && received % 5000 != 0) return;
    saved_ps = lock_save();
    usbj_print("[MM V44 received="); usbj_print_u32(received);
    usbj_print(" op="); usbj_print_u32((uint32_t)m->m_type);
    usbj_print(" source="); usbj_print_u32((uint32_t)m->m_source);
    usbj_print(" resumed=");
    usbj_print_u32((uint32_t)(proc_ptr == proc_addr(MM_PROC_NR) &&
        current_proc == proc_ptr && proc_ptr->p_flags == 0 &&
        !proc_ptr->p_blocked_frame_valid && k_reenter == 0));
    usbj_print("]\r\n");
    restore_lock(saved_ps);
}

/* Minimal MM service: receive, handle, reply, then block again. */
CP32_IRAM_EXT PUBLIC void mm_task()
{
    message m;
    usbj_print("[MM_TASK]\r\n");
    for (;;) {
        if (receive(ANY, &m) != OK)
            panic("MM receive failed", NO_NUM);
        cp32_trace_mm_receive(&m);
        if (!cp32_mm_source_valid(m.m_source)) continue;
        cp32_mm_handle_request(&m);
        if (send(m.m_source, &m) != OK)
            panic("MM reply failed", NO_NUM);
    }
}
