/* MINIX stadir.c:do_stat/stat_inode responsibilities. Read-only metadata
 * helper, not the descriptor/syscall ABI. Decode disk fields for Xtensa. */
#include "fs.h"

CP32_IRAM_EXT static int stat_inode(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_stat *out)
{
  struct cp32_minix_stat next;
  unsigned char raw[64];
  int result;
  if (!out || !read || !number || number>super->ninodes) return -CP32_SUPER_INVALID;
  result = cp32_fs_allocated(read, 2048, number, super->swapped);
  if (result) return result;
  if (read((2 + super->imap_blocks + super->zmap_blocks)*1024 + (number-1)*64,
           (char *)raw, sizeof(raw)) != sizeof(raw)) return -CP32_SUPER_IO;
  next.inode = number;
  next.mode = cp32_fs_u16(raw, super->swapped);
  next.links = cp32_fs_u16(raw+2, super->swapped);
  next.uid = cp32_fs_u16(raw+4, super->swapped);
  next.gid = cp32_fs_u16(raw+6, super->swapped);
  next.size = cp32_fs_u32(raw+8, super->swapped);
  next.atime = cp32_fs_u32(raw+12, super->swapped);
  next.mtime = cp32_fs_u32(raw+16, super->swapped);
  next.ctime = cp32_fs_u32(raw+20, super->swapped);
  if (!next.links || next.size > super->max_size) return -CP32_SUPER_INVALID;
  /* Metadata inspection does not require reading a file's data zones. */
  if ((next.mode & 0170000) != 0100000 && (next.mode & 0170000) != 0040000)
    return -CP32_SUPER_UNSUPPORTED;
  *out = next;
  return 0;
}

CP32_IRAM_EXT int cp32_minix_stat(cp32_disk_reader read, unsigned capacity,
    const char *path, struct cp32_minix_stat *out)
{
  struct cp32_minix_super super;
  unsigned number;
  int result;
  if(!out) return -CP32_SUPER_INVALID;
  result=cp32_fs_resolve_path(read,capacity,path,&super,&number);
  return result ? result : stat_inode(read,&super,number,out);
}

/* MINIX do_fstat: use the open inode, never the current pathname or offset. */
CP32_IRAM_EXT int cp32_fd_fstat(int fd,struct cp32_minix_stat *out)
{
  const struct cp32_minix_file *file=cp32_fd_file(fd);
  if(!file) return -CP32_FILE_BAD_FD;
  return stat_inode(file->read,&file->super,file->inode,out);
}

/* MINIX do_chdir/change responsibility for the single read-only command
 * client. Publish cwd only after directory validation; no credentials yet. */
CP32_IRAM_EXT int cp32_minix_chdir(cp32_disk_reader read, unsigned capacity,
                                  char *cwd, const char *path)
{
  char full[CP32_MINIX_PATH_MAX+1], canonical[CP32_MINIX_PATH_MAX+1];
  struct cp32_minix_dir dir, checked;
  unsigned i=0, n=1, start, length, j;
  int result=cp32_minix_abspath(cwd,path,full);
  if (result) return result;
  result=cp32_minix_dir_open(read,capacity,full,&dir);
  if (result) return result;
  canonical[0]='/';
  while(full[i]) {
    while(full[i]=='/') i++;
    start=i;
    while(full[i] && full[i]!='/') i++;
    length=i-start;
    if (!length || (length==1 && full[start]=='.')) continue;
    if (length==2 && full[start]=='.' && full[start+1]=='.') {
      while(n>1 && canonical[n-1]!='/') n--;
      if(n>1) n--;
    } else {
      if(n>1) canonical[n++]='/';
      for(j=0;j<length;j++) canonical[n++]=full[start+j];
    }
  }
  canonical[n]=0;
  /* The display path must describe the same inode on malformed dot entries. */
  {
    struct cp32_minix_super super;
    unsigned original, normalized;
    result=cp32_fs_resolve_path(read,capacity,full,&super,&original);
    if(result) return result;
    result=cp32_fs_resolve_path(read,capacity,canonical,&super,&normalized);
    if(result) return result;
    if(original!=normalized) return -CP32_SUPER_INVALID;
  }
  result=cp32_minix_dir_open(read,capacity,canonical,&checked);
  if(result) return result;
  for(i=0;i<=n;i++) cwd[i]=canonical[i];
  return 0;
}
