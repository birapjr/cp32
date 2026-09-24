#ifndef CP32_FS_FILE_H
#define CP32_FS_FILE_H
#include "type.h"
#include "super.h"
/* Read-only open state; filedes.c shares these through reference counts. */
struct cp32_minix_file {
  cp32_disk_reader read;
  unsigned size, position, zone_bytes, zones[7];
  struct cp32_minix_super super;
  unsigned indirect, double_indirect, inode;
};
#endif
