/* MINIX exec.c read_header responsibility, using Xtensa ELF32 rather than
 * x86 a.out. Fixed destinations deliberately exclude kernel/stack storage. */
#include "image.h"
CP32_IRAM_EXT static uint32_t u32(const unsigned char *p)
{
  return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
CP32_IRAM_EXT static unsigned u16(const unsigned char *p)
{ return p[0]|((unsigned)p[1]<<8); }
CP32_IRAM_EXT int cp32_image_read(cp32_image_reader read,void *context,
                                uint32_t size,struct cp32_image *out)
{
  unsigned char h[52], p[32];
  struct cp32_image next;
  uint32_t phoff, end[2];
  unsigned i;
  if(!read || !out || size<sizeof(h)) return -1;
  if(read(context,0,h,sizeof(h))) return -2;
  if(h[0]!=127 || h[1]!='E' || h[2]!='L' || h[3]!='F' ||
     h[4]!=1 || h[5]!=1 || h[6]!=1 || h[7]!=0 || h[8]!=0 ||
     u16(h+16)!=2 || u16(h+18)!=94 || u32(h+20)!=1 ||
     u16(h+40)!=52 || u16(h+42)!=32 || u16(h+44)!=2) return -1;
  /* Xtensa toolchain emits EF_XTENSA_XT_INSN and possibly XT_LIT. */
  if(u32(h+36)&~0x300U) return -1;
  phoff=u32(h+28);
  if(phoff<52 || phoff>size || size-phoff<64) return -1;
  next.entry=u32(h+24);
  for(i=0;i<2;i++) {
    struct cp32_image_segment *s=i ? &next.data : &next.text;
    uint32_t align, limit=i ? CP32_APP_STACK : CP32_APP_TEXT_END;
    if(read(context,phoff+i*32,p,sizeof(p))) return -2;
    s->offset=u32(p+4); s->address=u32(p+8);
    s->filesz=u32(p+16); s->memsz=u32(p+20); align=u32(p+28);
    if(u32(p)!=1 || u32(p+12)!=s->address || u32(p+24)!=(i?6U:5U) ||
       s->address!=(i?CP32_APP_DATA:CP32_APP_TEXT) ||
       !s->memsz || (!i && !s->filesz) || s->filesz>s->memsz ||
       s->memsz>limit-s->address || s->offset<phoff+64 ||
       s->offset>size || s->filesz>size-s->offset ||
       (align>1 && ((align&(align-1)) || ((s->address-s->offset)&(align-1)))))
      return -1;
    end[i]=s->offset+s->filesz;
  }
  if(next.text.offset<end[1] && next.data.offset<end[0]) return -1;
  if(next.entry<next.text.address || next.entry-next.text.address>=next.text.filesz)
    return -1;
  *out=next;
  return 0;
}

CP32_IRAM_EXT static void clear_image(unsigned char *text,unsigned char *data)
{
  unsigned i;
  for(i=0;i<CP32_APP_TEXT_END-CP32_APP_TEXT;i++) text[i]=0;
  for(i=0;i<CP32_APP_STACK-CP32_APP_DATA;i++) data[i]=0;
}

/* MINIX exec.c load_seg/zeroing responsibility, staging into an inactive
 * slot only. The runtime owner supplies the writable instruction alias. */
CP32_IRAM_EXT int cp32_image_load(cp32_image_reader read,void *context,
    uint32_t size,unsigned char *text,unsigned char *data,struct cp32_image *out)
{
  struct cp32_image next;
  unsigned i;
  int result;
  if(!text || !data || !out) return -1;
  result=cp32_image_read(read,context,size,&next);
  if(result) return result;
  clear_image(text,data);
  for(i=0;i<2;i++) {
    const struct cp32_image_segment *s=i ? &next.data : &next.text;
    unsigned char *dest=i ? data : text;
    uint32_t offset=0;
    while(offset<s->filesz) {
      unsigned count=s->filesz-offset>64 ? 64 : s->filesz-offset;
      if(read(context,s->offset+offset,dest+offset,count)) {
        clear_image(text,data);
        return -2;
      }
      offset+=count;
    }
  }
  *out=next;
  return 0;
}
