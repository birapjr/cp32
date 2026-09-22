#ifndef CP32_MINIX_SUPER_H
#define CP32_MINIX_SUPER_H
#include "ramdisk.h"

/* Read-only recognition, not a mount or an inode/bitmap integrity check. */
#define CP32_SUPER_OK 0
#define CP32_SUPER_ABSENT 1
#define CP32_SUPER_UNSUPPORTED 2
#define CP32_SUPER_INVALID 3
#define CP32_SUPER_IO 4
struct cp32_minix_super {
  unsigned ninodes, zones, first_data_zone, log_zone_size, max_size;
  unsigned imap_blocks, zmap_blocks, swapped;
};
typedef int (*cp32_disk_reader)(unsigned offset, char *buffer, int count);
CP32_IRAM_EXT int cp32_minix_super_read(cp32_disk_reader read,
    unsigned capacity, struct cp32_minix_super *out);
#endif
