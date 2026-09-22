#include "ramdisk.h"
#include "minix-demo.h"

/* Before tasks start: provision the volatile boot disk, never a live mount. */
CP32_IRAM_EXT int cp32_minix_demo_init(void)
{
  cp32_ramdisk_reset();
  return cp32_ramdisk_write_bytes(0, cp32_demo_bytes, sizeof(cp32_demo_bytes));
}
