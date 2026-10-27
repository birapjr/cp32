#ifndef CP32_APP_ABI_H
#define CP32_APP_ABI_H
/* Bootstrap ABI v22 (earlier prefixes preserved): trusted call0 callbacks, no privilege boundary.
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
/* v9 keeps the service-table prefix; operations use fixed-width records. */
struct cp32_app_open_request {char path[256];unsigned flags,mode;};
#define CP32_APP_OPEN_MODE 11
#define CP32_APP_WRITE 12
#define CP32_APP_DUP 13
/* v10: process-local cwd, umask and descriptor flags. */
#define CP32_APP_CHDIR 14
#define CP32_APP_GETCWD 15
#define CP32_APP_UMASK 16
#define CP32_APP_GETFD 17
#define CP32_APP_SETFD 18
#define CP32_APP_GETFL 19
#define CP32_APP_SETFL 20
#define CP32_APP_UNLINK 21
/* v12: mkdir uses open_request path/mode; flags must be zero. */
#define CP32_APP_MKDIR 22
/* v13: two bounded paths for regular-file hard links. */
struct cp32_app_link_request {char source[256],destination[256];};
#define CP32_APP_LINK 23
/* v14: chmod uses open_request path/mode, flags zero; rwx bits only. */
#define CP32_APP_CHMOD 24
#define CP32_APP_RMDIR 25 /* v15: terminated path, like unlink */
/* v16: same two-path record as link; regular files, no replacement. */
#define CP32_APP_RENAME_NOREPLACE 26
#define CP32_APP_FTRUNCATE 27 /* v17: fd and nonnegative byte length */
#define CP32_APP_DUP2 28 /* v18: source fd, target in argument; file slots only */
#define CP32_APP_DUPFD 29 /* v19: fd, minimum local descriptor in argument */
#define CP32_APP_FCHMOD 30 /* v20: fd, rwx mode in argument */
/* v21: positional transfers, at most64 bytes per request. */
struct cp32_app_position_request {unsigned offset,count;char data[64];};
#define CP32_APP_PREAD 31
#define CP32_APP_PWRITE 32
struct cp32_app_fsinfo {unsigned zone_size,zones,free_zones,inodes,free_inodes,name_max;};
#define CP32_APP_FSINFO 33 /* v22: sole mounted volume allocation counts */
#define CP32_APP_FILE_MAX 4

#define CP32_APP_RENAME 34 /* v23: replacement and directory moves */
/* v24: explicit device completion; no pointer payload. */
#define CP32_APP_SYNC 35
#define CP32_APP_FSYNC 36
/* v25: seconds since Unix epoch, range 0..INT32_MAX. */
#define CP32_APP_TIME 37
#define CP32_APP_STIME 38
#define CP32_APP_UTIME 39
struct cp32_app_utime_request {char path[256];unsigned atime,mtime,current;};
struct cp32_utimbuf {unsigned actime,modtime;};
#endif
