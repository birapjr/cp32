/* MINIX mm/exec.c patch_ptr and lib/i386/rts/crtso.s stack contract,
 * adapted to 32-bit little-endian pointers and Xtensa 16-byte SP alignment.
 * Staging only: no process maps or runnable context are changed here. */
#include "exec.h"

CP32_IRAM_EXT static void stack_word(unsigned char *p, uint32_t value)
{
  unsigned i;
  for(i=0;i<4;i++) p[i]=(unsigned char)(value>>(i*8));
}

CP32_IRAM_EXT int cp32_exec_stack(unsigned char *buffer, unsigned capacity,
    uint32_t base, unsigned argc, const char *const argv[],
    unsigned envc, const char *const envp[], uint32_t *sp)
{
  unsigned lengths[CP32_EXEC_VECTOR_MAX], total, count, i, n, start, offset, slot;
  uint32_t top;
  if(!buffer || !sp || (argc && !argv) || (envc && !envp) ||
      (base & 15U) || (capacity & 15U) || !capacity ||
      capacity>CP32_EXEC_STACK_MAX || base>0xffffffffU-capacity)
    return -1;
  if(argc>CP32_EXEC_VECTOR_MAX || envc>CP32_EXEC_VECTOR_MAX-argc) return -2;
  count=argc+envc;
  total=(count+3)*4;
  if(total>capacity) return -2;
  /* Validate everything before publishing any bytes. Strings are trusted,
   * but scan only up to the staging size to bound malformed inputs. */
  for(i=0;i<count;i++) {
    const char *s=i<argc ? argv[i] : envp[i-argc];
    if(!s) return -1;
    for(n=0;n<capacity-total && s[n];n++) {}
    if(n==capacity-total) return -2;
    lengths[i]=n+1;
    total+=n+1;
  }
  total=(total+15U)&~15U;
  if(total>capacity) return -2;
  start=capacity-total;
  top=base+start;
  for(i=start;i<capacity;i++) buffer[i]=0;
  stack_word(buffer+start,argc);
  offset=(count+3)*4;
  for(i=0;i<count;i++) {
    const char *s=i<argc ? argv[i] : envp[i-argc];
    slot=i<argc ? i+1 : i+2; /* skip argv's terminating NULL */
    stack_word(buffer+start+slot*4,top+offset);
    for(n=0;n<lengths[i];n++) buffer[start+offset+n]=(unsigned char)s[n];
    offset+=lengths[i];
  }
  *sp=top;
  return 0;
}
