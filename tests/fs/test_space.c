#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "fs.h"
static unsigned char disk[65536];static unsigned reads,fail;
static int rd(unsigned at,char *p,int n){assert(at+(unsigned)n<=sizeof(disk));if(++reads==fail)return -1;memcpy(p,disk+at,n);return n;}
static void p16(unsigned at,unsigned v,unsigned swap){disk[at+(swap?1:0)]=v;disk[at+(swap?0:1)]=v>>8;}
static void p32(unsigned at,unsigned v,unsigned swap){p16(at,swap?v>>16:v,swap);p16(at+2,swap?v:v>>16,swap);}
int main(void)
{
 for(unsigned swap=0;swap<2;swap++)for(unsigned scale=0;scale<2;scale++) {
  memset(disk,0,sizeof(disk));p16(1024,17,swap);p16(1028,1,swap);p16(1030,1,swap);
  p16(1032,6,swap);p16(1034,scale,swap);p32(1036,263*1024,swap);p16(1040,0x2468,swap);p32(1044,32,swap);
  /* Reserve bit0, inode1 and data-zone1. All padding bits are allocated. */
  memset(disk+2048,255,2048);
  p16(2048,3,swap);p16(2050,0xfffc,swap);
  p16(3072,3,swap);p16(3074,0xf800,swap);
  struct cp32_minix_space info,old;reads=fail=0;
  assert(!cp32_minix_space_read(rd,sizeof(disk),&info));unsigned steps=reads;
  assert(info.zone_size==(1024U<<scale) && info.zones==26 && info.free_zones==25);
  assert(info.inodes==17 && info.free_inodes==16 && info.name_max==14);
  memset(&old,0x5a,sizeof(old));
  for(unsigned i=1;i<=steps;i++){info=old;reads=0;fail=i;assert(cp32_minix_space_read(rd,sizeof(disk),&info)==-CP32_SUPER_IO && !memcmp(&info,&old,sizeof(info)));}
  fail=0;memset(disk+2048,255,2048);assert(!cp32_minix_space_read(rd,sizeof(disk),&info) && !info.free_zones && !info.free_inodes);
  memset(disk+2048,0,2048);assert(!cp32_minix_space_read(rd,sizeof(disk),&info) && info.free_zones==26 && info.free_inodes==17);
  info=old;assert(cp32_minix_space_read(rd,2048,&info)==-CP32_SUPER_INVALID && !memcmp(&info,&old,sizeof(info)));
 }
 puts("space: endian/scaled zones, reserved/padding bits, full/free maps, all read faults and atomic output pass");
}
