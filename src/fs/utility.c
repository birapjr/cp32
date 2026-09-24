/* MINIX utility.c: endian conversion. Explicit bytes preserve the disk ABI
 * without relying on Xtensa alignment or host structure layouts. */
#include "fs.h"

CP32_IRAM_EXT unsigned cp32_fs_u16(const unsigned char *p, unsigned swap)
{
  return swap ? ((unsigned)p[0] << 8) | p[1]
              : ((unsigned)p[1] << 8) | p[0];
}

CP32_IRAM_EXT unsigned cp32_fs_u32(const unsigned char *p, unsigned swap)
{
  return swap ? (cp32_fs_u16(p, swap) << 16) | cp32_fs_u16(p + 2, swap)
              : (cp32_fs_u16(p + 2, swap) << 16) | cp32_fs_u16(p, swap);
}
