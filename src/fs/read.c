/* MINIX read.c:rw_chunk/read_map responsibilities. Direct, single- and double-indirect reads,
 * sparse-hole zero filling and EOF; stage data to preserve buffers on failure. */
#include "fs.h"

/* MINIX V2 indirect entries occupy one 1024-byte block, even when a
 * zone spans several blocks. Decode four bytes explicitly for Xtensa and
 * opposite-endian images; never allocate a full block on the task stack. */
CP32_IRAM_EXT static int cp32_read_indirect(struct cp32_minix_file *file,
    unsigned table, unsigned index, unsigned parent, unsigned *zone)
{
  unsigned char raw[4];
  unsigned mapped, i;
  int result;
  if (!table) { *zone = 0; return 0; }
  if (file->read(table * file->zone_bytes + index * 4,
                 (char *)raw, sizeof(raw)) != sizeof(raw)) return -CP32_SUPER_IO;
  mapped = cp32_fs_u32(raw, file->super.swapped);
  if (mapped) {
    if (mapped < file->super.first_data_zone || mapped >= file->super.zones ||
        mapped == table || mapped == parent || mapped == file->indirect ||
        mapped == file->double_indirect) return -CP32_SUPER_INVALID;
    for (i = 0; i < 7; i++)
      if (mapped == file->zones[i]) return -CP32_SUPER_INVALID;
    result = cp32_fs_allocated(file->read, (2 + file->super.imap_blocks) * 1024,
               mapped - file->super.first_data_zone + 1, file->super.swapped);
    if (result) return result;
  }
  *zone = mapped;
  return 0;
}

CP32_IRAM_EXT static int cp32_read_map(struct cp32_minix_file *file,
                                      unsigned index, unsigned *zone)
{
  unsigned table, excess;
  int result;
  if (index < 7) { *zone = file->zones[index]; return 0; }
  if (index < 263)
    return cp32_read_indirect(file, file->indirect, index - 7, 0, zone);
  excess = index - 263;
  if (excess >= 256*256) return -CP32_SUPER_UNSUPPORTED;
  result = cp32_read_indirect(file, file->double_indirect, excess / 256, 0, &table);
  if (result) return result;
  return cp32_read_indirect(file, table, excess % 256, file->double_indirect, zone);
}

CP32_IRAM_EXT int cp32_minix_file_read(struct cp32_minix_file *file,
                                      char *buffer, unsigned count)
{
  char bytes[64];
  unsigned chunk, within, zone, i;
  int result;
  if (!file || !file->read || (count && !buffer)) return -CP32_SUPER_INVALID;
  if (!count || file->position >= file->size) return 0;
  chunk = file->size - file->position;
  if (chunk > count) chunk = count;
  if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
  within = file->position % file->zone_bytes;
  if (chunk > file->zone_bytes - within) chunk = file->zone_bytes - within;
  result = cp32_read_map(file, file->position / file->zone_bytes, &zone);
  if (result) return result;
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
