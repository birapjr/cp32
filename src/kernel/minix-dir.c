#include "minix-dir.h"

/* MINIX inode.c:new_icopy, read.c:read_map, path.c:search_dir.
 * Fixed disk layouts are decoded bytewise, not cast to Xtensa structures.
 * Only direct zones are supported; larger directories fail explicitly. */
CP32_IRAM_EXT static unsigned dir16(const unsigned char *p, unsigned swap)
{
  return swap ? ((unsigned)p[0] << 8) | p[1]
              : ((unsigned)p[1] << 8) | p[0];
}

CP32_IRAM_EXT static unsigned dir32(const unsigned char *p, unsigned swap)
{
  return swap ? (dir16(p, swap) << 16) | dir16(p + 2, swap)
              : (dir16(p + 2, swap) << 16) | dir16(p, swap);
}

CP32_IRAM_EXT static int allocated(cp32_disk_reader read, unsigned map,
                                   unsigned bit, unsigned swap)
{
  unsigned char word[2];
  /* MINIX allocation maps are arrays of endian-converted 16-bit chunks. */
  if (read(map + (bit / 16) * 2, (char *)word, 2) != 2) return -CP32_SUPER_IO;
  return (dir16(word, swap) & (1U << (bit % 16))) ? 0 : -CP32_SUPER_INVALID;
}

CP32_IRAM_EXT int cp32_minix_root_open(cp32_disk_reader read, unsigned capacity,
                                      struct cp32_minix_dir *dir)
{
  struct cp32_minix_dir next;
  unsigned char raw[64];
  unsigned i, used, zone_bytes, swap;
  int result;
  if (!dir) return -CP32_SUPER_INVALID;
  result = cp32_minix_super_read(read, capacity, &next.super);
  if (result) return -result;
  result = allocated(read, 2048, 1, next.super.swapped);
  if (result) return result;
  if (read((2 + next.super.imap_blocks + next.super.zmap_blocks) * 1024,
           (char *)raw, sizeof(raw)) != sizeof(raw)) return -CP32_SUPER_IO;
  swap = next.super.swapped;
  if ((dir16(raw, swap) & 0170000) != 0040000 || !dir16(raw + 2, swap))
    return -CP32_SUPER_INVALID;
  next.size = dir32(raw + 8, swap);
  zone_bytes = 1024U << next.super.log_zone_size;
  if (next.size < 32 || next.size % 16 || next.size > next.super.max_size)
    return -CP32_SUPER_INVALID;
  if (next.size > 7 * zone_bytes) return -CP32_SUPER_UNSUPPORTED;
  used = (next.size + zone_bytes - 1) / zone_bytes;
  for (i = 0; i < 7; i++) {
    next.zones[i] = dir32(raw + 24 + i * 4, swap);
    if (i < used) {
      unsigned j;
      if (next.zones[i] < next.super.first_data_zone ||
          next.zones[i] >= next.super.zones) return -CP32_SUPER_INVALID;
      for (j = 0; j < i; j++)
        if (next.zones[j] == next.zones[i]) return -CP32_SUPER_INVALID;
      result = allocated(read, (2 + next.super.imap_blocks) * 1024,
                         next.zones[i] - next.super.first_data_zone + 1, swap);
      if (result) return result;
    }
  }
  next.position = 0;
  next.read = read;
  *dir = next;
  return 0;
}

/* MINIX path.c:search_dir and inode.c:new_icopy, limited to a root name.
 * A handle is published only after validating every direct zone used by EOF. */
