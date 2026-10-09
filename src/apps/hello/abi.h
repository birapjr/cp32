#ifndef CP32_APP_ABI_H
#define CP32_APP_ABI_H
/* Bootstrap ABI v8 (earlier prefixes preserved): trusted call0 callbacks, no privilege boundary.
 * Loader supplies this table in a2; stack is built by cp32_exec_stack.
 * write returns bytes written or a negative error; exit must not return.
 * The kernel bridge supports one foreground process with write/exit/getpid/getppid IPC.
 * v2 appends getpid; v3 appends getppid after the v2 fields. */
struct cp32_app_services {
  unsigned version;
  int (*write)(const char *,unsigned);
  void (*exit)(int);
  int (*getpid)(void);
  int (*getppid)(void); /* v3: launching shell PID */
  void *(*sbrk)(int); /* v4: old break, or (void *)-1; zero-filled growth */
  int (*read)(char *,unsigned); /* v5: canonical console input, at most 64 bytes */
  /* v6: file operations return a value or negative MINIX errno. */
  int (*file)(unsigned operation,int fd,void *buffer,unsigned argument);
};
#define CP32_APP_OPEN 0
#define CP32_APP_READ 1
#define CP32_APP_CLOSE 2
#define CP32_APP_SEEK_SET 3
#define CP32_APP_SEEK_CUR 4
#define CP32_APP_SEEK_END 5
/* v7: fixed-width metadata and directory records, copied by value. */
struct cp32_app_stat {
  unsigned inode,mode,links,uid,gid,size,atime,mtime,ctime;
};
struct cp32_app_dirent {unsigned inode;char name[16];};
struct cp32_app_stat_request {char path[256];struct cp32_app_stat info;};
#define CP32_APP_FSTAT 6
#define CP32_APP_READDIR 7
#define CP32_APP_OPENDIR 8
#define CP32_APP_STAT 9
/* v8 exec: bounded NUL-separated argv, copied before replacing the image. */
struct cp32_app_exec_request {char path[256],args[256];unsigned argc;};
#define CP32_APP_EXEC 10
#define CP32_APP_FILE_MAX 4
#endif
