#ifndef CP32_RAMDISK_H
#define CP32_RAMDISK_H

#define CP32_RAMDISK_SECTOR_SIZE 512
#define CP32_RAMDISK_SECTORS 128
#ifndef CP32_IRAM_EXT
#if defined(__XTENSA__)
#define CP32_IRAM_EXT __attribute__((section(".iram_ext.text")))
#else
#define CP32_IRAM_EXT
#endif
#endif

CP32_IRAM_EXT int cp32_ramdisk_read(unsigned sector, void *buffer);
CP32_IRAM_EXT int cp32_ramdisk_write(unsigned sector, const void *buffer);
CP32_IRAM_EXT void cp32_ramdisk_reset(void);
CP32_IRAM_EXT unsigned cp32_ramdisk_sector_size(void);
CP32_IRAM_EXT unsigned cp32_ramdisk_sector_count(void);
CP32_IRAM_EXT unsigned cp32_ramdisk_capacity(void);
CP32_IRAM_EXT int cp32_ramdisk_read_bytes(unsigned offset, void *buffer, unsigned length);
CP32_IRAM_EXT int cp32_ramdisk_write_bytes(unsigned offset, const void *buffer, unsigned length);
CP32_IRAM_EXT int cp32_ramdisk_checksum(unsigned offset, unsigned length, unsigned *checksum);
CP32_IRAM_EXT int cp32_ramdisk_format(void);
CP32_IRAM_EXT int cp32_ramdisk_is_formatted(void);

#endif
