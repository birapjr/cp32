#include "kernel.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "rootdisk.h"
#include "../fs/fs.h"
#include "../lib/posix/dirent.h"
#include "tty.h"
#include "application.h"
#include "../apps/hello/abi.h"
#include <string.h>
#include <minix/com.h>
#include <minix/cp32_mm.h>
#include <minix/callnr.h>
#include <errno.h>
extern int _sendrec(int dest, message *m);
static int cp32_shell_input_fd=-1;
static char cp32_tty_line[64];
static char cp32_shell_cwd[CP32_MINIX_PATH_MAX+1] = "/";
static char cp32_shell_previous[CP32_MINIX_PATH_MAX+1];
static unsigned cp32_tty_line_len;
static char cp32_shell_output[256];
static unsigned cp32_shell_output_len;
CP32_IRAM_EXT static int cp32_shell_tty_io(int operation, char *buffer, int count);

/* TTY is the only runtime LCD owner. Batch command output in FS memory,
 * then submit it as one device write, so echo cannot interleave SPI calls. */
CP32_IRAM_EXT static void cp32_shell_flush(void)
{
  int result;
  if (cp32_shell_output_len == 0) return;
  result = cp32_shell_tty_io(DEV_WRITE, cp32_shell_output,
                           cp32_shell_output_len);
  if (result != (int)cp32_shell_output_len)
    panic("shell TTY write failed", result);
  cp32_shell_output_len = 0;
}

CP32_IRAM_EXT static void cp32_shell_display(const char *s)
{
  while (*s) {
    if (cp32_shell_output_len == sizeof(cp32_shell_output)) cp32_shell_flush();
    cp32_shell_output[cp32_shell_output_len++] = *s++;
  }
}

CP32_IRAM_EXT static void cp32_shell_print(const char *s)
{
  lock(); usbj_print(s); unlock();
  cp32_shell_display(s);
}

CP32_IRAM_EXT static void cp32_shell_print_u32(uint32_t value)
{
  char buf[11]; char *p = buf + sizeof(buf) - 1;
  *p = '\0';
  do { *--p = (char)('0' + value % 10); value /= 10; } while (value);
  cp32_shell_print(p);
}

CP32_IRAM_EXT static int cp32_shell_disk_io(int operation, unsigned offset,
                                         char *buffer, int count)
{
  message request;
  int result;
  memset(&request, 0, sizeof(request));
  request.m_type = operation;
  request.DEVICE = RAM_DEV;
  request.PROC_NR = FS_PROC_NR;
  request.POSITION = offset;
  request.COUNT = count;
  request.ADDRESS = buffer;
  if (operation == DEV_WRITE) cp32_cache_invalidate();
  result = _sendrec(MEM, &request);
  if (result != OK) return result;
  if (request.m_source != MEM || request.m_type != TASK_REPLY ||
      request.REP_PROC_NR != FS_PROC_NR || request.REP_STATUS > count) return EIO;
  return request.REP_STATUS;
}

/* Preserve the last sector across this IPC round trip. Static bounded buffers
 * keep the FS call0 stack small; only this single diagnostic client uses them. */
CP32_IRAM_EXT static int cp32_shell_disk_uncached(unsigned offset, char *buffer, int count)
{
  return cp32_shell_disk_io(DEV_READ, offset, buffer, count);
}

CP32_IRAM_EXT static int cp32_shell_disk_read(unsigned offset, char *buffer, int count)
{
  return cp32_cache_read(cp32_shell_disk_uncached,cp32_root_capacity(),
                         offset,buffer,count);
}

CP32_IRAM_EXT static void cp32_shell_stat(const char *path)
{
  struct cp32_minix_stat info;
  char full[CP32_MINIX_PATH_MAX+1];
  int result = cp32_minix_abspath(cp32_shell_cwd,path,full);
  if (!result) result = cp32_minix_stat(cp32_shell_disk_read, cp32_root_capacity(), full, &info);
  if (result) {
    cp32_shell_print("Stat failed: ");
    cp32_shell_print_u32((unsigned)-result);
    cp32_shell_print("\r\n");
    return;
  }
  cp32_shell_print((info.mode & 0170000) == 0040000 ? "directory inode=" : "file inode=");
  cp32_shell_print_u32(info.inode);
  cp32_shell_print(" size="); cp32_shell_print_u32(info.size);
  cp32_shell_print(" links="); cp32_shell_print_u32(info.links);
  cp32_shell_print("\r\nuid="); cp32_shell_print_u32(info.uid);
  cp32_shell_print(" gid="); cp32_shell_print_u32(info.gid);
  cp32_shell_print(" mtime="); cp32_shell_print_u32(info.mtime);
  cp32_shell_print("\r\n");
}

CP32_IRAM_EXT static void cp32_shell_fsinfo(void)
{
  struct cp32_minix_super super;
  int result = cp32_minix_super_read(cp32_shell_disk_read,
                                    cp32_root_capacity(), &super);
  switch (result) {
    case CP32_SUPER_OK:
      cp32_shell_print("MINIX V2 superblock: inodes=");
      cp32_shell_print_u32(super.ninodes);
      cp32_shell_print(" zones="); cp32_shell_print_u32(super.zones);
      cp32_shell_print("\r\nNot mounted\r\n");
      break;
    case CP32_SUPER_ABSENT: cp32_shell_print("No MINIX filesystem\r\n"); break;
    case CP32_SUPER_UNSUPPORTED: cp32_shell_print("Unsupported MINIX format\r\n"); break;
    case CP32_SUPER_INVALID: cp32_shell_print("Invalid MINIX geometry\r\n"); break;
    default: cp32_shell_print("Superblock read failed\r\n"); break;
  }
}

