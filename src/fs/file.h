#ifndef CP32_FS_FILE_H
#define CP32_FS_FILE_H
#include "type.h"
/* Caller-owned read-only handle; no shared filp/descriptor table yet. */
struct cp32_minix_file {
  cp32_disk_reader read;
  unsigned size, position, zone_bytes, zones[7];
};
#endif
