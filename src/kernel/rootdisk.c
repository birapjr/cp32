/* Read-only root volume. MINIX partition/device separation, with SD SPI
 * sectors in place of a PC disk controller. No whole-volume SRAM copy. */
#include "rootdisk.h"
#include "sdcard.h"
#include "../fs/fs.h"
#include <string.h>
static unsigned char sector[512];
static uint32_t volume_start,capacity,cached_lba;
static unsigned cached;
CP32_IRAM_EXT static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
CP32_IRAM_EXT unsigned cp32_root_capacity(void) {return capacity;}
CP32_IRAM_EXT int cp32_root_write(unsigned offset,const void *p,unsigned n)
{(void)offset;(void)p;(void)n;return -1;}
CP32_IRAM_EXT int cp32_root_read(unsigned offset,void *out,unsigned n)
{
  unsigned char *p=out;unsigned chunk,within;uint32_t lba;
  if(!capacity || offset>capacity || n>capacity-offset || (n && !p))return -1;
  while(n) {
    lba=volume_start+offset/512;within=offset%512;
    if(!cached || cached_lba!=lba) {
      cached=0;if(cp32_sdcard_read(lba,sector))return -1;
      cached_lba=lba;cached=1;
    }
    chunk=512-within;if(chunk>n)chunk=n;
    memcpy(p,sector+within,chunk);p+=chunk;offset+=chunk;n-=chunk;
  }
  return 0;
}
CP32_IRAM_EXT static int volume_read(unsigned offset,char *p,int n)
{return n<0 || cp32_root_read(offset,p,(unsigned)n) ? -1:n;}
CP32_IRAM_EXT int cp32_root_init(void)
{
  uint64_t total,length;uint32_t start=0;unsigned i,found=0;int r;
  struct cp32_minix_super super;
  capacity=0;cached=0;volume_start=0;
  r=cp32_sdcard_init();if(r)return r;
  total=cp32_sdcard_sectors();length=total;
  if(total<4 || cp32_sdcard_read(0,sector))return -5;
  /* Exactly one primary MINIX partition (type 0x81), or a raw V2 volume.
   * Never interpret another partition table as a raw filesystem. */
  if(sector[510]==0x55 && sector[511]==0xaa) {
    for(i=0;i<4;i++) {
      const unsigned char *p=sector+446+i*16;
      if(p[4]==0x81) {
        if(++found!=1 || (p[0]!=0 && p[0]!=0x80))return -6;
        start=le32(p+8);length=le32(p+12);
        if(!start || length<4 || (uint64_t)start+length>total)return -6;
      }
    }
    if(!found)return -6;
  }
  /* The existing FS/device ABI uses signed 32-bit byte positions. Refuse
   * larger volumes instead of truncating their address space. */
  if(length>0x7ffffe00U/512U) {
    if(found)return -7;
    length=0x7ffffe00U/512U; /* raw volume: validate its own size below */
  }
  volume_start=start;capacity=(uint32_t)length*512;
  r=cp32_minix_super_read(volume_read,capacity,&super);
  if(r) {capacity=0;cached=0;return -8;}
  if(!found)capacity=super.zones*(1024U<<super.log_zone_size);
  cached=0;return 0;
}