CP32_IRAM_EXT static void cp32_shell_ls(const char *path)
{
  struct cp32_dir dir=CP32_DIR_INIT;
  struct cp32_dirent entry;
  char full[CP32_MINIX_PATH_MAX+1];
  int result = cp32_minix_abspath(cp32_shell_cwd,path,full);
  if (!result) {
    result=cp32_opendir(cp32_shell_disk_read,cp32_root_capacity(),full,&dir);
  }
  if (result == 0) {
    while ((result = cp32_readdir(&dir, &entry)) > 0) {
      cp32_shell_print(entry.name);
      cp32_shell_print("\r\n");
    }
  }
  if(dir.fd>=0) {
    int closed=cp32_closedir(&dir);
    if(result>=0 && closed<0) result=closed;
  }
  if (result < 0) {
    cp32_shell_print("Directory read failed: ");
    cp32_shell_print_u32((unsigned)-result);
    cp32_shell_print("\r\n");
  }
}

CP32_IRAM_EXT static int cp32_shell_disk_check(void)
{
#if !CP32_ROOT_RAM
  /* Real media diagnostics are read-only; never overwrite a mounted volume. */
  struct cp32_minix_super super;
  cp32_cache_invalidate();
  return cp32_minix_super_read(cp32_shell_disk_read,cp32_root_capacity(),&super) ? EIO:OK;
#else
  static char saved[CP32_RAMDISK_SECTOR_SIZE], data[CP32_RAMDISK_SECTOR_SIZE];
  unsigned offset = cp32_root_capacity() - sizeof(saved), i;
  int result;
  result = cp32_shell_disk_io(DEV_READ, offset, saved, sizeof(saved));
  if (result != (int)sizeof(saved)) return result < 0 ? result : EIO;
  for (i = 0; i < sizeof(data); i++) data[i] = (char)(i ^ 0x5A);
  result = cp32_shell_disk_io(DEV_WRITE, offset, data, sizeof(data));
  if (result == (int)sizeof(data)) {
    memset(data, 0, sizeof(data));
    result = cp32_shell_disk_io(DEV_READ, offset, data, sizeof(data));
    if (result == (int)sizeof(data)) {
      result = OK;
      for (i = 0; i < sizeof(data); i++)
        if ((unsigned char)data[i] != (unsigned char)(i ^ 0x5A)) result = EIO;
    } else if (result >= 0) result = EIO;
  } else if (result >= 0) result = EIO;
  /* Restore even after a failed or partial test write/read. */
  if (cp32_shell_disk_io(DEV_WRITE, offset, saved, sizeof(saved)) != (int)sizeof(saved))
    return EIO;
  if (cp32_shell_disk_io(DEV_READ, offset, data, sizeof(data)) != (int)sizeof(data))
    return EIO;
  for (i = 0; i < sizeof(data); i++) if (saved[i] != data[i]) return EIO;
  return result;
#endif
}

/* MINIX commands/simple/tail.c count semantics, using seekable regular files.
 * Find the requested lines with bounded windows. Reads may stop
 * at zone boundaries, so fill each window before scanning it in reverse. */
CP32_IRAM_EXT static int cp32_shell_tail_scan(int fd,unsigned count,int bytes_mode,int from_start)
{
  char bytes[64];
  unsigned size, end, start, length, have, lines = 0, i;
  struct cp32_minix_stat info;
  int result=cp32_fd_fstat(fd,&info);
  if(result) return result;
  size=info.size;
  if(bytes_mode) {
    unsigned position=from_start ? (count ? count-1 : 0) : (count<size ? size-count : 0);
    if(position>size) position=size;
    return cp32_fd_seek(fd,position,0,0);
  }
  if(!from_start && !count) return cp32_fd_seek(fd,size,0,0);
  if(from_start) {
    unsigned remaining=count>0 ? count-1 : 0;
    result=cp32_fd_seek(fd,0,0,0);
    if(result || !remaining) return result;
    start=0;
    while((result=cp32_fd_read(fd,bytes,sizeof(bytes)))>0) {
      for(i=0;i<(unsigned)result;i++)
        if(bytes[i]=='\n' && --remaining==0)
          return cp32_fd_seek(fd,start+i+1,0,0);
      start+=(unsigned)result;
    }
    return result<0 ? result : cp32_fd_seek(fd,size,0,0);
  }
  end=size;
  while (end) {
    start = end > sizeof(bytes) ? end - sizeof(bytes) : 0;
    length = end - start;
    result = cp32_fd_seek(fd, start, 0, 0);
    if (result) return result;
    have = 0;
    while (have < length) {
      result = cp32_fd_read(fd, bytes + have, length - have);
      if (result <= 0) return result ? result : -CP32_SUPER_IO;
      have += result;
    }
    for (i = length; i > 0; i--) {
      unsigned position = start + i - 1;
      if (bytes[i-1] == '\n' && position != size - 1 && ++lines == count)
        return cp32_fd_seek(fd, position + 1, 0, 0);
    }
    end = start;
  }
  return cp32_fd_seek(fd, 0, 0, 0);
}

/* Scan through an owned duplicate; shared offset positions the original
 * descriptor for output, while closing this reference keeps it open. */
CP32_IRAM_EXT static int cp32_shell_tail_start(int fd,unsigned count,int bytes_mode,int from_start)
{
  int scan=cp32_fd_dup(fd), result;
  if(scan<0) return scan;
  result=cp32_shell_tail_scan(scan,count,bytes_mode,from_start);
  cp32_fd_close(scan);
  return result;
}

/* cat/head/tail/cmp retain their regular-file policy even though FS descriptors
 * now support read-only directories, as MINIX open does. */
