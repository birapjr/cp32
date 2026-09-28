#include "ramdisk.h"
#include "minix-demo.h"

/* Before tasks start: provision the volatile boot disk, never a live mount. */
CP32_IRAM_EXT int cp32_minix_demo_init(void)
{
  cp32_ramdisk_reset();
  unsigned i, position=0;
  int result;
  for(i=0;i<sizeof(cp32_demo_runs)/sizeof(cp32_demo_runs[0]);i++) {
    unsigned count=cp32_demo_runs[i][1];
    result=cp32_ramdisk_write_bytes(cp32_demo_runs[i][0],
                                    cp32_demo_bytes+position,count);
    if(result) return result;
    position+=count;
  }
  return 0;
}
