/* SD Physical Layer Simplified Specification, SPI chapter 7.
 * MINIX disk-driver boundary: fixed blocks with bounded device errors, not FS
 * pathname operations. Supports SD v1/v2 SDSC and block-addressed SDHC/SDXC. */
#include "sd_spi.h"
CP32_IRAM_EXT static void finish(struct cp32_sd *s)
{ s->bus->select(0); s->bus->transfer(0xff); }
CP32_IRAM_EXT static int command(struct cp32_sd *s,unsigned cmd,uint32_t arg)
{
  unsigned i,j;uint8_t packet[5],crc=0,r;
  finish(s);s->bus->select(1);
  for(i=0;i<4096;i++)if(s->bus->transfer(0xff)==0xff)break;
  if(i==4096)return CP32_SD_TIMEOUT;
  packet[0]=0x40|cmd;
  for(i=0;i<4;i++)packet[i+1]=(uint8_t)(arg>>(24-i*8));
  for(i=0;i<5;i++) {
    uint8_t b=packet[i];
    for(j=0;j<8;j++) {crc<<=1;if((b^crc)&0x80)crc^=9;b<<=1;}
    s->bus->transfer(packet[i]);
  }
  s->bus->transfer((uint8_t)((crc<<1)|1));
  for(i=0;i<16;i++) {r=s->bus->transfer(0xff);if(!(r&0x80))return r;}
  return CP32_SD_TIMEOUT;
}
CP32_IRAM_EXT static int data(struct cp32_sd *s,unsigned char *p,unsigned n)
{
  unsigned i,j;uint8_t r;uint16_t crc=0,wire;
  /* 65536 polling bytes at <=400kHz permit >1s, exceeding read timeout. */
  for(i=0;i<65536;i++) {r=s->bus->transfer(0xff);if(r!=0xff)break;}
  if(i==65536)return CP32_SD_TIMEOUT;
  if(r!=0xfe)return CP32_SD_IO;
  for(i=0;i<n;i++) {
    p[i]=s->bus->transfer(0xff);crc^=(uint16_t)p[i]<<8;
    for(j=0;j<8;j++)crc=(crc&0x8000) ? (uint16_t)((crc<<1)^0x1021):(uint16_t)(crc<<1);
  }
  wire=(uint16_t)s->bus->transfer(0xff)<<8;wire|=s->bus->transfer(0xff);
  return crc==wire ? 0:CP32_SD_CRC;
}
CP32_IRAM_EXT int cp32_sd_init(struct cp32_sd *s,const struct cp32_sd_bus *bus)
{
  unsigned i,v2=0;int r=CP32_SD_IO;uint32_t ocr;unsigned char csd[16],r7[4];
  if(!s || !bus || !bus->transfer || !bus->select || !bus->delay_ms)return CP32_SD_IO;
  s->bus=bus;s->ready=0;s->sectors=0;s->block_addressed=0;
  bus->select(0);bus->delay_ms(10);
  for(i=0;i<10;i++)bus->transfer(0xff); /* >=74 clocks, CS and MOSI high */
  for(i=0;i<10;i++) {r=command(s,0,0);finish(s);if(r==1)break;bus->delay_ms(10);}
  if(r!=1)goto fail;
  r=command(s,8,0x1aa);
  if(r==1) {
    for(i=0;i<4;i++)r7[i]=bus->transfer(0xff);
    if(r7[0] || r7[1] || r7[2]!=1 || r7[3]!=0xaa) {r=CP32_SD_UNSUPPORTED;goto fail;}
    v2=1;
  } else if(r!=5)goto fail; /* v1 SD may report illegal CMD8 */
  finish(s);
  for(i=0;i<200;i++) {
    r=command(s,55,0);finish(s);if(r!=0 && r!=1)goto fail;
    r=command(s,41,v2 ? 0x40000000U:0);finish(s);
    if(!r)break;if(r!=1)goto fail;bus->delay_ms(10);
  }
  if(r) {r=CP32_SD_TIMEOUT;goto fail;}
  r=command(s,58,0);if(r)goto fail;
  ocr=0;for(i=0;i<4;i++)ocr=(ocr<<8)|bus->transfer(0xff);
  finish(s);
  if(!(ocr&0x80000000U) || !(ocr&0x00300000U)) {r=CP32_SD_UNSUPPORTED;goto fail;}
  s->block_addressed=v2 && (ocr&0x40000000U);
  if(!s->block_addressed) {r=command(s,16,512);finish(s);if(r)goto fail;}
  r=command(s,9,0);if(r)goto fail;
  r=data(s,csd,16);finish(s);if(r)goto fail;
  if((csd[0]>>6)==1 && s->block_addressed) {
    uint32_t size=((uint32_t)(csd[7]&63)<<16)|((uint32_t)csd[8]<<8)|csd[9];
    s->sectors=((uint64_t)size+1)*1024;
  } else if((csd[0]>>6)==0 && !s->block_addressed) {
    unsigned bits=csd[5]&15,mult=((csd[9]&3)<<1)|(csd[10]>>7);
    unsigned size=((csd[6]&3)<<10)|(csd[7]<<2)|(csd[8]>>6);
    if(bits<9 || bits>11) {r=CP32_SD_UNSUPPORTED;goto fail;}
    s->sectors=((uint64_t)size+1)<<(mult+2+bits-9);
  } else {r=CP32_SD_UNSUPPORTED;goto fail;}
  s->ready=1;return 0;
fail:
  finish(s);s->sectors=0;s->ready=0;return r<0 ? r:CP32_SD_IO;
}
CP32_IRAM_EXT int cp32_sd_read(struct cp32_sd *s,uint32_t lba,unsigned char p[512])
{
  int r;
  if(!s || !s->ready || !p || (uint64_t)lba>=s->sectors ||
     (!s->block_addressed && lba>0x7fffffU))return CP32_SD_IO;
  r=command(s,17,s->block_addressed ? lba:lba*512U);
  if(!r)r=data(s,p,512);else if(r>0)r=CP32_SD_IO;
  finish(s);return r;
}
