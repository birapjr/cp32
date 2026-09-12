#ifndef CP32_RAMDISK_H
#define CP32_RAMDISK_H

#define CP32_RAMDISK_SECTOR_SIZE 512
#define CP32_RAMDISK_SECTORS 128

int cp32_ramdisk_read(unsigned sector, void *buffer);
int cp32_ramdisk_write(unsigned sector, const void *buffer);
void cp32_ramdisk_reset(void);
unsigned cp32_ramdisk_sector_size(void);
unsigned cp32_ramdisk_sector_count(void);
unsigned cp32_ramdisk_capacity(void);
int cp32_ramdisk_read_bytes(unsigned offset, void *buffer, unsigned length);
int cp32_ramdisk_write_bytes(unsigned offset, const void *buffer, unsigned length);

#endif
