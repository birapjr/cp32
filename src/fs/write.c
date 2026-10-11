/* MINIX read.c:rw_chunk maps an existing zone and preserves bytes outside
 * the write. This first write milestone uses synchronous device completion
 * instead of dirty buffers. Allocation, timestamps and POSIX write follow. */
#include "fs.h"
CP32_IRAM_EXT int cp32_minix_patch(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned offset,const char *bytes,unsigned n)
{
  struct cp32_minix_file file;
  struct cp32_minix_stat info;
  unsigned zone,physical;int r;
  if(!read || !write || !path || !bytes || !n || n>64 || n>512-offset%512)
    return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  /* The privileged shell can edit a read-only-mode data file. Executable
   * files are deliberately outside this maintenance operation. */
  if(info.mode&0111)return -CP32_SUPER_UNSUPPORTED;
  r=cp32_minix_file_open(read,capacity,path,&file);if(r)return r;
  if(offset>file.size || n>file.size-offset)return -CP32_SUPER_INVALID;
  r=cp32_fs_read_map(&file,offset/file.zone_bytes,&zone);if(r)return r;
  if(!zone)return -CP32_SUPER_UNSUPPORTED; /* allocation not yet ported */
  physical=zone*file.zone_bytes+offset%file.zone_bytes;
  if(physical>capacity || n>capacity-physical)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=write(physical,bytes,n);
  cp32_cache_invalidate(); /* also discard cache entries after uncertain I/O */
  return r==(int)n ? r:-CP32_SUPER_IO;
}
