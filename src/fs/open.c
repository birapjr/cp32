/* MINIX open.c: pathname-to-open-state orchestration.
 * These are read-only helper handles, not the complete MINIX open syscall. */
#include "fs.h"

CP32_IRAM_EXT int cp32_minix_root_open(cp32_disk_reader read, unsigned capacity,
                                      struct cp32_minix_dir *dir)
{
  struct cp32_minix_super super;
  int result = cp32_minix_super_read(read, capacity, &super);
  return result ? -result : cp32_fs_open_directory(read, &super, 1, dir);
}

CP32_IRAM_EXT int cp32_minix_dir_open(cp32_disk_reader read, unsigned capacity,
                                     const char *path, struct cp32_minix_dir *dir)
{
  struct cp32_minix_super super;
  unsigned number;
  int result = cp32_fs_resolve_path(read, capacity, path, &super, &number);
  return result ? result : cp32_fs_open_directory(read, &super, number, dir);
}

CP32_IRAM_EXT int cp32_minix_file_open(cp32_disk_reader read, unsigned capacity,
                                      const char *name, struct cp32_minix_file *file)
{
  struct cp32_minix_super super;
  unsigned number;
  int result;
  if (!name || !file) return -CP32_SUPER_INVALID;
  result = cp32_fs_resolve_path(read, capacity, name, &super, &number);
  return result ? result : cp32_fs_file_inode(read, &super, number, file);
}
