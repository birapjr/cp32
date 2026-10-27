#ifndef CP32_SDCARD_H
#define CP32_SDCARD_H
#include "../drivers/sd_spi.h"
CP32_IRAM_EXT int cp32_sdcard_init(void);
CP32_IRAM_EXT uint64_t cp32_sdcard_sectors(void);
CP32_IRAM_EXT int cp32_sdcard_read(uint32_t,unsigned char *);
CP32_IRAM_EXT int cp32_sdcard_write(uint32_t,const unsigned char *);
CP32_IRAM_EXT int cp32_sdcard_sync(void);
#endif
