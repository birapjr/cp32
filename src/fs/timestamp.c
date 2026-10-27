/* MINIX inode.c:update_times / read.c:read_write. The serialized FS binds its
 * mounted device and CLOCK service; host readers and read-only roots opt out.
 * No allocation, interrupt work, or hardware clock assumptions live here. */
#include "fs.h"
static cp32_disk_reader time_reader;
static cp32_disk_writer time_writer;
static int (*time_clock)(unsigned *);
CP32_FS_TEXT void cp32_fs_time_bind(cp32_disk_reader read,cp32_disk_writer write,
                                  int (*clock)(unsigned *))
{ time_reader=read;time_writer=write;time_clock=clock; }

/* raw points at the 12-byte V2 atime/mtime/ctime tuple. */
CP32_FS_TEXT int cp32_fs_stamp(cp32_disk_reader read,unsigned char *raw,
                             unsigned swapped,unsigned mask)
{
  unsigned now,i,j;
  if(read!=time_reader || !time_writer || !time_clock)return 0;
  if(time_clock(&now) || now>0x7fffffffU)return -CP32_SUPER_IO;
  for(i=0;i<3;i++)if(mask&(1U<<i))
    for(j=0;j<4;j++)raw[4*i+j]=(unsigned char)(now>>(8*(swapped ? 3-j:j)));
  return 0;
}

CP32_FS_TEXT int cp32_fs_data_time(struct cp32_minix_file *file,unsigned mask)
{
  unsigned char raw[12];unsigned at,previous[3],i;int r;
  if(file->read!=time_reader || !time_writer || !time_clock)return 0;
  at=(2+file->super.imap_blocks+file->super.zmap_blocks)*1024+(file->inode-1)*64+12;
  if(file->read(at,(char *)raw,12)!=12)return -CP32_SUPER_IO;
  for(i=0;i<3;i++)previous[i]=cp32_fs_u32(raw+4*i,file->super.swapped);
  r=cp32_fs_stamp(file->read,raw,file->super.swapped,mask);if(r)return r;
  /* Repeated small reads in one second do not wear the card unnecessarily. */
  for(i=0;i<3;i++)if(cp32_fs_u32(raw+4*i,file->super.swapped)!=previous[i])break;
  if(i==3)return 0;
  cp32_cache_invalidate();r=time_writer(at,(const char *)raw,12);cp32_cache_invalidate();
  return r==12 ? 0:-CP32_SUPER_IO;
}
