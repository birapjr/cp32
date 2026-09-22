/* MINIX read.c:rw_chunk/read_map responsibilities. Direct-zone reads,
 * sparse-hole zero filling and EOF; stage data to preserve buffers on failure. */
#include "fs.h"

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
