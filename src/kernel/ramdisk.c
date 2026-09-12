#include "ramdisk.h"
#include <string.h>

static unsigned char ramdisk[CP32_RAMDISK_SECTORS * CP32_RAMDISK_SECTOR_SIZE];
#define RAMDISK_BYTES (CP32_RAMDISK_SECTORS * CP32_RAMDISK_SECTOR_SIZE)

static int valid_sector(unsigned sector)
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

static int valid_range(unsigned offset, unsigned length)
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
