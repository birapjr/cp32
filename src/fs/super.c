/* MINIX super.c: read_super and allocation-map access.
 * CP32 currently recognizes V2 and reads allocation bits; no allocator yet. */
#include "fs.h"

CP32_IRAM_EXT int cp32_minix_super_read(cp32_disk_reader read,
    unsigned capacity, struct cp32_minix_super *out)
{
  unsigned char bytes[24];
  struct cp32_minix_super s;
  unsigned magic, metadata_blocks, zone_blocks;
  if (!read || !out || capacity < 2048) return CP32_SUPER_INVALID;
  if (read(1024, (char *)bytes, sizeof(bytes)) != sizeof(bytes))
    return CP32_SUPER_IO;
  magic = cp32_fs_u16(bytes + 16, 0);
  if (magic == 0x137f || magic == 0x7f13)
    return CP32_SUPER_UNSUPPORTED;
  if (magic != 0x2468 && magic != 0x6824) return CP32_SUPER_ABSENT;
  s.swapped = magic == 0x6824;
  s.ninodes = cp32_fs_u16(bytes, s.swapped);
  s.imap_blocks = cp32_fs_u16(bytes + 4, s.swapped);
  s.zmap_blocks = cp32_fs_u16(bytes + 6, s.swapped);
  s.first_data_zone = cp32_fs_u16(bytes + 8, s.swapped);
  s.log_zone_size = cp32_fs_u16(bytes + 10, s.swapped);
  s.max_size = cp32_fs_u32(bytes + 12, s.swapped);
  s.zones = cp32_fs_u32(bytes + 20, s.swapped);
  if (!s.ninodes || !s.imap_blocks || !s.zmap_blocks ||
      s.imap_blocks > 32767 || s.zmap_blocks > 32767 ||
      s.log_zone_size > 4 || !s.max_size || s.max_size > 0x7fffffffU)
    return CP32_SUPER_INVALID;
  zone_blocks = 1U << s.log_zone_size;
  /* Division avoids overflow even for hostile 32-bit zone counts. */
  if (s.zones > capacity / 1024 / zone_blocks ||
      s.first_data_zone >= s.zones) return CP32_SUPER_INVALID;
  metadata_blocks = 2 + s.imap_blocks + s.zmap_blocks +
                    (s.ninodes + 15) / 16; /* V2 disk inode = 64 bytes */
  if (metadata_blocks > s.first_data_zone * zone_blocks ||
      s.ninodes + 1 > s.imap_blocks * 8192 ||
      s.zones - s.first_data_zone + 1 > s.zmap_blocks * 8192)
    return CP32_SUPER_INVALID;
  *out = s;
  return CP32_SUPER_OK;
}

CP32_IRAM_EXT int cp32_fs_allocated(cp32_disk_reader read, unsigned map,
                                   unsigned bit, unsigned swap)
{
  unsigned char word[2];
  /* MINIX allocation maps are arrays of endian-converted 16-bit chunks. */
  if (read(map + (bit / 16) * 2, (char *)word, 2) != 2) return -CP32_SUPER_IO;
  return (cp32_fs_u16(word, swap) & (1U << (bit % 16))) ? 0 : -CP32_SUPER_INVALID;
}
