#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "image.h"
static unsigned char file[65536];
static unsigned size, calls, fail;
static int reader(void *ctx,uint32_t off,unsigned char *p,unsigned n) {
  (void)ctx;
  assert(off<=size && n<=size-off);
  if(++calls==fail) return -1;
  memcpy(p,file+off,n); return 0;
}
static void put(unsigned at,uint32_t v) {
  for(unsigned i=0;i<4;i++) file[at+i]=(unsigned char)(v>>(8*i));
}
static void fixture(void) {
  memset(file,0,sizeof(file)); size=256; calls=0; fail=0;
  memcpy(file,"\177ELF\1\1\1",7); file[16]=2; file[18]=94;
  put(20,1); put(24,CP32_APP_TEXT); put(28,52); put(36,0x300);
  file[40]=52; file[42]=32; file[44]=2;
  put(52,1); put(56,128); put(60,CP32_APP_TEXT); put(64,CP32_APP_TEXT);
  put(68,16); put(72,16); put(76,5); put(80,16);
  put(84,1); put(88,144); put(92,CP32_APP_DATA); put(96,CP32_APP_DATA);
  put(100,16); put(104,32); put(108,6); put(112,16);
}
static unsigned char text[16384+2], data[12288+2];
static void test_load(void) {
  struct cp32_image image, old;
  unsigned total;
  memset(&old,0xa5,sizeof(old));
  memset(text,0xa5,sizeof(text)); memset(data,0xa5,sizeof(data)); calls=0;
  assert(!cp32_image_load(reader,0,size,text+1,data+1,&image));
  total=calls;
  assert(!memcmp(text+1,file+image.text.offset,image.text.filesz));
  assert(!memcmp(data+1,file+image.data.offset,image.data.filesz));
  for(unsigned i=image.text.filesz;i<16384;i++) assert(!text[1+i]);
  for(unsigned i=image.data.filesz;i<12288;i++) assert(!data[1+i]);
  assert(text[0]==0xa5 && text[16385]==0xa5 && data[0]==0xa5 && data[12289]==0xa5);
  for(unsigned n=1;n<=total;n++) {
    memset(text+1,0xa5,16384); memset(data+1,0xa5,12288);
    calls=0; fail=n; image=old;
    assert(cp32_image_load(reader,0,size,text+1,data+1,&image)==-2);
    assert(!memcmp(&image,&old,sizeof(old)));
    for(unsigned i=1;i<=16384;i++) assert(text[i]==(n<=3 ? 0xa5 : 0));
    for(unsigned i=1;i<=12288;i++) assert(data[i]==(n<=3 ? 0xa5 : 0));
    assert(text[0]==0xa5 && text[16385]==0xa5 && data[0]==0xa5 && data[12289]==0xa5);
  }
  fail=0; calls=0;
  assert(!cp32_image_load(reader,0,size,text+1,data+1,&image));
}
int main(int argc,char **argv) {
  struct cp32_image image, sentinel;
  memset(&sentinel,0xa5,sizeof(sentinel));
  if(argc==2) {
    FILE *f=fopen(argv[1],"rb"); assert(f);
    size=fread(file,1,sizeof(file),f); assert(feof(f)); fclose(f);
    assert(!cp32_image_read(reader,0,size,&image));
    printf("hello ELF validated: entry=%08x text=%u data=%u bss=%u\n",
      image.entry,image.text.filesz,image.data.filesz,image.data.memsz-image.data.filesz);
    test_load();
    return 0;
  }
  fixture(); test_load();
  fixture(); assert(!cp32_image_read(reader,0,size,&image));
  assert(image.entry==CP32_APP_TEXT && image.data.memsz==32);
  const unsigned offsets[]={0,4,5,6,7,8,16,18,20,28,36,40,42,44,
    52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,24};
  for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
    fixture(); put(offsets[i],0xffffffffU); image=sentinel;
    assert(cp32_image_read(reader,0,size,&image)<0);
    assert(!memcmp(&image,&sentinel,sizeof(image)));
  }
  for(unsigned n=0;n<160;n++) {
    fixture(); size=n; image=sentinel;
    assert(cp32_image_read(reader,0,size,&image)<0);
    assert(!memcmp(&image,&sentinel,sizeof(image)));
  }
  for(unsigned n=1;n<=3;n++) {
    fixture(); fail=n; image=sentinel;
    assert(cp32_image_read(reader,0,size,&image)==-2);
    assert(!memcmp(&image,&sentinel,sizeof(image)));
  }
  fixture(); put(88,128); assert(cp32_image_read(reader,0,size,&image)<0);
  fixture(); put(104,0x3001); assert(cp32_image_read(reader,0,size,&image)<0);
  fixture(); put(24,CP32_APP_TEXT+16); assert(cp32_image_read(reader,0,size,&image)<0);
  puts("ELF validator: format, regions, bounds, overlap, entry and atomic failure passed");
}
