#include "kernel.h"
#include <minix/com.h>
#include <minix/callnr.h>
#include "ramdisk.h"

/* MINIX driver.c:do_rdwt and memory.c:m_schedule, limited to /dev/ram.
 * POSITION and COUNT are bytes, not sectors; EOF returns a short count.
 * CP32 uses mapped internal SRAM buffers instead of x86 segmented memory.
 * Only FS brokers device requests; no /dev/mem or /dev/kmem is exposed. */
CP32_IRAM_EXT PRIVATE int cp32_mem_request(const message *request)
{
  char buffer[64];
  phys_bytes user;
  unsigned offset, count, transferred = 0, capacity = cp32_ramdisk_capacity();
  if (request->m_source != FS_PROC_NR) return EPERM;
  if (request->DEVICE != RAM_DEV) return ENXIO;
  switch (request->m_type) {
    case DEV_OPEN:
    case DEV_CLOSE:
      return OK;
    case DEV_READ:
    case DEV_WRITE:
      break;
    default:
      return EINVAL;
  }
  if (request->COUNT <= 0 || request->POSITION < 0) return EINVAL;
  /* Validate the whole caller buffer before changing either disk or memory,
   * even for a short transfer at EOF, as in MINIX m_schedule. */
  user = numap(request->PROC_NR, (vir_bytes)request->ADDRESS, request->COUNT);
  if (user == 0) return EFAULT;
  if ((unsigned long)request->POSITION >= capacity) return 0;
  offset = (unsigned)request->POSITION;
  count = (unsigned)request->COUNT;
  if (count > capacity - offset) count = capacity - offset;
  while (transferred < count) {
    unsigned chunk = count - transferred;
    if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
    if (request->m_type == DEV_READ) {
      if (cp32_ramdisk_read_bytes(offset + transferred, buffer, chunk) != 0)
        return EIO;
      phys_copy(vir2phys(buffer), user + transferred, chunk);
    } else {
      phys_copy(user + transferred, vir2phys(buffer), chunk);
      if (cp32_ramdisk_write_bytes(offset + transferred, buffer, chunk) != 0)
        return EIO;
    }
    transferred += chunk;
  }
  return (int)transferred;
}

/* Removable, rate-limited trace from the normal device service path. */
CP32_IRAM_EXT PRIVATE void cp32_trace_mem_request(const message *request, int result)
{
  static unsigned requests;
  if (++requests <= 8 || requests % 5000 == 0) {
    int saved_ps = lock_save();
    usbj_print("[RAM V49 op="); usbj_print_u32((uint32_t)request->m_type);
    usbj_print(" result="); usbj_print_u32((uint32_t)result);
    usbj_print(" n="); usbj_print_u32(requests); usbj_print("]\r\n");
    restore_lock(saved_ps);
  }
}

CP32_IRAM_EXT PUBLIC void mem_task(void)
{
  message request;
  for (;;) {
    int caller, owner, result;
    if (receive(ANY, &request) != OK) panic("RAM receive failed", NO_NUM);
    /* Match MINIX driver_task: ignore stale IRQs and non-FS requests. */
    if (request.m_source != FS_PROC_NR) continue;
    caller = request.m_source;
    owner = request.PROC_NR;
    result = cp32_mem_request(&request);
    cp32_trace_mem_request(&request, result);
    request.m_type = TASK_REPLY;
    request.REP_PROC_NR = owner;
    request.REP_STATUS = result;
    if (send(caller, &request) != OK) panic("RAM reply failed", NO_NUM);
  }
}
