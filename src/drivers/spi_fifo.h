#ifndef CP32_SPI_FIFO_H
#define CP32_SPI_FIFO_H
#include "sd_spi.h"
/* Register access is supplied by the platform. Single owner, no DMA or IRQ.
 * ticks is a free-running wrapping counter; timeout is less than 2^31 ticks. */
struct cp32_spi_fifo {
  uint32_t (*read)(unsigned);
  void (*write)(unsigned,uint32_t);
  uint32_t (*ticks)(void);
  uint32_t timeout;
  int failed;
};
CP32_IRAM_EXT void cp32_spi_fifo_init(struct cp32_spi_fifo *);
/* NULL tx sends FF; NULL rx discards input. CS belongs to the caller. */
CP32_IRAM_EXT int cp32_spi_fifo_transfer(struct cp32_spi_fifo *,const uint8_t *,uint8_t *,unsigned);
#endif
