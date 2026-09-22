/* MINIX path.c:last_dir/advance/search_dir responsibilities.
 * Iterative directory traversal; relative paths start at root for now. */
#include "fs.h"

CP32_IRAM_EXT int cp32_fs_resolve_path(cp32_disk_reader read, unsigned capacity,
    const char *path, struct cp32_minix_super *super, unsigned *number)
{
  struct cp32_minix_dir dir;
  unsigned length, offset = 0, current = 1, next, start, count, i;
  char entry[15];
  int result;
  if (!path || !*path) return -CP32_FILE_NAME;
  for (length = 0; path[length]; length++)
    if (length == CP32_MINIX_PATH_MAX || path[length] < 0x20 || path[length] > 0x7e)
      return -CP32_FILE_NAME;
  result = cp32_minix_super_read(read, capacity, super);
  if (result) return -result;
  while (offset < length) {
    while (path[offset] == '/') offset++;
    if (offset == length) break;
    start = offset;
    while (path[offset] && path[offset] != '/') offset++;
    count = offset - start;
    if (count > 14) return -CP32_FILE_NAME;
    result = cp32_fs_open_directory(read, super, current, &dir);
    if (result) return result;
    while ((result = cp32_minix_root_next(&dir, &next, entry)) > 0) {
      for (i = 0; i < count && entry[i] == path[start+i]; i++) {}
      if (i == count && !entry[i]) break;
    }
    if (result <= 0) return result ? result : -CP32_FILE_NOT_FOUND;
    /* Stay at the filesystem root even if its on-disk dot-dot is corrupt. */
    if (!(current == 1 && count == 2 && path[start] == '.' && path[start+1] == '.'))
      current = next;
  }
  /* A trailing slash requires a directory, including paths made of slashes. */
  if (path[length-1] == '/') {
    result = cp32_fs_open_directory(read, super, current, &dir);
    if (result) return result;
  }
  *number = current;
  return 0;
}

CP32_IRAM_EXT int cp32_minix_root_next(struct cp32_minix_dir *dir,
                                      unsigned *inode, char name[15])
{
  unsigned char raw[16];
  unsigned zone_bytes, offset, number, i;
  int result;
  if (!dir || !dir->read || !inode || !name) return -CP32_SUPER_INVALID;
  zone_bytes = 1024U << dir->super.log_zone_size;
  while (dir->position < dir->size) {
    offset = dir->zones[dir->position / zone_bytes] * zone_bytes +
             dir->position % zone_bytes;
    if (dir->read(offset, (char *)raw, sizeof(raw)) != sizeof(raw))
      return -CP32_SUPER_IO;
    number = cp32_fs_u16(raw, dir->super.swapped);
    if (!number) { dir->position += 16; continue; } /* deleted entry */
    if (number > dir->super.ninodes || !raw[2]) return -CP32_SUPER_INVALID;
    result = cp32_fs_allocated(dir->read, 2048, number, dir->super.swapped);
    if (result) return result;
    for (i = 0; i < 14 && raw[2+i]; i++)
      if (raw[2+i] < 0x20 || raw[2+i] > 0x7e || raw[2+i] == '/')
        return -CP32_SUPER_UNSUPPORTED; /* console-safe names only for now */
    for (i = 0; i < 14; i++) name[i] = (char)raw[2+i];
    name[14] = 0;
    *inode = number;
    dir->position += 16;
    return 1;
  }
  return 0;
}
