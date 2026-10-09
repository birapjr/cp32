#include "sdcard.h"
#include <esp32s3/sd_gpio.h>
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
CP32_IRAM_EXT int cp32_sdcard_init(void)
{
  static const struct cp32_sd_bus bus={transfer,select_card,delay_ms};
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
  return cp32_sd_init(&card,&bus);
}
CP32_IRAM_EXT uint64_t cp32_sdcard_sectors(void) {return card.sectors;}
CP32_IRAM_EXT int cp32_sdcard_read(uint32_t sector,unsigned char *out)
{return cp32_sd_read(&card,sector,out);}