CP32_IRAM_EXT static int cp32_shell_open_file(const char *path)
{
  struct cp32_minix_stat info;
  int fd=cp32_fd_open(cp32_shell_disk_read,cp32_root_capacity(),path);
  int result;
  if(fd<0) return fd;
  result=cp32_fd_fstat(fd,&info);
  if(!result && (info.mode & 0170000)==0040000) result=-CP32_FILE_IS_DIR;
  if(result) { cp32_fd_close(fd); return result; }
  return fd;
}

/* MINIX commands/simple/wc.c adapted to bounded descriptor reads.
 * Count raw bytes, LF lines and ASCII-whitespace-delimited words. Count word
 * starts, so EOF does not lose an unterminated word as in the old reference.
 * The single regular file has a 32-bit size, which bounds every counter. */
CP32_IRAM_EXT static void cp32_shell_wc(const char *args)
{
  char full[CP32_MINIX_PATH_MAX+1], bytes[64];
  unsigned counts[3]={0,0,0}, flags=0, i;
  int fd=-1, result, word=0, printed=0;
  while(*args==' ' || *args=='\t') args++;
  if(args[0]=='-' && args[1]!='-') {
    args++;
    while(*args && *args!=' ' && *args!='\t') {
      if(*args=='l') flags|=1;
      else if(*args=='w') flags|=2;
      else if(*args=='c') flags|=4;
      else goto usage;
      args++;
    }
    if(!flags) goto usage;
    while(*args==' ' || *args=='\t') args++;
  }
  if(args[0]=='-' && args[1]=='-' && (args[2]==' ' || args[2]=='\t')) {
    args+=3;
    while(*args==' ' || *args=='\t') args++;
  } else if(args[0]=='-') goto usage;
  if(!*args) goto usage;
  if(!flags) flags=7;
  result=cp32_minix_abspath(cp32_shell_cwd,args,full);
  if(!result) {
    fd=cp32_shell_open_file(full);
    if(fd<0) result=fd;
  }
  if(!result) {
    while((result=cp32_fd_read(fd,bytes,sizeof(bytes)))>0) {
      counts[2]+=(unsigned)result;
      for(i=0;i<(unsigned)result;i++) {
        unsigned char c=(unsigned char)bytes[i];
        int space=c==' ' || (c>='\t' && c<='\r');
        if(c=='\n') counts[0]++;
        if(!space && !word) counts[1]++;
        word=!space;
      }
    }
  }
  if(fd>=0) cp32_fd_close(fd);
  if(result<0) {
    cp32_shell_print("Count failed: ");
    cp32_shell_print_u32((unsigned)-result);
    cp32_shell_print("\r\n");
    return;
  }
  /* Compact columns fit the LCD; always line, word, byte order. Never
   * publish partial counts if a read failed. */
  for(i=0;i<3;i++) if(flags & (1U<<i)) {
    if(printed) cp32_shell_print(" ");
    cp32_shell_print_u32(counts[i]); printed=1;
  }
  cp32_shell_print(" "); cp32_shell_print(args); cp32_shell_print("\r\n");
  return;
usage:
  cp32_shell_print("Usage: wc [-lwc] [--] filename\r\n");
}

CP32_IRAM_EXT int cp32_shell_app_read(void *context,uint32_t offset,
                                             unsigned char *buffer,unsigned count)
{
  int fd=*(int *)context;
  if(cp32_fd_seek(fd,offset,0,0)) return -1;
  unsigned done=0;
  while(done<count) {
    int n=cp32_fd_read(fd,(char *)buffer+done,count-done);
    if(n<=0 || (unsigned)n>count-done) return -1;
    done+=(unsigned)n;
  }
  return 0;
}
CP32_IRAM_EXT int cp32_shell_app_input(char *buffer,unsigned count)
{
  char input[64];
  int result;
  if(count>sizeof(input)) return -1;
  /* TTY copies into the FS caller's mapped stack, then FS copies into the
   * already validated application buffer while that child remains blocked. */
  cp32_shell_flush();
  result=cp32_shell_input_fd>=0 ? cp32_fd_read(cp32_shell_input_fd,input,count) :
         cp32_shell_tty_io(DEV_READ,input,count);
  if(result>0 && (unsigned)result<=count) memcpy(buffer,input,result);
  return result;
}

/* Child fd 3..6 never exposes the shell's executable or redirected input fd.
 * Store backing fd+1 so zero-initialized state is empty. */
static int cp32_app_fds[CP32_APP_FILE_MAX];
CP32_IRAM_EXT void cp32_shell_app_files_reset(void)
{
  unsigned i;
  for(i=0;i<CP32_APP_FILE_MAX;i++) if(cp32_app_fds[i]) {
    cp32_fd_close(cp32_app_fds[i]-1);cp32_app_fds[i]=0;
  }
}
CP32_IRAM_EXT static int cp32_app_file_error(int r)
{
  switch(-r) {
    case CP32_FILE_NOT_FOUND:return -2;
    case CP32_FILE_IS_DIR:return -21;
    case CP32_FILE_NOT_DIR:return -20;
    case CP32_FILE_NAME:return -36;
    case CP32_FILE_BAD_FD:return -9;
    case CP32_FILE_LIMIT:return -24;
    default:return -5;
  }
}
/* Internal backing descriptor, independent of the child's four open handles. */
CP32_IRAM_EXT int cp32_shell_exec_open(const char *name,unsigned *size)
{
  char path[CP32_MINIX_PATH_MAX+1];struct cp32_minix_stat info;
  int r,fd;
  if(!*name)return -2;
  r=cp32_minix_abspath(cp32_shell_cwd,name,path);
  if(r)return cp32_app_file_error(r);
  fd=cp32_shell_open_file(path);
  if(fd<0)return fd==-CP32_FILE_IS_DIR ? -13 : cp32_app_file_error(fd);
  r=cp32_fd_fstat(fd,&info);
  if(r || !(info.mode & 0111)) {cp32_fd_close(fd);return r ? -5 : -13;}
  *size=info.size;return fd;
}
CP32_IRAM_EXT void cp32_shell_exec_close(int fd) {cp32_fd_close(fd);}

