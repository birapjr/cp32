#include "ramdisk.h"
#include <string.h>

static unsigned char ramdisk[CP32_RAMDISK_SECTORS * CP32_RAMDISK_SECTOR_SIZE];
#define RAMDISK_BYTES (CP32_RAMDISK_SECTORS * CP32_RAMDISK_SECTOR_SIZE)
#define RAMDISK_MAGIC 0x43503332u
#define RAMDISK_VERSION 1u

struct ramdisk_header {
  unsigned magic;
  unsigned version;
  unsigned sector_size;
  unsigned sector_count;
  unsigned checksum;
};

CP32_IRAM_EXT static unsigned header_checksum(const struct ramdisk_header *header)
{
  return header->magic ^ header->version ^ header->sector_size ^
         header->sector_count ^ 0xA5A5A5A5u;
}

CP32_IRAM_EXT static int valid_sector(unsigned sector)
{
  return sector < CP32_RAMDISK_SECTORS;
}

int cp32_ramdisk_read(unsigned sector, void *buffer)
{
  if (!valid_sector(sector) || buffer == (void *)0) return -1;
  memcpy(buffer, &ramdisk[sector * CP32_RAMDISK_SECTOR_SIZE],
         CP32_RAMDISK_SECTOR_SIZE);
  return 0;
}

int cp32_ramdisk_write(unsigned sector, const void *buffer)
{
  if (!valid_sector(sector) || buffer == (const void *)0) return -1;
  memcpy(&ramdisk[sector * CP32_RAMDISK_SECTOR_SIZE], buffer,
         CP32_RAMDISK_SECTOR_SIZE);
  return 0;
}

void cp32_ramdisk_reset(void)
{
  unsigned i;
  for (i = 0; i < RAMDISK_BYTES; ++i) ramdisk[i] = 0;
}

unsigned cp32_ramdisk_sector_size(void)
{
  return CP32_RAMDISK_SECTOR_SIZE;
}

unsigned cp32_ramdisk_sector_count(void)
{
  return CP32_RAMDISK_SECTORS;
}

unsigned cp32_ramdisk_capacity(void)
{
  return CP32_RAMDISK_SECTORS * CP32_RAMDISK_SECTOR_SIZE;
}

CP32_IRAM_EXT static int valid_range(unsigned offset, unsigned length)
{
  return offset <= RAMDISK_BYTES && length <= RAMDISK_BYTES - offset;
}

int cp32_ramdisk_read_bytes(unsigned offset, void *buffer, unsigned length)
{
  if (!valid_range(offset, length) || (length != 0 && buffer == (void *)0))
    return -1;
  if (length != 0) memcpy(buffer, &ramdisk[offset], length);
  return 0;
}

int cp32_ramdisk_write_bytes(unsigned offset, const void *buffer, unsigned length)
{
  if (!valid_range(offset, length) || (length != 0 && buffer == (const void *)0))
    return -1;
  if (length != 0) memcpy(&ramdisk[offset], buffer, length);
  return 0;
}

int cp32_ramdisk_checksum(unsigned offset, unsigned length, unsigned *checksum)
{
  unsigned i;
  unsigned value = 2166136261u;
  if (!valid_range(offset, length) || checksum == (unsigned *)0) return -1;
  for (i = 0; i < length; ++i) {
    value ^= ramdisk[offset + i];
    value *= 16777619u;
  }
  *checksum = value;
  return 0;
}

int cp32_ramdisk_format(void)
{
  struct ramdisk_header header;
  cp32_ramdisk_reset();
  header.magic = RAMDISK_MAGIC;
  header.version = RAMDISK_VERSION;
  header.sector_size = CP32_RAMDISK_SECTOR_SIZE;
  header.sector_count = CP32_RAMDISK_SECTORS;
  header.checksum = header_checksum(&header);
  return cp32_ramdisk_write_bytes(0, &header, sizeof(header));
}

int cp32_ramdisk_is_formatted(void)
{
  struct ramdisk_header header;
  if (cp32_ramdisk_read_bytes(0, &header, sizeof(header)) != 0) return 0;
  return header.magic == RAMDISK_MAGIC &&
         header.version == RAMDISK_VERSION &&
         header.sector_size == CP32_RAMDISK_SECTOR_SIZE &&
         header.sector_count == CP32_RAMDISK_SECTORS &&
         header.checksum == header_checksum(&header);
}