CP32_IRAM_EXT int cp32_minix_file_open(cp32_disk_reader read, unsigned capacity,
                                      const char *name, struct cp32_minix_file *file)
{
  struct cp32_minix_dir dir;
  struct cp32_minix_file next;
  unsigned char raw[64];
  char entry[15];
  unsigned length, number = 0, i, used, swap, mode;
  int result;
  if (!name || !file) return -CP32_SUPER_INVALID;
  if (*name == '/') name++;
  for (length = 0; name[length]; length++) {
    if (length == 14 || name[length] < 0x20 || name[length] > 0x7e || name[length] == '/')
      return -CP32_FILE_NAME;
  }
  if (!length) return -CP32_FILE_NAME;
  result = cp32_minix_root_open(read, capacity, &dir);
  if (result) return result;
  while ((result = cp32_minix_root_next(&dir, &number, entry)) > 0) {
    for (i = 0; i < length && entry[i] == name[i]; i++) {}
    if (i == length && !entry[i]) break;
  }
  if (result <= 0) return result ? result : -CP32_FILE_NOT_FOUND;
  if (read((2 + dir.super.imap_blocks + dir.super.zmap_blocks) * 1024 +
           (number - 1) * 64, (char *)raw, sizeof(raw)) != sizeof(raw))
    return -CP32_SUPER_IO;
  swap = dir.super.swapped;
  mode = dir16(raw, swap) & 0170000;
  if (mode == 0040000) return -CP32_FILE_IS_DIR;
  if (mode != 0100000) return -CP32_SUPER_UNSUPPORTED;
  if (!dir16(raw + 2, swap)) return -CP32_SUPER_INVALID;
  next.size = dir32(raw + 8, swap);
  next.zone_bytes = 1024U << dir.super.log_zone_size;
  if (next.size > dir.super.max_size) return -CP32_SUPER_INVALID;
  if (next.size > 7 * next.zone_bytes) return -CP32_SUPER_UNSUPPORTED;
  used = (next.size + next.zone_bytes - 1) / next.zone_bytes;
  for (i = 0; i < 7; i++) {
    unsigned j;
    next.zones[i] = dir32(raw + 24 + i * 4, swap);
    if (i >= used || !next.zones[i]) continue; /* MINIX sparse-file hole */
    if (next.zones[i] < dir.super.first_data_zone || next.zones[i] >= dir.super.zones)
      return -CP32_SUPER_INVALID;
    for (j = 0; j < i; j++)
      if (next.zones[j] == next.zones[i]) return -CP32_SUPER_INVALID;
    result = allocated(read, (2 + dir.super.imap_blocks) * 1024,
                       next.zones[i] - dir.super.first_data_zone + 1, swap);
    if (result) return result;
  }
  next.position = 0;
  next.read = read;
  *file = next;
  return 0;
}

/* MINIX read.c:rw_chunk returns zeros for unallocated regular-file zones.
 * Stage each bounded read so short/error I/O changes neither caller data nor
 * offset. A successful short read at a zone boundary is normal. */
CP32_IRAM_EXT int cp32_minix_file_read(struct cp32_minix_file *file,
                                      char *buffer, unsigned count)
{
  char bytes[64];
  unsigned chunk, within, zone, i;
  if (!file || !file->read || (count && !buffer)) return -CP32_SUPER_INVALID;
  if (!count || file->position >= file->size) return 0;
  chunk = file->size - file->position;
  if (chunk > count) chunk = count;
  if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
  within = file->position % file->zone_bytes;
  if (chunk > file->zone_bytes - within) chunk = file->zone_bytes - within;
  zone = file->zones[file->position / file->zone_bytes];
  if (zone) {
    if (file->read(zone * file->zone_bytes + within, bytes, chunk) != (int)chunk)
      return -CP32_SUPER_IO;
  } else {
    for (i = 0; i < chunk; i++) bytes[i] = 0;
  }
  for (i = 0; i < chunk; i++) buffer[i] = bytes[i];
  file->position += chunk;
  return (int)chunk;
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
    number = dir16(raw, dir->super.swapped);
    if (!number) { dir->position += 16; continue; } /* deleted entry */
    if (number > dir->super.ninodes || !raw[2]) return -CP32_SUPER_INVALID;
    result = allocated(dir->read, 2048, number, dir->super.swapped);
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
