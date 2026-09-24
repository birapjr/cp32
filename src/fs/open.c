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

/* MINIX open.c:do_lseek responsibility, on caller-owned read-only handles.
 * Keep the target's signed 32-bit offset range even on a 64-bit host. Seeking
 * beyond EOF is valid; negative positions and overflow never change state. */
CP32_IRAM_EXT int cp32_minix_file_seek(struct cp32_minix_file *file,
                                      long offset, int whence)
{
  unsigned base, position;
  if (!file || !file->read) return -CP32_SUPER_INVALID;
  switch (whence) {
    case 0: base = 0; break;
    case 1: base = file->position; break;
    case 2: base = file->size; break;
    default: return -CP32_SUPER_INVALID;
  }
  if (base > 0x7fffffffU || offset > 0x7fffffffL ||
      offset < (-0x7fffffffL - 1)) return -CP32_SUPER_INVALID;
  if (offset < 0) {
    unsigned magnitude = (unsigned)(-(offset + 1)) + 1U;
    if (magnitude > base) return -CP32_SUPER_INVALID;
    position = base - magnitude;
  } else {
    if ((unsigned long)offset > 0x7fffffffU - base) return -CP32_SUPER_INVALID;
    position = base + (unsigned)offset;
  }
  file->position = position;
  return 0;
}
