#ifndef CP32_SD_SPI_H
#define CP32_SD_SPI_H
#include <stdint.h>
#ifndef CP32_IRAM_EXT
#ifdef __XTENSA__
#define CP32_IRAM_EXT __attribute__((section(".iram_ext.text")))
#else
#define CP32_IRAM_EXT
#endif
#endif
/* Single owner, SPI mode 0, MSB first, <=400kHz. All waits are bounded.
 * select(1) asserts CS. delay_ms must wait at least the requested interval.
 * No card writes. Call only from boot or the serialized storage task. */
struct cp32_sd_bus {
  uint8_t (*transfer)(uint8_t);
  void (*select)(int);
  void (*delay_ms)(unsigned);
};
struct cp32_sd {
  const struct cp32_sd_bus *bus;
  uint64_t sectors;
  unsigned ready, block_addressed;
};
#define CP32_SD_IO -1
#define CP32_SD_TIMEOUT -2
#define CP32_SD_UNSUPPORTED -3
#define CP32_SD_CRC -4
CP32_IRAM_EXT int cp32_sd_init(struct cp32_sd *, const struct cp32_sd_bus *);
/* Failure may modify buffer; caller publishes it only on success. */
CP32_IRAM_EXT int cp32_sd_read(struct cp32_sd *,uint32_t lba,unsigned char buffer[512]);
#endif
