/* MINIX inode.c:new_icopy/get_inode responsibilities. Decode and validate
 * directory or regular-file inode state. No inode cache or reference table yet. */
#include "fs.h"

CP32_IRAM_EXT int cp32_fs_open_directory(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_dir *dir)
{
  struct cp32_minix_dir next;
  unsigned char raw[64];
  unsigned i, used, zone_bytes, swap;
  int result;
  if (!dir) return -CP32_SUPER_INVALID;
  next.super = *super;
  if (!number || number > super->ninodes) return -CP32_SUPER_INVALID;
  result = cp32_fs_allocated(read, 2048, number, next.super.swapped);
  if (result) return result;
  if (read((2 + next.super.imap_blocks + next.super.zmap_blocks) * 1024 + (number-1)*64,
           (char *)raw, sizeof(raw)) != sizeof(raw)) return -CP32_SUPER_IO;
  swap = next.super.swapped;
  if ((cp32_fs_u16(raw, swap) & 0170000) != 0040000)
    return number == 1 ? -CP32_SUPER_INVALID : -CP32_FILE_NOT_DIR;
  if (!cp32_fs_u16(raw + 2, swap)) return -CP32_SUPER_INVALID;
  next.size = cp32_fs_u32(raw + 8, swap);
  zone_bytes = 1024U << next.super.log_zone_size;
  if (next.size < 32 || next.size % 16 || next.size > next.super.max_size)
    return -CP32_SUPER_INVALID;
  if (next.size > 7 * zone_bytes) return -CP32_SUPER_UNSUPPORTED;
  used = (next.size + zone_bytes - 1) / zone_bytes;
  for (i = 0; i < 7; i++) {
    next.zones[i] = cp32_fs_u32(raw + 24 + i * 4, swap);
    if (i < used) {
      unsigned j;
      if (next.zones[i] < next.super.first_data_zone ||
          next.zones[i] >= next.super.zones) return -CP32_SUPER_INVALID;
      for (j = 0; j < i; j++)
        if (next.zones[j] == next.zones[i]) return -CP32_SUPER_INVALID;
      result = cp32_fs_allocated(read, (2 + next.super.imap_blocks) * 1024,
                         next.zones[i] - next.super.first_data_zone + 1, swap);
      if (result) return result;
    }
  }
  next.position = 0;
  next.read = read;
  *dir = next;
  return 0;
}

CP32_IRAM_EXT int cp32_fs_file_inode(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_file *file)
{
  struct cp32_minix_file next;
  unsigned char raw[64];
  unsigned i, used, swap, mode;
  int result;
  if (!file || !number || number > super->ninodes) return -CP32_SUPER_INVALID;
  result = cp32_fs_allocated(read, 2048, number, super->swapped);
  if (result) return result;
  if (read((2 + super->imap_blocks + super->zmap_blocks) * 1024 +
           (number - 1) * 64, (char *)raw, sizeof(raw)) != sizeof(raw))
    return -CP32_SUPER_IO;
  swap = super->swapped;
  mode = cp32_fs_u16(raw, swap) & 0170000;
  if (mode == 0040000) return -CP32_FILE_IS_DIR;
  if (mode != 0100000) return -CP32_SUPER_UNSUPPORTED;
  if (!cp32_fs_u16(raw + 2, swap)) return -CP32_SUPER_INVALID;
  next.size = cp32_fs_u32(raw + 8, swap);
  next.zone_bytes = 1024U << super->log_zone_size;
  if (next.size > super->max_size) return -CP32_SUPER_INVALID;
  if (next.size > 7 * next.zone_bytes) return -CP32_SUPER_UNSUPPORTED;
  used = (next.size + next.zone_bytes - 1) / next.zone_bytes;
  for (i = 0; i < 7; i++) {
    unsigned j;
    next.zones[i] = cp32_fs_u32(raw + 24 + i * 4, swap);
    if (i >= used || !next.zones[i]) continue; /* MINIX sparse-file hole */
    if (next.zones[i] < super->first_data_zone || next.zones[i] >= super->zones)
      return -CP32_SUPER_INVALID;
    for (j = 0; j < i; j++)
      if (next.zones[j] == next.zones[i]) return -CP32_SUPER_INVALID;
    result = cp32_fs_allocated(read, (2 + super->imap_blocks) * 1024,
                       next.zones[i] - super->first_data_zone + 1, swap);
    if (result) return result;
  }
  next.position = 0;
  next.read = read;
  *file = next;
  return 0;
}
