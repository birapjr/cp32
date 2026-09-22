#ifndef CP32_MINIX_DIR_H
#define CP32_MINIX_DIR_H
#include "minix-super.h"

/* Read-only root directory handle. No global mount state or shared buffer. */
struct cp32_minix_dir {
  struct cp32_minix_super super;
  cp32_disk_reader read;
  unsigned size, position, zones[7];
};
CP32_IRAM_EXT int cp32_minix_root_open(cp32_disk_reader read, unsigned capacity,
                                      struct cp32_minix_dir *dir);
/* 1 = entry, 0 = EOF, negative CP32_SUPER_* = error. Name is NUL terminated. */
CP32_IRAM_EXT int cp32_minix_root_next(struct cp32_minix_dir *dir,
                                      unsigned *inode, char name[15]);
#define CP32_FILE_NOT_FOUND 5
#define CP32_FILE_IS_DIR 6
#define CP32_FILE_NAME 7
struct cp32_minix_file {
  cp32_disk_reader read;
  unsigned size, position, zone_bytes, zones[7];
};
/* Root-component lookup only, optionally prefixed by one slash. */
CP32_IRAM_EXT int cp32_minix_file_open(cp32_disk_reader read, unsigned capacity,
                                      const char *name, struct cp32_minix_file *file);
/* Returns up to 64 bytes per call, zero at EOF, or negative error. */
CP32_IRAM_EXT int cp32_minix_file_read(struct cp32_minix_file *file,
                                      char *buffer, unsigned count);
#endif