/* Both records are nine 32-bit unsigned fields in the same declared order. */
typedef char cp32_stat_layout_check[(sizeof(struct cp32_app_stat)==
                                    sizeof(struct cp32_minix_stat)) ? 1 : -1];
CP32_IRAM_EXT int cp32_shell_app_file(unsigned op,int fd,void *buffer,unsigned arg)
{
  int r,slot;
  unsigned position;
  if(op==CP32_APP_STAT) {
    char path[CP32_MINIX_PATH_MAX+1];
    struct cp32_minix_stat info;
    if(!*(char *)buffer)return -2;
    r=cp32_minix_abspath(cp32_shell_cwd,buffer,path);
    if(!r)r=cp32_minix_stat(cp32_shell_disk_read,cp32_root_capacity(),path,&info);
    if(!r)memcpy((char *)buffer+256,&info,sizeof(info));
    return r ? cp32_app_file_error(r) : 0;
  }
  if(op==CP32_APP_OPEN || op==CP32_APP_OPENDIR) {
    char path[CP32_MINIX_PATH_MAX+1];
    struct cp32_minix_stat info;
    if(!*(char *)buffer)return -2;
    for(slot=0;slot<CP32_APP_FILE_MAX;slot++)if(!cp32_app_fds[slot])break;
    if(slot==CP32_APP_FILE_MAX)return -24;
    r=cp32_minix_abspath(cp32_shell_cwd,buffer,path);
    if(r)return cp32_app_file_error(r);
    r=op==CP32_APP_OPENDIR ? cp32_fd_open(cp32_shell_disk_read,cp32_root_capacity(),path) :
                           cp32_shell_open_file(path);
    if(r<0)return cp32_app_file_error(r);
    fd=r;r=cp32_fd_fstat(fd,&info);
    if(!r && op==CP32_APP_OPENDIR && (info.mode & 0170000)!=0040000) {
      cp32_fd_close(fd);return -20;
    }
    if(r || !(info.mode & 0444)) {
      cp32_fd_close(fd);return r ? cp32_app_file_error(r) : -13;
    }
    cp32_app_fds[slot]=fd+1;return slot+3;
  }
  if(fd<3 || fd>=3+CP32_APP_FILE_MAX || !cp32_app_fds[fd-3])return -9;
  slot=fd-3;fd=cp32_app_fds[slot]-1;
  if(op==CP32_APP_FSTAT) {
    struct cp32_minix_stat info;
    r=cp32_fd_fstat(fd,&info);
    if(!r)memcpy(buffer,&info,sizeof(info));
  } else if(op==CP32_APP_READDIR) {
    struct cp32_app_dirent entry;
    memset(&entry,0,sizeof(entry));
    r=cp32_fd_readdir(fd,&entry.inode,entry.name);
    if(r==1)memcpy(buffer,&entry,sizeof(entry));
  } else if(op==CP32_APP_CLOSE) {
    r=cp32_fd_close(fd);
    if(!r)cp32_app_fds[slot]=0;
  } else if(op==CP32_APP_READ) {
    char data[64];
    if(arg>sizeof(data))return -22;
    r=cp32_fd_read(fd,data,arg);
    if(r>0)memcpy(buffer,data,r);
  } else if(op>=CP32_APP_SEEK_SET && op<=CP32_APP_SEEK_END) {
    r=cp32_fd_seek(fd,(int)arg,op-CP32_APP_SEEK_SET,&position);
    if(!r)return position;
    if(r==-CP32_SUPER_INVALID)return -22;
  } else return -22;
  return r<0 ? cp32_app_file_error(r) : r;
}

CP32_IRAM_EXT static void cp32_shell_app_output(const char *buffer,unsigned count)
{
  char text[65];
  unsigned n=0,i;
  for(i=0;i<count;i++) {
    unsigned char c=buffer[i];
    if(n>61) { text[n]=0; cp32_shell_print(text); n=0; }
    if(c=='\n') text[n++]='\r';
    text[n++]=(c=='\n' || (c>=32 && c<=126)) ? c : '?';
  }
  text[n]=0; cp32_shell_print(text);
}
/* Application words are bounded by the 63-byte shell input line. */
/* Bounded in-place word decoding, a subset of MINIX ash's quote removal.
 * No expansion, operators or multiline continuation in this bootstrap shell.
 * Output never grows; callers discard all words on a syntax/limit error. */
CP32_IRAM_EXT static int cp32_shell_words(char *text,const char **argv,
                                         unsigned capacity,unsigned *count)
{
  char *read=text,*write=text;
  unsigned n=0;
  while(*read) {
    char quote=0;
    while(*read==' ' || *read=='\t') read++;
    if(!*read) break;
    if(n==capacity) return -1;
    argv[n++]=write;
    while(*read && (quote || (*read!=' ' && *read!='\t'))) {
      char c=*read++;
      if(c==quote) { quote=0; continue; }
      if(!quote && (c=='\'' || c=='"')) { quote=c; continue; }
      if(c=='\\' && quote!='\'') {
        if(!*read) return -2;
        if(quote=='"' && *read!='"' && *read!='\\' &&
           *read!='$' && *read!='`') { *write++=c; continue; }
        c=*read++;
      }
      *write++=c;
    }
    if(quote) return -2;
    /* Advance before writing NUL when read and write still coincide. */
    while(*read==' ' || *read=='\t') read++;
    *write++=0;
  }
  *count=n;
  return 0;
}

