#ifndef CP32_SD_SPI2_REGS_H
#define CP32_SD_SPI2_REGS_H
/* ESP32-S3 TRM GPSPI and Espressif v5.4.2 spi_reg.h, system_reg.h,
 * gpio_sig_map.h. SPI2 only; SPI0/1 flash and SPI3 are untouched. */
#define SD_SPI2_BASE 0x60024000UL
#define SD_SPI2_CMD 0x00
#define SD_SPI2_CTRL 0x08
#define SD_SPI2_CLOCK 0x0c
#define SD_SPI2_USER 0x10
#define SD_SPI2_USER1 0x14
#define SD_SPI2_USER2 0x18
#define SD_SPI2_DLEN 0x1c
#define SD_SPI2_MISC 0x20
#define SD_SPI2_DMA 0x30
#define SD_SPI2_INT_ENA 0x34
#define SD_SPI2_W0 0x98
#define SD_SPI2_SLAVE 0xe0
#define SD_SPI2_GATE 0xe8
#define SD_SPI2_UPDATE (1U<<23)
#define SD_SPI2_USR (1U<<24)
/* 40MHz XTAL / (pre+1) / (N+1); H+1 is half of N+1. */
#define SD_SPI2_SLOW ((4U<<18)|(19U<<12)|(9U<<6)|19U)
#define SD_SPI2_FAST ((9U<<12)|(4U<<6)|9U)
#define SD_SPI2_DMA_IDLE ((1U<<20)|(1U<<19))
#define SD_SPI2_FIFO_RESET ((1U<<30)|(1U<<29))
#define SD_SPI2_SYS_CLK (*(volatile uint32_t *)0x600c0018UL)
#define SD_SPI2_SYS_RESET (*(volatile uint32_t *)0x600c0020UL)
#define SD_SPI2_SYS_BIT (1U<<6)
#define SD_SPI2_CLK_SIGNAL 101U
#define SD_SPI2_MISO_SIGNAL 102U
#define SD_SPI2_MOSI_SIGNAL 103U
#endif
