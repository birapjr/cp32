#include <string.h>
#include "../src/kernel/ramdisk.h"

int main(void)
{
  unsigned char in[CP32_RAMDISK_SECTOR_SIZE];
  unsigned char out[CP32_RAMDISK_SECTOR_SIZE];
  unsigned i;
  if (cp32_ramdisk_sector_size() != CP32_RAMDISK_SECTOR_SIZE) return 1;
  if (cp32_ramdisk_sector_count() != CP32_RAMDISK_SECTORS) return 1;
  if (cp32_ramdisk_capacity() !=
      CP32_RAMDISK_SECTOR_SIZE * CP32_RAMDISK_SECTORS) return 1;
  if (cp32_ramdisk_write_bytes(3, in, 7) != 0) return 1;
  if (cp32_ramdisk_read_bytes(3, out, 7) != 0 || memcmp(in, out, 7) != 0) return 1;
  if (cp32_ramdisk_read_bytes(cp32_ramdisk_capacity() - 1, out, 2) == 0) return 1;
  for (i = 0; i < sizeof(in); ++i) in[i] = (unsigned char)i;
  cp32_ramdisk_reset();
  if (cp32_ramdisk_read(0, out) != 0) return 1;
  for (i = 0; i < sizeof(out); ++i) if (out[i] != 0) return 1;
  if (cp32_ramdisk_write(0, in) != 0) return 1;
  if (cp32_ramdisk_read(0, out) != 0 || memcmp(in, out, sizeof(in)) != 0) return 1;
  if (cp32_ramdisk_read(CP32_RAMDISK_SECTORS, out) == 0) return 1;
  if (cp32_ramdisk_write(0, (const void *)0) == 0) return 1;
  return 0;
}