/* One trailing input redirection; quoted/escaped '<' stays an argument. */
CP32_IRAM_EXT static int cp32_shell_redirect(char *text,const char **path)
{
  char *p=text,*split=0,quote=0;
  unsigned count;
  *path=0;
  for(;*p;p++) {
    if(*p=='\\' && quote!='\'' && p[1]) {
      if(!quote || p[1]=='"' || p[1]=='\\' || p[1]=='$' || p[1]=='`') p++;
      continue;
    }
    if(quote) {if(*p==quote)quote=0;continue;}
    if(*p=='\'' || *p=='"') {quote=*p;continue;}
    if(*p=='<') {if(split)return -1;split=p;}
  }
  if(!split) return 0;
  *split++=0;
  if(cp32_shell_words(split,path,1,&count) || count!=1 || !(*path)[0]) return -1;
  return 0;
}

CP32_IRAM_EXT static void cp32_shell_launch(const char *args,int mode)
{
  struct cp32_minix_stat info;
  char copy[64], full[CP32_MINIX_PATH_MAX+1], *p;
  const char *argv[9],*input_path;
  int hello=mode==1;
  unsigned argc=hello ? 1 : 0,n=0;
  int status=0,result,fd;
  if(hello) argv[0]="hello";
  while(n<sizeof(copy)-1 && args[n]) { copy[n]=args[n]; n++; }
  if(args[n]) goto usage;
  copy[n]=0;
  if(cp32_shell_redirect(copy,&input_path)) {
    cp32_shell_print("Invalid input redirection\r\n");return;
  }
  result=cp32_shell_words(copy,argv+argc,9-argc,&n);
  if(result==-2) { cp32_shell_print("Invalid command quoting\r\n"); return; }
  if(result) goto usage;
  argc+=n;
  if(!argc || !argv[0][0]) goto usage;
  /* MINIX shell lookup: slash bypasses PATH; builtins were handled first.
   * The bootstrap environment currently has the single fixed PATH=/boot. */
  p=(char *)argv[0];
  while(*p && *p!='/') p++;
  result=cp32_minix_abspath(mode==2 && !*p ? "/boot" : cp32_shell_cwd,
                           hello ? "/boot/hello" : argv[0],full);
  if(result) { cp32_shell_print("Invalid executable path\r\n"); return; }
  fd=cp32_shell_open_file(full);
  if(fd<0) {
    cp32_shell_print(mode==2 ? "Command not found\r\n" : "Cannot open executable\r\n");
    return;
  }
  result=cp32_fd_fstat(fd,&info);
  /* Trusted single-client policy until MM credentials exist. */
  if(!result && !(info.mode & 0111)) {
    cp32_fd_close(fd);
    cp32_shell_print("File is not executable\r\n");
    return;
  }
  if(!result && input_path) {
    result=cp32_minix_abspath(cp32_shell_cwd,input_path,full);
    if(!result) cp32_shell_input_fd=cp32_shell_open_file(full);
    if(result || cp32_shell_input_fd<0) {
      cp32_fd_close(fd);cp32_shell_print("Cannot open input\r\n");return;
    }
  }
  if(!result) result=cp32_application_run(cp32_shell_app_read,&fd,info.size,
                                        argc,argv,cp32_shell_app_output,&status);
  if(cp32_shell_input_fd>=0) {cp32_fd_close(cp32_shell_input_fd);cp32_shell_input_fd=-1;}
  cp32_fd_close(fd);
  if(result) cp32_shell_print(hello ? "Hello load failed\r\n" : "Application load failed\r\n");
  else {
    cp32_shell_print(hello ? "Hello exit=" : "Application exit=");
    if(status<0) { cp32_shell_print("-"); cp32_shell_print_u32(0U-(unsigned)status); }
    else cp32_shell_print_u32((unsigned)status);
    cp32_shell_print("\r\n");
  }
  return;
usage:
  cp32_shell_print(hello ? "Usage: hello [up to 8 arguments]\r\n" :
                          "Usage: run path [up to 8 arguments]\r\n");
}

CP32_IRAM_EXT static void cp32_shell_hello(const char *args)
{
  cp32_shell_launch(args,1);
}

/* mode: 0=whole file, 1=tail, 2=head (positive line count). */
CP32_IRAM_EXT static void cp32_shell_show(const char *name,int mode,unsigned count,int bytes_mode,int from_start)
{
  int fd=-1;
  char bytes[64], text[129];
  unsigned i, n, printed = 0;
  int result, newline = 1;
  char full[CP32_MINIX_PATH_MAX+1];
  result = cp32_minix_abspath(cp32_shell_cwd,name,full);
  if (!result) {
    fd=cp32_shell_open_file(full);
    if(fd<0) result=fd;
  }
  if (!result && mode==1) result = cp32_shell_tail_start(fd,count,bytes_mode,from_start);
  if (!result) {
    while ((result = cp32_fd_read(fd, bytes, sizeof(bytes))) > 0) {
      n = 0;
      for (i = 0; i < (unsigned)result; i++) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == '\n') { text[n++] = '\r'; text[n++] = '\n'; }
        else text[n++] = (ch >= 0x20 && ch <= 0x7e) ? ch : '?';
        newline = ch == '\n';
        if(mode==2 && newline && --count==0) break;
      }
      text[n] = 0;
      cp32_shell_print(text);
      printed = 1;
      if(mode==2 && !count) { result=0; break; }
    }
  }
  if(fd>=0) cp32_fd_close(fd);
  if (printed && !newline) cp32_shell_print("\r\n");
  if (result < 0) {
    if (result == -CP32_FILE_NOT_FOUND) cp32_shell_print("File not found\r\n");
    else if (result == -CP32_FILE_IS_DIR) cp32_shell_print("Is a directory\r\n");
    else if (result == -CP32_FILE_NAME) cp32_shell_print("Invalid path\r\n");
    else if (result == -CP32_FILE_NOT_DIR) cp32_shell_print("Not a directory\r\n");
    else {
      cp32_shell_print("File read failed: ");
      cp32_shell_print_u32((unsigned)-result);
      cp32_shell_print("\r\n");
    }
  }
}

