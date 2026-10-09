#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sd_spi.h"
static unsigned selected,packet_n,head,tail,clocks,init_tries;
static unsigned version=2,hc=1,absent,stuck,bad_echo,bad_ocr,bad_csd,bad_crc,no_token,read_error;
static uint8_t packet[6],queue[600];
static uint32_t read_arg;
static unsigned commands[64];
static void push(unsigned b) {assert(tail<sizeof(queue));queue[tail++]=(uint8_t)b;}
static void block(const unsigned char *p,unsigned n) {
  uint16_t crc=0;
  push(0xff);push(0xfe);
  for(unsigned i=0;i<n;i++) {
    push(p[i]);crc^=(uint16_t)p[i]<<8;
    for(unsigned j=0;j<8;j++)crc=crc&0x8000 ? (uint16_t)((crc<<1)^0x1021):(uint16_t)(crc<<1);
  }
  push(crc>>8);push((crc&255)^bad_crc);
}
static void process(void) {
  unsigned cmd=packet[0]&63;commands[cmd]++;
  uint32_t arg=((uint32_t)packet[1]<<24)|((uint32_t)packet[2]<<16)|((uint32_t)packet[3]<<8)|packet[4];
  unsigned char bytes[512]={0};
  switch(cmd) {
  case 0:assert(packet[5]==0x95 && clocks>=10);push(1);break;
  case 8:assert(packet[5]==0x87 && arg==0x1aa);push(version==2 ? 1:5);
    if(version==2){push(0);push(0);push(1);push(bad_echo ? 0:0xaa);}break;
  case 55:push(1);break;
  case 41:assert(arg==(version==2 ? 0x40000000U:0));push(stuck || ++init_tries<3 ? 1:0);break;
  case 58:push(0);push(bad_ocr ? 0:hc ? 0xc0:0x80);push(0x30);push(0);push(0);break;
  case 16:assert(!hc && arg==512);push(0);break;
  case 9:push(0);
    if(hc){bytes[0]=0x40;bytes[9]=7;} /* 8192 sectors */
    else {bytes[5]=9;bytes[6]=3;bytes[7]=255;bytes[8]=0xc0;} /* 16384 sectors */
    if(bad_csd)bytes[0]=0xc0;
    block(bytes,16);break;
  case 17:read_arg=arg;push(read_error ? 4:0);
    if(!read_error && !no_token) {for(unsigned i=0;i<512;i++)bytes[i]=(uint8_t)(i^0xa5);block(bytes,512);}break;
  default:assert(!"unexpected command (writes are forbidden)");
  }
}
static uint8_t transfer(uint8_t b) {
  if(!selected) {assert(b==255);clocks++;return 255;}
  if(absent)return 255;
  if(head<tail)return queue[head++];
  if(packet_n || (b&0xc0)==0x40) {
    packet[packet_n++]=b;
    if(packet_n==6) {packet_n=0;head=tail=0;process();}
  }
  return 255;
}
static void select_card(int state) {
  selected=state;
  if(!state){packet_n=head=tail=0;}
}
static void delay(unsigned ms) {assert(ms==10);}
static const struct cp32_sd_bus bus={transfer,select_card,delay};
static void reset(void) {
 selected=packet_n=head=tail=clocks=init_tries=0;
 absent=stuck=bad_echo=bad_ocr=bad_csd=bad_crc=no_token=read_error=0;
 version=2;hc=1;memset(commands,0,sizeof(commands));
}
int main(void) {
 struct cp32_sd card;unsigned char bytes[512];
 for(unsigned mode=0;mode<3;mode++) {
  reset();version=mode==0 ? 1:2;hc=mode==2;
  assert(!cp32_sd_init(&card,&bus) && card.ready && !selected);
  assert(card.sectors==(hc ? 8192:16384));
  assert(!cp32_sd_read(&card,7,bytes) && read_arg==(hc ? 7:7*512) && !selected);
  for(unsigned i=0;i<512;i++)assert(bytes[i]==(uint8_t)(i^0xa5));
  assert(cp32_sd_read(&card,(uint32_t)card.sectors,bytes)==CP32_SD_IO);
  bad_crc=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_CRC && !selected);bad_crc=0;
  read_error=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_IO && !selected);read_error=0;
  no_token=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_TIMEOUT && !selected);
 }
 for(unsigned failure=0;failure<6;failure++) {
  reset();
  if(failure==0)absent=1;if(failure==1)stuck=1;if(failure==2)bad_echo=1;
  if(failure==3)bad_ocr=1;if(failure==4)bad_csd=1;if(failure==5)bad_crc=1;
  assert(cp32_sd_init(&card,&bus)<0 && !selected && !card.ready && !card.sectors);
  assert(cp32_sd_read(&card,0,bytes)==CP32_SD_IO);
 }
 puts("SD SPI: SDSC v1/v2, SDHC, command CRC, addressing, CRC16, absent/timeout/error paths pass");
}
