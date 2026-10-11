#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "spi_fifo.h"
#include <esp32s3/sd_spi2.h>
static uint32_t regs[64],now,hang;
static unsigned starts,updates,pending,position;
static int configured,echo;
static uint32_t ticks(void) {now+=10;return now;}
static uint32_t read_reg(unsigned r)
{
  assert(r<sizeof(regs) && !(r&3));
  if(r==SD_SPI2_CMD && regs[r/4] && !(regs[r/4]&hang)) {
    if(pending--)return regs[r/4];
    if(regs[r/4]==SD_SPI2_UPDATE)configured=1;
    if(regs[r/4]==SD_SPI2_USR) {
      unsigned n=(regs[SD_SPI2_DLEN/4]+1)/8;
      assert(n && n<=64);
      for(unsigned i=0;i<n;i++) {
        unsigned at=SD_SPI2_W0/4+i/4,shift=(i%4)*8;
        unsigned value=(regs[at]>>shift)&255;
        if(echo)value^=0xa5;
        else {assert(value==255);value=(position++*17)&255;}
        regs[at]=(regs[at]&~(255U<<shift))|(value<<shift);
      }
    }
    regs[r/4]=0;
  }
  return regs[r/4];
}
static void write_reg(unsigned r,uint32_t v)
{
  assert(r<sizeof(regs) && !(r&3));
  if(r==SD_SPI2_CMD) {
    assert(!regs[r/4]);pending=2;
    if(v==SD_SPI2_UPDATE){updates++;configured=0;}
    else {assert(v==SD_SPI2_USR && configured);starts++;configured=0;}
  }
  regs[r/4]=v;
}
static struct cp32_spi_fifo spi={read_reg,write_reg,ticks,100,0};
static void reset(void)
{
  memset(regs,0,sizeof(regs));starts=updates=pending=position=0;
  now=0xffffffe0U;hang=0;configured=0;echo=0;
  cp32_spi_fifo_init(&spi);
  assert(regs[SD_SPI2_GATE/4]==3 && regs[SD_SPI2_CLOCK/4]==SD_SPI2_SLOW);
  assert(regs[SD_SPI2_USER/4]==((1U<<28)|(1U<<27)|1));
  assert(regs[SD_SPI2_MISC/4]==63 && !regs[SD_SPI2_INT_ENA/4]);
}
int main(void)
{
  unsigned sizes[]={1,3,4,63,64,65,512};uint8_t tx[514],rx[514];
  for(unsigned k=0;k<sizeof(sizes)/sizeof(*sizes);k++) {
    unsigned n=sizes[k];reset();memset(rx,0xcc,sizeof(rx));
    assert(!cp32_spi_fifo_transfer(&spi,0,rx+1,n));
    assert(starts==(n+63)/64 && updates==starts);
    for(unsigned i=0;i<n;i++)assert(rx[i+1]==(uint8_t)(i*17));
    assert(rx[0]==0xcc && rx[n+1]==0xcc);
    reset();echo=1;for(unsigned i=0;i<n;i++)tx[i+1]=(uint8_t)(i*3);
    assert(!cp32_spi_fifo_transfer(&spi,tx+1,rx+1,n));
    for(unsigned i=0;i<n;i++)assert(rx[i+1]==(uint8_t)(tx[i+1]^0xa5));
  }
  for(unsigned mode=0;mode<2;mode++) {
    reset();hang=mode ? SD_SPI2_USR:SD_SPI2_UPDATE;
    memset(rx,0xcc,sizeof(rx));
    assert(cp32_spi_fifo_transfer(&spi,0,rx,64)==CP32_SD_TIMEOUT && spi.failed);
    assert(rx[0]==0xcc);unsigned before=starts;
    assert(cp32_spi_fifo_transfer(&spi,0,rx,1)==CP32_SD_IO && starts==before);
  }
  reset();assert(!cp32_spi_fifo_transfer(&spi,0,0,1));
  assert(!cp32_spi_fifo_transfer(&spi,0,0,0) && starts==1);
  puts("SPI2 FIFO: byte order, unaligned/tail/512-byte bursts, UPDATE ordering, wrap and stuck-controller timeouts pass");
}