CP32_IRAM_EXT static void cp32_shell_cat(const char *name)
{
  cp32_shell_show(name,0,0,0,0);
}

/* MINIX commands/simple/head.c: default ten lines and historical -N.
 * Also accept -n N; retain positive counts and a single regular file.
 * No stdio or heap is needed on the freestanding Xtensa target. */
CP32_IRAM_EXT static void cp32_shell_head(const char *args)
{
  unsigned count=10;
  char *end;
  long number;
  while(*args==' ' || *args=='\t') args++;
  if(args[0]=='-' && args[1]!='-') {
    args++;
    if(args[0]=='n' && (args[1]==' ' || args[1]=='\t')) {
      args++;
      while(*args==' ' || *args=='\t') args++;
    }
    if(*args<'0' || *args>'9') goto usage;
    errno=0;
    number=strtol(args,&end,10);
    if(errno || number<=0 || (*end!=' ' && *end!='\t')) goto usage;
    count=(unsigned)number;
    args=end;
    while(*args==' ' || *args=='\t') args++;
  }
  if(args[0]=='-' && args[1]=='-' && (args[2]==' ' || args[2]=='\t')) {
    args+=3;
    while(*args==' ' || *args=='\t') args++;
  } else if(args[0]=='-') goto usage;
  if(!*args) goto usage;
  cp32_shell_show(args,2,count,0,0);
  return;
usage:
  cp32_shell_print("Usage: head [-N | -n N] [--] filename\r\n");
}

CP32_IRAM_EXT static void cp32_shell_tail(const char *args)
{
  unsigned count=10;
  int bytes_mode=0,from_start=0;
  char *end;
  long number;
  while(*args==' ' || *args=='\t') args++;
  if(args[0]=='-' && (args[1]=='n' || args[1]=='c') &&
      (args[2]==' ' || args[2]=='\t')) {
    bytes_mode=args[1]=='c';
    args+=3;
    while(*args==' ' || *args=='\t') args++;
    from_start=*args=='+';
    errno=0;
    number=strtol(args,&end,10);
    if(errno || end==args || (*end!=' ' && *end!='\t')) goto usage;
    count=number<0 ? (unsigned)(-(number+1))+1U : (unsigned)number;
    args=end;
    while(*args==' ' || *args=='\t') args++;
  }
  if(args[0]=='-' && args[1]=='-' && (args[2]==' ' || args[2]=='\t')) {
    args+=3;
    while(*args==' ' || *args=='\t') args++;
  } else if(args[0]=='-') goto usage;
  if(!*args) goto usage;
  cp32_shell_show(args,1,count,bytes_mode,from_start);
  return;
usage:
  cp32_shell_print("Usage: tail [-n count | -c count] [--] filename\r\n");
}

/* Allocate/release via MM's real IPC service; never touch allocator state
 * from the client. The temporary allocation is released on every success. */
CP32_IRAM_EXT static int cp32_shell_mm_exchange(void)
{
  message m;
  int result;
  memset(&m, 0, sizeof(m));
  m.m_type = CP32_MM_ALLOCATE;
  m.m1_i1 = 1;
  result = _sendrec(MM_PROC_NR, &m);
  if (result != OK) return result;
  if (m.m_source != MM_PROC_NR) return EIO;
  if (m.m_type != OK) return m.m_type;
  if (m.m1_i1 <= 0) return EIO;
  m.m_type = CP32_MM_RELEASE;
  result = _sendrec(MM_PROC_NR, &m);
  if (result != OK) return result;
  if (m.m_source != MM_PROC_NR) return EIO;
  return m.m_type;
}

/* Read-only MINIX SYS queries through the production system-task loop. */
CP32_IRAM_EXT static int cp32_shell_sys_query(uint32_t *ticks)
{
  message request;
  int result;
  memset(&request, 0, sizeof(request));
  request.m_type = SYS_GETSP;
  request.PROC1 = FS_PROC_NR;
  result = _sendrec(SYSTASK, &request);
  if (result != OK) return result;
  if (request.m_source != SYSTASK) return EIO;
  if (request.m_type != OK) return request.m_type;
  if (request.STACK_PTR == (char *)0 || ((uintptr_t)request.STACK_PTR & 15))
    return EIO;
  memset(&request, 0, sizeof(request));
  request.m_type = SYS_TIMES;
  request.PROC1 = FS_PROC_NR;
  result = _sendrec(SYSTASK, &request);
  if (result != OK) return result;
  if (request.m_source != SYSTASK) return EIO;
  if (request.m_type != OK) return request.m_type;
  *ticks = (uint32_t)request.BOOT_TICKS;
  return OK;
}

