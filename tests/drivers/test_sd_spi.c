#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sd_spi.h"
static unsigned selected,packet_n,head,tail,clocks,init_tries;
static unsigned version=2,hc=1,absent,stuck,bad_echo,bad_ocr,bad_csd,bad_crc,no_token,read_error;
static uint8_t packet[6],queue[600];
static uint32_t read_arg,write_arg;
static unsigned ready_busy;
static unsigned writing,write_n,write_reject,write_busy,status_error,command_error;
static uint8_t written[514];
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
  case 24:write_arg=arg;push(command_error ? 4:0);writing=!command_error;write_n=0;break;
  case 13:push(0);push(status_error);break;
  default:assert(!"unexpected command");
  }
}
static uint8_t transfer(uint8_t b) {
  if(!selected) {assert(b==255);clocks++;return 255;}
  if(absent)return 255;
  if(ready_busy)return 0;
  if(head<tail)return queue[head++];
  if(writing) {
    if(writing==1) {if(b==255)return 255;assert(b==0xfe);writing=2;return 255;}
    if(writing==3)return write_busy ? 0:255;
    assert(write_n<514);written[write_n++]=b;
    if(write_n==514) {
      /* Independent polynomial long-division check over data plus CRC. */
      unsigned residue=0;
      for(unsigned k=0;k<514;k++)for(int bit=7;bit>=0;bit--) {
        residue=(residue<<1)|((written[k]>>bit)&1);
        if(residue&0x10000)residue^=0x11021;
      }
      assert(!residue);head=tail=0;push(write_reject ? write_reject:5);
      push(0);push(0);if(!write_busy)push(255);writing=3;
    }
    return 255;
  }
  if(packet_n || (b&0xc0)==0x40) {
    packet[packet_n++]=b;
    if(packet_n==6) {packet_n=0;head=tail=0;process();}
  }
  return 255;
}
static void select_card(int state) {
  selected=state;
  if(!state){packet_n=head=tail=writing=0;}
}
static unsigned delayed,bursts,bulk_failure,fault;
static void delay(unsigned ms) {assert(ms==10 || ms==1);delayed+=ms;}
static int receive_bytes(uint8_t *p,unsigned n) {
 bursts++;if(bulk_failure)return CP32_SD_TIMEOUT;
 for(unsigned i=0;i<n;i++)p[i]=transfer(255);
 return 0;
}
static int transmit_bytes(const uint8_t *p,unsigned n) {
 if(bulk_failure)return CP32_SD_TIMEOUT;
 for(unsigned i=0;i<n;i++)transfer(p[i]);return 0;
}
static int error(void) {return fault;}
static struct cp32_sd_bus bus={transfer,select_card,delay,0,error,0};
static void reset(void) {
 writing=write_n=write_reject=write_busy=status_error=command_error=0;
 delayed=bursts=bulk_failure=fault=0;
 selected=packet_n=head=tail=clocks=init_tries=0;
 absent=stuck=bad_echo=bad_ocr=bad_csd=bad_crc=no_token=read_error=0;
 version=2;hc=1;memset(commands,0,sizeof(commands));
}
int main(void) {
 struct cp32_sd card;unsigned char bytes[512];
 for(unsigned bulk=0;bulk<2;bulk++) {
 bus.receive=bulk ? receive_bytes:0;bus.transmit=bulk ? transmit_bytes:0;
 for(unsigned mode=0;mode<3;mode++) {
  reset();version=mode==0 ? 1:2;hc=mode==2;
  assert(!cp32_sd_init(&card,&bus) && card.ready && !selected);
  assert(card.sectors==(hc ? 8192:16384));
  assert(!cp32_sd_read(&card,7,bytes) && read_arg==(hc ? 7:7*512) && !selected);
  for(unsigned i=0;i<512;i++)assert(bytes[i]==(uint8_t)(i^0xa5));
  assert(cp32_sd_read(&card,(uint32_t)card.sectors,bytes)==CP32_SD_IO);
  bad_crc=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_CRC && !selected);bad_crc=0;
  read_error=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_IO && !selected);read_error=0;
  delayed=0;no_token=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_TIMEOUT && !selected);
  assert(delayed>=256);no_token=0;
  assert(!cp32_sd_write(&card,7,bytes) && !selected);
  assert(write_arg==(hc ? 7:7*512) && !memcmp(written,bytes,512));
  assert(commands[13]==1);
  assert(!cp32_sd_sync(&card) && commands[13]==2 && !selected);
  status_error=1;assert(cp32_sd_sync(&card)==CP32_SD_IO && !selected);status_error=0;
  ready_busy=1;assert(cp32_sd_sync(&card)==CP32_SD_TIMEOUT && !selected);ready_busy=0;
  fault=1;assert(cp32_sd_sync(&card)==CP32_SD_IO && !selected);fault=0;
  card.ready=0;assert(cp32_sd_sync(&card)==CP32_SD_IO);card.ready=1;
  unsigned before=commands[24];
  assert(cp32_sd_write(&card,(uint32_t)card.sectors,bytes)==CP32_SD_IO && commands[24]==before);
  write_reject=11;assert(cp32_sd_write(&card,7,bytes)==CP32_SD_CRC && !selected);
  write_reject=13;assert(cp32_sd_write(&card,7,bytes)==CP32_SD_IO && !selected);write_reject=0;
  write_busy=1;delayed=0;
  assert(cp32_sd_write(&card,7,bytes)==CP32_SD_TIMEOUT && !selected && delayed>=1000);write_busy=0;
  status_error=0x20;assert(cp32_sd_write(&card,7,bytes)==CP32_SD_IO && !selected);status_error=0;
  command_error=1;write_n=0;
  assert(cp32_sd_write(&card,7,bytes)==CP32_SD_IO && !selected && !write_n);command_error=0;
  if(bulk) {bulk_failure=1;assert(cp32_sd_write(&card,7,bytes)==CP32_SD_TIMEOUT && !selected);bulk_failure=0;}
 }
 for(unsigned failure=0;failure<6;failure++) {
  reset();
  if(failure==0)absent=1;if(failure==1)stuck=1;if(failure==2)bad_echo=1;
  if(failure==3)bad_ocr=1;if(failure==4)bad_csd=1;if(failure==5)bad_crc=1;
  assert(cp32_sd_init(&card,&bus)<0 && !selected && !card.ready && !card.sectors);
  assert(cp32_sd_read(&card,0,bytes)==CP32_SD_IO);
 }
 reset();assert(!cp32_sd_init(&card,&bus));
 fault=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_IO && !selected);fault=0;
 if(bulk) {
  bulk_failure=1;assert(cp32_sd_read(&card,7,bytes)==CP32_SD_TIMEOUT && !selected);
  reset();bulk_failure=1;
  assert(cp32_sd_init(&card,&bus)==CP32_SD_TIMEOUT && !card.ready && !selected);
 }
 }
 puts("SD SPI: SDSC v1/v2, SDHC, CMD24/CRC16/status/busy and read/addressing/error paths pass");
}
