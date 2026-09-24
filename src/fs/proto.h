#ifndef CP32_FS_PROTO_H
#define CP32_FS_PROTO_H
#include "type.h"
struct cp32_minix_super;
struct cp32_minix_dir;
struct cp32_minix_file;
CP32_IRAM_EXT int cp32_minix_abspath(const char *cwd, const char *path, char *out);
CP32_IRAM_EXT int cp32_minix_chdir(cp32_disk_reader read, unsigned capacity,
                                  char *cwd, const char *path);
/* Internal descriptor inode view; owned by the single FS client. */
CP32_IRAM_EXT const struct cp32_minix_file *cp32_fd_file(int fd);
CP32_IRAM_EXT int cp32_fd_fstat(int fd,struct cp32_minix_stat *out);
/* stadir.c */
CP32_IRAM_EXT int cp32_minix_stat(cp32_disk_reader read, unsigned capacity,
    const char *path, struct cp32_minix_stat *out);
/* cache.c: single-client clean cache; invalidate before any backing write. */
CP32_IRAM_EXT void cp32_cache_invalidate(void);
CP32_IRAM_EXT int cp32_cache_read(cp32_disk_reader read, unsigned capacity,
                                 unsigned offset, char *out, int count);
/* filedes.c: single-client read-only descriptors; not syscall errno ABI. */
CP32_IRAM_EXT int cp32_fd_open(cp32_disk_reader read,unsigned capacity,const char *path);
CP32_IRAM_EXT int cp32_fd_close(int fd);
CP32_IRAM_EXT int cp32_fd_read(int fd,char *out,unsigned count);
CP32_IRAM_EXT int cp32_fd_seek(int fd,long offset,int whence,unsigned *position);
/* filedes.c internal sharing primitive; misc.c duplication operations. */
CP32_IRAM_EXT int cp32_fd_share(int source,int target);
CP32_IRAM_EXT int cp32_fd_dup(int fd);
CP32_IRAM_EXT int cp32_fd_dup2(int fd,int target);
/* super.c */
CP32_IRAM_EXT int cp32_minix_super_read(cp32_disk_reader read, unsigned capacity,
                                       struct cp32_minix_super *out);
CP32_IRAM_EXT int cp32_fs_allocated(cp32_disk_reader read, unsigned map,
                                   unsigned bit, unsigned swap);
/* utility.c */
CP32_IRAM_EXT unsigned cp32_fs_u16(const unsigned char *p, unsigned swap);
CP32_IRAM_EXT unsigned cp32_fs_u32(const unsigned char *p, unsigned swap);
/* inode.c */
CP32_IRAM_EXT int cp32_fs_open_directory(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_dir *dir);
CP32_IRAM_EXT int cp32_fs_file_inode(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_file *file);
/* path.c */
CP32_IRAM_EXT int cp32_fs_resolve_path(cp32_disk_reader read, unsigned capacity,
    const char *path, struct cp32_minix_super *super, unsigned *number);
/* 1=entry, 0=EOF, negative status=error; name receives a NUL terminator. */
CP32_IRAM_EXT int cp32_minix_root_next(struct cp32_minix_dir *dir,
                                      unsigned *inode, char name[15]);
/* open.c */
CP32_IRAM_EXT int cp32_minix_root_open(cp32_disk_reader read, unsigned capacity,
                                      struct cp32_minix_dir *dir);
CP32_IRAM_EXT int cp32_minix_dir_open(cp32_disk_reader read, unsigned capacity,
                                     const char *path, struct cp32_minix_dir *dir);
CP32_IRAM_EXT int cp32_minix_file_open(cp32_disk_reader read, unsigned capacity,
                                      const char *name, struct cp32_minix_file *file);
/* whence: 0=start, 1=current, 2=end; returns 0 or negative status. */
CP32_IRAM_EXT int cp32_minix_file_seek(struct cp32_minix_file *file,
                                      long offset, int whence);
CP32_IRAM_EXT int cp32_fs_read_map(struct cp32_minix_file *file,
                                  unsigned index, unsigned *zone);
/* read.c: up to 64 bytes, zero at EOF, negative status on failure. */
CP32_IRAM_EXT int cp32_minix_file_read(struct cp32_minix_file *file,
                                      char *buffer, unsigned count);
#endif