/* Previous-directory policy belongs to the command client, not FS chdir. */
CP32_IRAM_EXT static void cp32_shell_cd(const char *path)
{
  char next[CP32_MINIX_PATH_MAX+1];
  int previous = strcmp(path, "-") == 0;
  int result;
  if (previous && !cp32_shell_previous[0]) {
    cp32_shell_print("No previous directory\r\n");
    return;
  }
  strcpy(next, cp32_shell_cwd);
  result = cp32_minix_chdir(cp32_shell_disk_read, cp32_root_capacity(),
                           next, previous ? cp32_shell_previous : path);
  if (result) {
    cp32_shell_print("Directory change failed: ");
    cp32_shell_print_u32((unsigned)-result); cp32_shell_print("\r\n");
    return;
  }
  strcpy(cp32_shell_previous, cp32_shell_cwd);
  strcpy(cp32_shell_cwd, next);
  if (previous) {
    cp32_shell_print(cp32_shell_cwd); cp32_shell_print("\r\n");
  }
}

/* Compare raw bytes through two independent opens. No quoting/escape syntax
 * yet: the command client accepts two whitespace-separated path arguments. */
CP32_IRAM_EXT static void cp32_shell_cmp(const char *args)
{
  char names[2][64], full[CP32_MINIX_PATH_MAX+1], bytes[2][64];
  int fd[2]={-1,-1}, result=0, a, b;
  unsigned i, j;
  for(i=0;i<2;i++) {
    while(*args==' ' || *args=='\t') args++;
    j=0;
    while(*args && *args!=' ' && *args!='\t') {
      if(j==63) goto usage;
      names[i][j++]=*args++;
    }
    names[i][j]=0;
    if(!j) goto usage;
  }
  while(*args==' ' || *args=='\t') args++;
  if(*args) goto usage;
  for(i=0;i<2;i++) {
    result=cp32_minix_abspath(cp32_shell_cwd,names[i],full);
    if(result) goto done;
    fd[i]=cp32_shell_open_file(full);
    if(fd[i]<0) { result=fd[i]; goto done; }
  }
  for(;;) {
    a=cp32_fd_read(fd[0],bytes[0],64);
    if(a<0) { result=a; break; }
    b=cp32_fd_read(fd[1],bytes[1],64);
    if(b<0) { result=b; break; }
    if(a!=b) { result=1; break; }
    for(j=0;j<(unsigned)a;j++) if(bytes[0][j]!=bytes[1][j]) break;
    if(j!=(unsigned)a) { result=1; break; }
    if(!a) break;
  }
done:
  for(i=0;i<2;i++) if(fd[i]>=0) cp32_fd_close(fd[i]);
  if(result<0) {
    cp32_shell_print("Compare failed: "); cp32_shell_print_u32((unsigned)-result);
    cp32_shell_print("\r\n");
  } else cp32_shell_print(result ? "Files differ\r\n" : "Files identical\r\n");
  return;
usage:
  cp32_shell_print("Usage: cmp file1 file2\r\n");
}

CP32_IRAM_EXT static void cp32_shell_command(const char *line)
{
  if (strcmp(line, "ipc") == 0) {
    message request;
    struct termios attributes;
    int result;
    memset(&request, 0, sizeof(request));
    memset(&attributes, 0, sizeof(attributes));
    request.m_type = DEV_IOCTL;
    request.TTY_LINE = 0;
    request.PROC_NR = FS_PROC_NR;
    request.TTY_REQUEST = TCGETS;
    request.ADDRESS = (char *)&attributes;
    result = _sendrec(TTY_PROC_NR, &request);
    if (result == OK && (request.m_source != TTY_PROC_NR ||
        request.m_type != TASK_REPLY || request.REP_PROC_NR != FS_PROC_NR))
      result = EIO;
    if (result == OK) result = request.REP_STATUS;
    cp32_shell_print("[TTY IPC V44 reply-result=");
    cp32_shell_print_u32((uint32_t)result);
    cp32_shell_print("]\r\n");
  } else if (strcmp(line, "mm") == 0) {
    int result = cp32_shell_mm_exchange();
    int saved_ps = lock_save();
    usbj_print("[MM IPC V44 alloc-release-result=");
    usbj_print_u32((uint32_t)result);
    usbj_print("]\r\n");
    restore_lock(saved_ps);
    cp32_shell_display(result == OK ? "MM alloc/release OK\r\n" :
                                           "MM alloc/release failed\r\n");
  } else if (strcmp(line, "sys") == 0) {
    uint32_t ticks = 0;
    int result = cp32_shell_sys_query(&ticks);
    int saved_ps = lock_save();
    usbj_print("[SYS IPC V44 result="); usbj_print_u32((uint32_t)result);
    usbj_print(" uptime="); usbj_print_u32(ticks);
    usbj_print("]\r\n");
    restore_lock(saved_ps);
    cp32_shell_display(result == OK ? "SYS queries OK\r\n" :
                                           "SYS queries failed\r\n");
  } else if (strcmp(line, "write") == 0) {
    char text[] = "TTY write via IPC\n";
    int result, saved_ps;
    result = cp32_shell_tty_io(DEV_WRITE, text, sizeof(text) - 1);
    saved_ps = lock_save();
    usbj_print("[TTY WRITE V44 result="); usbj_print_u32((uint32_t)result);
    usbj_print(" expected="); usbj_print_u32(sizeof(text) - 1);
    usbj_print("]\r\n");
    restore_lock(saved_ps);
  } else if (strcmp(line, "hello") == 0 ||
             (line[0]=='h' && line[1]=='e' && line[2]=='l' && line[3]=='l' && line[4]=='o' && (line[5]==' ' || line[5]=='\t'))) {
    cp32_shell_hello(line[5] ? line+6 : "");
  } else if (strcmp(line,"run")==0 ||
             (line[0]=='r' && line[1]=='u' && line[2]=='n' &&
              (line[3]==' ' || line[3]=='\t'))) {
    cp32_shell_launch(line[3] ? line+4 : "",0);
  } else if (strcmp(line, "font") == 0) {
    cp32_shell_print("< > [ ] { } ( ) | ? \" ' \\ ! + - _ / = . .. A a C c\r\n");
  } else if (strcmp(line, "ls") == 0) {
    cp32_shell_ls(".");
  } else if (line[0] == 'l' && line[1] == 's' && line[2] == ' ') {
    cp32_shell_ls(line + 3);
  } else if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' && line[3] == ' ') {
    cp32_shell_cat(line + 4);
  } else if (strcmp(line, "cat") == 0) {
    cp32_shell_print("Usage: cat filename\r\n");
  } else if (line[0] == 't' && line[1] == 'a' && line[2] == 'i' &&
             line[3] == 'l' && line[4] == ' ') {
    cp32_shell_tail(line + 5);
  } else if (line[0]=='h' && line[1]=='e' && line[2]=='a' &&
             line[3]=='d' && (line[4]==' ' || line[4]=='\t')) {
    cp32_shell_head(line+5);
  } else if (strcmp(line, "wc") == 0 ||
             (line[0]=='w' && line[1]=='c' && (line[2]==' ' || line[2]=='\t'))) {
    cp32_shell_wc(line[2] ? line+3 : "");
  } else if (strcmp(line, "head") == 0) {
    cp32_shell_head("");
  } else if (strcmp(line, "tail") == 0) {
    cp32_shell_tail("");
  } else if (line[0] == 's' && line[1] == 't' && line[2] == 'a' &&
             line[3] == 't' && line[4] == ' ') {
    cp32_shell_stat(line + 5);
  } else if (strcmp(line, "stat") == 0) {
    cp32_shell_print("Usage: stat pathname\r\n");
  } else if (strcmp(line, "pwd") == 0) {
    cp32_shell_print(cp32_shell_cwd); cp32_shell_print("\r\n");
  } else if (strcmp(line, "cd") == 0 ||
             (line[0]=='c' && line[1]=='d' && line[2]==' ')) {
    cp32_shell_cd(line[2] ? line+3 : "/");
  } else if (strcmp(line, "cmp") == 0 ||
             (line[0]=='c' && line[1]=='m' && line[2]=='p' && line[3]==' ')) {
    cp32_shell_cmp(line[3] ? line+4 : "");
  } else if (strcmp(line, "fsinfo") == 0) {
    cp32_shell_fsinfo();
  } else if (strcmp(line, "disk") == 0) {
    int result = cp32_shell_disk_check();
    cp32_shell_print("[DISK IPC V123 result=");
    cp32_shell_print_u32((uint32_t)result);
    cp32_shell_print("]\r\n");
  } else if (strcmp(line, "storage") == 0 || strcmp(line, "ramdisk") == 0) {
    cp32_shell_print("[STORAGE " CP32_ROOT_NAME " capacity=");
    cp32_shell_print_u32(cp32_root_capacity());
    cp32_shell_print("]\r\n");
  } else if (line[0] != '\0') {
    cp32_shell_launch(line,2);
  }
}

