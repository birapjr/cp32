#ifndef CP32_MM_PROTOCOL_H
#define CP32_MM_PROTOCOL_H

/* CP32 bring-up MM service, not the full MINIX process-management ABI.
 * ALLOCATE: m1_i1 is a click count; reply m1_i1 is the allocated base click.
 * RELEASE: m1_i1 is the exact base click. Reply m_type is OK or an error.
 * Ownership always comes from the IPC-supplied m_source. */
#define CP32_MM_ALLOCATE 1001
#define CP32_MM_RELEASE  1002

#endif
