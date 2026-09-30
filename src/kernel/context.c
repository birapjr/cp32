/* MINIX exec register reset adapted to Xtensa call0. Placement/ownership
 * validation belongs to the caller; this helper publishes no runnable state. */
#include "kernel.h"
#include "proc.h"
CP32_IRAM_EXT int cp32_exec_frame(struct proc *rp,reg_t pc,reg_t sp,reg_t argument)
{
  if(rp==NIL_PROC || !pc || !sp || (sp & 15)) return EINVAL;
  memset(&rp->p_reg,0,sizeof(rp->p_reg));
  rp->p_reg.pc=pc;
  rp->p_reg.psw=0x100; /* EXCM for initial RFE, INTLEVEL zero */
  rp->p_reg.sp=sp;
  rp->p_reg.a[1]=sp;
  rp->p_reg.a[15]=sp;
  rp->p_reg.a[2]=argument;
  return OK;
}
