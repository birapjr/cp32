#ifndef CP32_FS_INODE_H
#define CP32_FS_INODE_H
#include "type.h"
#include "super.h"
/* Validated directory inode and traversal state, not an inode cache. */
struct cp32_minix_dir {
  struct cp32_minix_super super;
  cp32_disk_reader read;
  unsigned size, position, zones[7];
};
#endif
