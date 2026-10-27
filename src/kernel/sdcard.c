#include "sdcard.h"
#include <esp32s3/sd_gpio.h>
#ifndef CP32_SD_SOFT
#include <esp32s3/sd_spi2.h>
#include "../drivers/spi_fifo.h"
#endif
static struct cp32_sd card;
CP32_IRAM_EXT static uint32_t cycles(void)
{ uint32_t n;__asm__ volatile("rsr.ccount %0":"=a"(n));return n; }
CP32_IRAM_EXT static void delay_cycles(uint32_t n)
{ uint32_t start=cycles();while((uint32_t)(cycles()-start)<n){} }
CP32_IRAM_EXT static void delay_ms(unsigned ms)
{ while(ms--)delay_cycles(240000); }
CP32_IRAM_EXT static void output(unsigned pin,unsigned value)
{
  unsigned offset=pin<32 ? (value?SD_GPIO_OUT_SET:SD_GPIO_OUT_CLEAR):
                          (value?SD_GPIO_OUT1_SET:SD_GPIO_OUT1_CLEAR);
  SD_GPIO_REG(offset)=1U<<(pin&31);
}
CP32_IRAM_EXT static void select_card(int selected)
{ output(SD_GPIO_CS,!selected); }
#ifdef CP32_SD_SOFT
CP32_IRAM_EXT static uint8_t transfer(uint8_t out)
{
  unsigned i;uint8_t in=0;
  /* Mode 0, at least 2us per half-period at the maximum 240MHz CPU clock.
   * Interrupts remain enabled. No other task owns these SD pins. */
  for(i=0;i<8;i++) {
    output(SD_GPIO_MOSI,(out&0x80)!=0);out<<=1;delay_cycles(480);
    output(SD_GPIO_CLK,1);delay_cycles(480);
    in=(uint8_t)((in<<1)|((SD_GPIO_REG(SD_GPIO_IN1)>>(SD_GPIO_MISO-32))&1));
    output(SD_GPIO_CLK,0);
  }
  return in;
}
#else
CP32_IRAM_EXT static uint32_t spi_read(unsigned offset)
{ return *(volatile uint32_t *)(SD_SPI2_BASE+offset); }
CP32_IRAM_EXT static void spi_write(unsigned offset,uint32_t value)
{ *(volatile uint32_t *)(SD_SPI2_BASE+offset)=value; }
static struct cp32_spi_fifo controller={spi_read,spi_write,cycles,2400000,0};
CP32_IRAM_EXT static uint8_t transfer(uint8_t out)
{
  uint8_t in=0xff;
  if(cp32_spi_fifo_transfer(&controller,&out,&in,1))return 0xff;
  return in;
}
CP32_IRAM_EXT static int receive_bytes(uint8_t *in,unsigned n)
{ return cp32_spi_fifo_transfer(&controller,0,in,n); }
CP32_IRAM_EXT static int transmit_bytes(const uint8_t *out,unsigned n)
{ return cp32_spi_fifo_transfer(&controller,out,0,n); }
CP32_IRAM_EXT static int transport_error(void) {return controller.failed;}
#endif
CP32_IRAM_EXT int cp32_sdcard_init(void)
{
#ifdef CP32_SD_SOFT
  static const struct cp32_sd_bus bus={transfer,select_card,delay_ms,0,0,0};
#else
  static const struct cp32_sd_bus bus={transfer,select_card,delay_ms,receive_bytes,transport_error,transmit_bytes};
  int result;
#endif
  static const unsigned pins[]={SD_GPIO_CS,SD_GPIO_MOSI,SD_GPIO_CLK,SD_GPIO_MISO};
  unsigned i,p;
  output(SD_GPIO_CS,1);output(SD_GPIO_MOSI,1);output(SD_GPIO_CLK,0);
  for(i=0;i<4;i++) {
    p=pins[i];
    SD_GPIO_MUX(p)=(SD_GPIO_MUX(p)&~((7U<<12)|(1U<<7)))|(1U<<12)|(1U<<9)|(1U<<8);
    SD_GPIO_FUNC(p)=256U|(1U<<10);
  }
  SD_GPIO_REG(SD_GPIO_ENABLE1_CLEAR)=1U<<(SD_GPIO_MISO-32);
  SD_GPIO_REG(SD_GPIO_ENABLE_SET)=(1U<<SD_GPIO_CS)|(1U<<SD_GPIO_MOSI);
  SD_GPIO_REG(SD_GPIO_ENABLE1_SET)=1U<<(SD_GPIO_CLK-32);
#ifdef CP32_SD_SOFT
  return cp32_sd_init(&card,&bus);
#else
  SD_SPI2_SYS_CLK|=SD_SPI2_SYS_BIT;
  SD_SPI2_SYS_RESET|=SD_SPI2_SYS_BIT;
  SD_SPI2_SYS_RESET&=~SD_SPI2_SYS_BIT;
  cp32_spi_fifo_init(&controller);
  SD_GPIO_FUNC(SD_GPIO_MOSI)=SD_SPI2_MOSI_SIGNAL|(1U<<10);
  SD_GPIO_FUNC(SD_GPIO_CLK)=SD_SPI2_CLK_SIGNAL|(1U<<10);
  SD_GPIO_INPUT(SD_SPI2_MISO_SIGNAL)=SD_GPIO_MISO|(1U<<7);
  result=cp32_sd_init(&card,&bus);
  /* All identification/CSD traffic remains <=400kHz. Next transfer applies
   * the 4MHz divider via UPDATE, after CS has been released. */
  if(!result)spi_write(SD_SPI2_CLOCK,SD_SPI2_FAST);
  return result;
#endif
}
CP32_IRAM_EXT uint64_t cp32_sdcard_sectors(void) {return card.sectors;}
CP32_IRAM_EXT int cp32_sdcard_read(uint32_t sector,unsigned char *out)
{return cp32_sd_read(&card,sector,out);}

CP32_IRAM_EXT int cp32_sdcard_write(uint32_t sector,const unsigned char *out)
{return cp32_sd_write(&card,sector,out);}

CP32_IRAM_EXT int cp32_sdcard_sync(void) {return cp32_sd_sync(&card);}
