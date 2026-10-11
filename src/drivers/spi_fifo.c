#include "spi_fifo.h"
#include <esp32s3/sd_spi2.h>
CP32_IRAM_EXT static int wait_idle(struct cp32_spi_fifo *s,uint32_t mask)
{
  uint32_t start=s->ticks();
  while(s->read(SD_SPI2_CMD)&mask) {
    if((uint32_t)(s->ticks()-start)>=s->timeout) {
      s->failed=1;return CP32_SD_TIMEOUT;
    }
  }
  return 0;
}
CP32_IRAM_EXT void cp32_spi_fifo_init(struct cp32_spi_fifo *s)
{
  s->failed=0;
  s->write(SD_SPI2_GATE,3); /* XTAL, master clock active, clock enabled */
  s->write(SD_SPI2_SLAVE,0);
  s->write(SD_SPI2_INT_ENA,0);
  s->write(SD_SPI2_CTRL,1U<<19); /* MSB first, idle MOSI high */
  s->write(SD_SPI2_USER,(1U<<28)|(1U<<27)|1U); /* mode 0, full duplex */
  s->write(SD_SPI2_USER1,0);s->write(SD_SPI2_USER2,0);
  s->write(SD_SPI2_MISC,0x3f); /* all hardware CS disabled; clock idle low */
  s->write(SD_SPI2_DMA,SD_SPI2_DMA_IDLE);
  s->write(SD_SPI2_CLOCK,SD_SPI2_SLOW);
}
CP32_IRAM_EXT int cp32_spi_fifo_transfer(struct cp32_spi_fifo *s,
  const uint8_t *tx,uint8_t *rx,unsigned length)
{
  unsigned n,i,j;uint32_t word;
  if(s->failed)return CP32_SD_IO;
  while(length) {
    n=length>64 ? 64:length;
    if(wait_idle(s,SD_SPI2_USR|SD_SPI2_UPDATE))return CP32_SD_TIMEOUT;
    s->write(SD_SPI2_DMA,SD_SPI2_DMA_IDLE|SD_SPI2_FIFO_RESET);
    s->write(SD_SPI2_DMA,SD_SPI2_DMA_IDLE);
    for(i=0;i<n;i+=4) {
      word=0xffffffffU;
      for(j=0;j<4 && i+j<n;j++)if(tx)
        word=(word&~(255U<<(8*j)))|((uint32_t)tx[i+j]<<(8*j));
      s->write(SD_SPI2_W0+i,word);
    }
    s->write(SD_SPI2_DLEN,n*8-1);
    s->write(SD_SPI2_CMD,SD_SPI2_UPDATE);
    if(wait_idle(s,SD_SPI2_UPDATE))return CP32_SD_TIMEOUT;
    s->write(SD_SPI2_CMD,SD_SPI2_USR);
    if(wait_idle(s,SD_SPI2_USR))return CP32_SD_TIMEOUT;
    if(rx)for(i=0;i<n;i+=4) {
      word=s->read(SD_SPI2_W0+i);
      for(j=0;j<4 && i+j<n;j++)rx[i+j]=(uint8_t)(word>>(8*j));
    }
    if(tx)tx+=n;if(rx)rx+=n;length-=n;
  }
  return 0;
}
