/* MINIX fs/time.c:do_utime. CP32 writes the V2 timestamp triple synchronously;
 * CLOCK supplies current seconds at the FS broker boundary. No RTC assumption. */
#include "fproc.h"

CP32_FS_TEXT int cp32_fproc_utime(struct cp32_fproc *p,cp32_disk_reader read,
    cp32_disk_writer write,unsigned capacity,const char *name,
    unsigned atime,unsigned mtime,unsigned now,int current)
{
  char path[256];struct cp32_minix_super super;struct cp32_minix_stat info;
  unsigned number,values[3],i,j,at,rights;unsigned char raw[12];int r;
  if(!p || !p->pid)return -9;
  if(!name || !*name)return -2;
  if(!write)return -30;
  if(now>0x7fffffffU || (!current && (atime>0x7fffffffU || mtime>0x7fffffffU)))return -22;
  r=cp32_minix_abspath(p->cwd,name,path);if(r)return cp32_fs_errno(r);
  cp32_cache_invalidate();
  r=cp32_fs_resolve_path(read,capacity,path,&super,&number);
  if(!r)r=cp32_fs_stat_inode(read,&super,number,&info);
  if(r)return cp32_fs_errno(r);
  if(p->euid && p->euid!=info.uid) {
    rights=(info.mode>>(p->egid==info.gid ? 3:0))&7;
    if(!current)return -1;
    if(!(rights&2))return -13;
  }
  values[0]=current ? now:atime;values[1]=current ? now:mtime;values[2]=now;
  for(i=0;i<3;i++)for(j=0;j<4;j++)raw[i*4+j]=(unsigned char)(values[i]>>(8*(super.swapped ? 3-j:j)));
  at=(2+super.imap_blocks+super.zmap_blocks)*1024+(number-1)*64+12;
  r=write(at,(const char *)raw,sizeof(raw));cp32_cache_invalidate();
  return r==sizeof(raw) ? 0:-5;
}