/* MINIX driver I/O protocol: TASK_REPLY may finish now or suspend the
 * request. After SUSPEND keep the stack buffer live until TTY sends REVIVE. */
CP32_IRAM_EXT static int cp32_shell_tty_io(int operation, char *buffer, int count)
{
  message request;
  int result;
  memset(&request, 0, sizeof(request));
  request.m_type = operation;
  request.TTY_LINE = 0;
  request.PROC_NR = FS_PROC_NR;
  request.COUNT = count;
  request.ADDRESS = buffer;
  result = _sendrec(TTY_PROC_NR, &request);
  if (result != OK) return result;
  if (request.m_source != TTY_PROC_NR || request.m_type != TASK_REPLY ||
      request.REP_PROC_NR != FS_PROC_NR) return EIO;
  if (request.REP_STATUS == SUSPEND) {
    result = _receive(TTY_PROC_NR, &request);
    if (result != OK) return result;
    if (request.m_source != TTY_PROC_NR || request.m_type != REVIVE ||
        request.REP_PROC_NR != FS_PROC_NR) return EIO;
  }
  result = request.REP_STATUS;
  if (result > count || result == SUSPEND) return EIO;
  return result;
}

CP32_IRAM_EXT static void cp32_trace_shell_read(int count)
{
  static unsigned reports;
  if (++reports == 1 || reports % 500 == 0) {
    int saved_ps = lock_save();
    usbj_print("[TTY READ V46 reply="); usbj_print_u32((uint32_t)count);
    usbj_print(" n="); usbj_print_u32(reports); usbj_print("]\r\n");
    restore_lock(saved_ps);
  }
}

CP32_IRAM_EXT void cp32_tty_read_client(void)
{
  char input[64];
  int overflow = 0;
  usbj_print("[TTY user-entry]\r\n");
  for (;;) {
    int count = cp32_shell_tty_io(DEV_READ, input, sizeof(input)), i;
    if (count < 0) panic("shell TTY read failed", count);
    cp32_trace_shell_read(count);
    for (i = 0; i < count; i++) {
      char ch = input[i];
      if (ch == '\n') {
        cp32_tty_line[cp32_tty_line_len] = '\0';
        usbj_print("[TTY line="); usbj_print(cp32_tty_line); usbj_print("]\r\n");
        if (overflow) cp32_shell_print("Command too long\r\n");
        else cp32_shell_command(cp32_tty_line);
        cp32_shell_display("$ ");
        cp32_shell_flush();
        cp32_tty_line_len = 0;
        overflow = 0;
      } else if (ch >= 0x20 && ch <= 0x7E) {
        if (cp32_tty_line_len < sizeof(cp32_tty_line) - 1)
          cp32_tty_line[cp32_tty_line_len++] = ch;
        else overflow = 1;
      }
    }
  }
}
