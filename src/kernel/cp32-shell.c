#include "kernel.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "ramdisk.h"
#include "../fs/fs.h"
#include "tty.h"
#include <string.h>
#include <minix/com.h>
#include <minix/cp32_mm.h>
#include <minix/callnr.h>
#include <errno.h>
extern int _sendrec(int dest, message *m);
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
  return cp32_cache_read(cp32_shell_disk_uncached,cp32_ramdisk_capacity(),
                         offset,buffer,count);
}

CP32_IRAM_EXT static void cp32_shell_stat(const char *path)
{
  struct cp32_minix_stat info;
  char full[CP32_MINIX_PATH_MAX+1];
  int result = cp32_minix_abspath(cp32_shell_cwd,path,full);
  if (!result) result = cp32_minix_stat(cp32_shell_disk_read, cp32_ramdisk_capacity(), full, &info);
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
                                    cp32_ramdisk_capacity(), &super);
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
  struct cp32_minix_dir dir;
  unsigned inode;
  char name[15];
  char full[CP32_MINIX_PATH_MAX+1];
  int result = cp32_minix_abspath(cp32_shell_cwd,path,full);
  if (!result) result = cp32_minix_dir_open(cp32_shell_disk_read,
                                  cp32_ramdisk_capacity(), full, &dir);
  if (result == 0) {
    while ((result = cp32_minix_root_next(&dir, &inode, name)) > 0) {
      cp32_shell_print(name);
      cp32_shell_print("\r\n");
    }
  }
  if (result < 0) {
    cp32_shell_print("Directory read failed: ");
    cp32_shell_print_u32((unsigned)-result);
    cp32_shell_print("\r\n");
  }
}

CP32_IRAM_EXT static int cp32_shell_disk_check(void)
{
  static char saved[CP32_RAMDISK_SECTOR_SIZE], data[CP32_RAMDISK_SECTOR_SIZE];
  unsigned offset = cp32_ramdisk_capacity() - sizeof(saved), i;
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
}

/* Find the last ten lines with bounded backward windows. Reads may stop
 * at zone boundaries, so fill each window before scanning it in reverse. */
CP32_IRAM_EXT static int cp32_shell_tail_scan(int fd)
{
  char bytes[64];
  unsigned size, end, start, length, have, lines = 0, i;
  struct cp32_minix_stat info;
  int result=cp32_fd_fstat(fd,&info);
  if(result) return result;
  size=info.size;
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
      if (bytes[i-1] == '\n' && position != size - 1 && ++lines == 10)
        return cp32_fd_seek(fd, position + 1, 0, 0);
    }
    end = start;
  }
  return cp32_fd_seek(fd, 0, 0, 0);
}

/* Scan through an owned duplicate; shared offset positions the original
 * descriptor for output, while closing this reference keeps it open. */
CP32_IRAM_EXT static int cp32_shell_tail_start(int fd)
{
  int scan=cp32_fd_dup(fd), result;
  if(scan<0) return scan;
  result=cp32_shell_tail_scan(scan);
  cp32_fd_close(scan);
  return result;
}

CP32_IRAM_EXT static void cp32_shell_show(const char *name, int tail)
{
  int fd=-1;
  char bytes[64], text[129];
  unsigned i, n, printed = 0;
  int result, newline = 1;
  char full[CP32_MINIX_PATH_MAX+1];
  result = cp32_minix_abspath(cp32_shell_cwd,name,full);
  if (!result) {
    fd=cp32_fd_open(cp32_shell_disk_read,cp32_ramdisk_capacity(),full);
    if(fd<0) result=fd;
  }
  if (!result && tail) result = cp32_shell_tail_start(fd);
  if (!result) {
    while ((result = cp32_fd_read(fd, bytes, sizeof(bytes))) > 0) {
      n = 0;
      for (i = 0; i < (unsigned)result; i++) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == '\n') { text[n++] = '\r'; text[n++] = '\n'; }
        else text[n++] = (ch >= 0x20 && ch <= 0x7e) ? ch : '?';
        newline = ch == '\n';
      }
      text[n] = 0;
      cp32_shell_print(text);
      printed = 1;
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
  cp32_shell_show(name, 0);
}

CP32_IRAM_EXT static void cp32_shell_tail(const char *name)
{
  cp32_shell_show(name, 1);
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
  result = cp32_minix_chdir(cp32_shell_disk_read, cp32_ramdisk_capacity(),
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
    fd[i]=cp32_fd_open(cp32_shell_disk_read,cp32_ramdisk_capacity(),full);
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
  } else if (strcmp(line, "font") == 0) {
    cp32_shell_print("hyphen: -\r\n");
    cp32_shell_print("under:  _\r\n");
    cp32_shell_print("slash:  /\r\n");
    cp32_shell_print("equals: =\r\n");
    cp32_shell_print("dots: . ..\r\n");
    cp32_shell_print("case: Aa Cc\r\n");
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
  } else if (strcmp(line, "tail") == 0) {
    cp32_shell_print("Usage: tail filename\r\n");
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
    cp32_shell_print("[RAM IPC V49 result=");
    cp32_shell_print_u32((uint32_t)result);
    cp32_shell_print("]\r\n");
  } else if (strcmp(line, "ramdisk") == 0) {
    cp32_shell_print("[CMD ramdisk capacity=");
    cp32_shell_print_u32(cp32_ramdisk_capacity());
    cp32_shell_print("]\r\n");
  } else if (line[0] != '\0') {
    cp32_shell_print("[CMD unknown=");
    cp32_shell_print(line);
    cp32_shell_print("]\r\n");
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
