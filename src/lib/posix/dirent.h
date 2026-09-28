#ifndef CP32_LIB_DIRENT_H
#define CP32_LIB_DIRENT_H
#include "../../fs/type.h"
/* Caller-owned, unbuffered MINIX V2 directory stream. Initialize before
 * opening; do not copy an open stream or manipulate its owned descriptor.
 * This is a kernel-linked library interface, not the POSIX DIR/errno ABI. */
struct cp32_dir { int fd; };
#define CP32_DIR_INIT { -1 }
struct cp32_dirent { unsigned inode; char name[15]; };
CP32_IRAM_EXT int cp32_opendir(cp32_disk_reader read,unsigned capacity,
                               const char *path,struct cp32_dir *dir);
/* 1=entry, 0=EOF, negative error. Output is unchanged on EOF/error. */
CP32_IRAM_EXT int cp32_readdir(struct cp32_dir *dir,struct cp32_dirent *entry);
CP32_IRAM_EXT int cp32_closedir(struct cp32_dir *dir);
CP32_IRAM_EXT int cp32_rewinddir(struct cp32_dir *dir);
CP32_IRAM_EXT long cp32_telldir(struct cp32_dir *dir);
/* Byte positions from telldir; aligned positions beyond EOF are permitted. */
CP32_IRAM_EXT int cp32_seekdir(struct cp32_dir *dir,long position);
#endif
