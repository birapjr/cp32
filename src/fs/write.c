/* MINIX read.c:rw_chunk maps an existing zone and preserves bytes outside
 * the write. This first write milestone uses synchronous device completion
 * instead of dirty buffers. Allocation, timestamps and POSIX write follow. */
#include "fs.h"
CP32_FS_TEXT int cp32_minix_patch(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned offset,const char *bytes,unsigned n)
{
  struct cp32_minix_file file;
  struct cp32_minix_stat info;
  unsigned zone,physical[2],length[2],count=0,done=0,i;int r;
  if(!read || !write || !path || !bytes || !n || n>64)
    return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  /* The privileged shell can edit a read-only-mode data file. Executable
   * files are deliberately outside this maintenance operation. */
  if(info.mode&0111)return -CP32_SUPER_UNSUPPORTED;
  r=cp32_minix_file_open(read,capacity,path,&file);if(r)return r;
  if(offset>file.size || n>file.size-offset)return -CP32_SUPER_INVALID;
  /* At most two sectors for a bounded patch. Validate every mapping before
   * the first write, including a possibly noncontiguous next filesystem zone. */
  while(done<n) {
    unsigned position=offset+done,chunk=512-position%512;
    if(chunk>n-done)chunk=n-done;
    r=cp32_fs_read_map(&file,position/file.zone_bytes,&zone);if(r)return r;
    if(!zone)return -CP32_SUPER_UNSUPPORTED; /* no sparse allocation */
    physical[count]=zone*file.zone_bytes+position%file.zone_bytes;
    if(physical[count]>capacity || chunk>capacity-physical[count])
      return -CP32_SUPER_INVALID;
    length[count++]=chunk;done+=chunk;
  }
  done=0;
  for(i=0;i<count;i++) {
    cp32_cache_invalidate();
    r=write(physical[i],bytes+done,length[i]);
    cp32_cache_invalidate(); /* discard even after uncertain completion */
    if(r!=(int)length[i])return -CP32_SUPER_IO;
    done+=length[i];
  }
  r=cp32_fs_data_time(&file,6);
  return r ? r:(int)done;
}
