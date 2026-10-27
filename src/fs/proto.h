#ifndef CP32_FS_PROTO_H
#define CP32_FS_PROTO_H
#include "type.h"
struct cp32_minix_super;
struct cp32_minix_dir;
struct cp32_minix_file;
CP32_FS_TEXT int cp32_minix_abspath(const char *cwd, const char *path, char *out);
CP32_FS_TEXT int cp32_minix_chdir(cp32_disk_reader read, unsigned capacity,
                                  char *cwd, const char *path);
/* Internal descriptor inode view; owned by the single FS client. */
CP32_FS_TEXT const struct cp32_minix_file *cp32_fd_file(int fd);
CP32_FS_TEXT int cp32_fd_fstat(int fd,struct cp32_minix_stat *out);
CP32_FS_TEXT int cp32_fs_stat_inode(cp32_disk_reader,const struct cp32_minix_super *,unsigned,struct cp32_minix_stat *);
CP32_FS_TEXT int cp32_fs_truncate_inode(cp32_disk_reader,cp32_disk_writer,unsigned,unsigned,unsigned);
CP32_FS_TEXT int cp32_fs_write_inode(cp32_disk_reader,cp32_disk_writer,const struct cp32_minix_super *,unsigned,unsigned,const char *,unsigned);
CP32_FS_TEXT int cp32_fd_open_mode(cp32_disk_reader,cp32_disk_writer,unsigned,const char *,unsigned,unsigned);
CP32_FS_TEXT int cp32_fd_write(int,const char *,unsigned);
/* stadir.c */
CP32_FS_TEXT int cp32_minix_stat(cp32_disk_reader read, unsigned capacity,
    const char *path, struct cp32_minix_stat *out);
/* cache.c: single-client clean cache; invalidate before any backing write. */
CP32_FS_TEXT void cp32_cache_invalidate(void);
CP32_FS_TEXT int cp32_cache_read(cp32_disk_reader read, unsigned capacity,
                                 unsigned offset, char *out, int count);
/* filedes.c: single-client read-only descriptors; not syscall errno ABI. */
CP32_FS_TEXT int cp32_fd_open(cp32_disk_reader read,unsigned capacity,const char *path);
CP32_FS_TEXT int cp32_fd_close(int fd);
CP32_FS_TEXT int cp32_fd_read(int fd,char *out,unsigned count);
/* 1=entry, 0=EOF, negative status; shares the descriptor's byte offset. */
CP32_FS_TEXT int cp32_fd_readdir(int fd,unsigned *inode,char name[15]);
CP32_FS_TEXT int cp32_fd_seek(int fd,long offset,int whence,unsigned *position);
/* filedes.c internal sharing primitive; misc.c duplication operations. */
CP32_FS_TEXT int cp32_fd_share(int source,int target);
CP32_FS_TEXT int cp32_fd_dup(int fd);
CP32_FS_TEXT int cp32_fd_dup2(int fd,int target);
/* MINIX descriptor/status controls; flags stored, exec/locking not ported. */
CP32_FS_TEXT int cp32_fd_fcntl(int fd,int command,long argument);
/* super.c */
CP32_FS_TEXT int cp32_minix_super_read(cp32_disk_reader read, unsigned capacity,
                                       struct cp32_minix_super *out);
CP32_FS_TEXT int cp32_fs_allocated(cp32_disk_reader read, unsigned map,
                                   unsigned bit, unsigned swap);
/* utility.c */
CP32_FS_TEXT unsigned cp32_fs_u16(const unsigned char *p, unsigned swap);
CP32_FS_TEXT unsigned cp32_fs_u32(const unsigned char *p, unsigned swap);
/* inode.c */
CP32_FS_TEXT int cp32_fs_open_directory(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_dir *dir);
CP32_FS_TEXT int cp32_fs_file_inode(cp32_disk_reader read,
    const struct cp32_minix_super *super, unsigned number, struct cp32_minix_file *file);
/* path.c */
CP32_FS_TEXT int cp32_fs_resolve_path(cp32_disk_reader read, unsigned capacity,
    const char *path, struct cp32_minix_super *super, unsigned *number);
/* 1=entry, 0=EOF, negative status=error; name receives a NUL terminator. */
CP32_FS_TEXT int cp32_minix_root_next(struct cp32_minix_dir *dir,
                                      unsigned *inode, char name[15]);
/* open.c */
CP32_FS_TEXT int cp32_minix_root_open(cp32_disk_reader read, unsigned capacity,
                                      struct cp32_minix_dir *dir);
CP32_FS_TEXT int cp32_minix_dir_open(cp32_disk_reader read, unsigned capacity,
                                     const char *path, struct cp32_minix_dir *dir);
CP32_FS_TEXT int cp32_minix_file_open(cp32_disk_reader read, unsigned capacity,
                                      const char *name, struct cp32_minix_file *file);
/* whence: 0=start, 1=current, 2=end; returns 0 or negative status. */
CP32_FS_TEXT int cp32_minix_file_seek(struct cp32_minix_file *file,
                                      long offset, int whence);
CP32_FS_TEXT int cp32_fs_read_map(struct cp32_minix_file *file,
                                  unsigned index, unsigned *zone);
/* Trusted-shell chmod: numeric rwx bits only, no credential enforcement. */
CP32_FS_TEXT int cp32_minix_chmod(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,unsigned);
/* Rename regular files to unused names in one-block directories. */
CP32_FS_TEXT int cp32_minix_rename(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,const char *);
/* Empty one-block directories; cwd must identify the current working directory. */
CP32_FS_TEXT int cp32_minix_rmdir(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,const char *);
CP32_FS_TEXT int cp32_fd_inode_open(cp32_disk_reader,unsigned);
CP32_FS_TEXT int cp32_fd_defer_unlink(cp32_disk_reader,cp32_disk_writer,unsigned);
CP32_FS_TEXT int cp32_fs_reap_inode(cp32_disk_reader,cp32_disk_writer,const struct cp32_minix_super *,unsigned);
/* Unlink regular names; open inodes survive until their final close. */
CP32_FS_TEXT int cp32_minix_unlink(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *);
CP32_FS_TEXT int cp32_minix_copy(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,const char *);
/* Add a name in an existing parent for a regular inode; no allocation. */
CP32_FS_TEXT int cp32_minix_link(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,const char *);
CP32_FS_TEXT int cp32_minix_link_root(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,const char *);
/* Shrink and reclaim direct/single-indirect mappings; synchronous writer.
 * On I/O failure, shorter EOF may be visible and reservations may leak. */
CP32_FS_TEXT int cp32_minix_truncate(cp32_disk_reader,cp32_disk_writer,
                                    unsigned,const char *,unsigned);
/* Append 1..64 bytes to non-executable data files, one-block zones,
 * direct/single-indirect growth up to263KiB. Synchronous data before size. */
CP32_FS_TEXT int cp32_minix_append(cp32_disk_reader,cp32_disk_writer,
                                  unsigned,const char *,const char *,unsigned);
/* Create directory0755, initialized dot entries and parent link count. */
CP32_FS_TEXT int cp32_minix_mkdir(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *);
/* Create a regular file in an existing one-block parent, absolute/root-relative
 * path. No overwrite; 1..64 bytes; verified synchronous publication. */
CP32_FS_TEXT int cp32_minix_create(cp32_disk_reader,cp32_disk_writer,
    unsigned,const char *,const char *,unsigned);
/* Create-only root entry, 1..14-byte name and 0..64 bytes of initial data.
 * One-block zones and existing root directory block; no replacement/growth.
 * Synchronous writer required. Failed writes may leak reserved resources. */
CP32_FS_TEXT int cp32_minix_create_root(cp32_disk_reader,cp32_disk_writer,
                                       unsigned,const char *,const char *,unsigned);
/* Privileged maintenance patch: existing non-executable regular file,
 * 1..64 bytes within EOF, split at sector boundaries; no allocation or metadata updates.
 * Writer must complete synchronously; failures can leave media changed. */
CP32_FS_TEXT int cp32_minix_patch(cp32_disk_reader,cp32_disk_writer,unsigned,
                                  const char *,unsigned,const char *,unsigned);
/* read.c: up to 64 bytes, zero at EOF, negative status on failure. */
CP32_FS_TEXT int cp32_minix_file_read(struct cp32_minix_file *file,
                                      char *buffer, unsigned count);
CP32_FS_TEXT int cp32_minix_mkdir_mode(cp32_disk_reader,cp32_disk_writer,unsigned,const char *,unsigned);
CP32_FS_TEXT int cp32_fs_resize_inode(cp32_disk_reader,cp32_disk_writer,unsigned,unsigned,unsigned);
CP32_FS_TEXT int cp32_fd_truncate(int,unsigned);
CP32_FS_TEXT int cp32_fs_chmod_inode(cp32_disk_reader,cp32_disk_writer,const struct cp32_minix_super *,unsigned,unsigned);
CP32_FS_TEXT int cp32_fd_pread(int,char *,unsigned,unsigned);
CP32_FS_TEXT int cp32_fd_pwrite(int,const char *,unsigned,unsigned);
CP32_FS_TEXT int cp32_minix_space_read(cp32_disk_reader,unsigned,struct cp32_minix_space *);

CP32_FS_TEXT int cp32_minix_move(cp32_disk_reader,cp32_disk_writer,unsigned,const char *,const char *);
CP32_FS_TEXT void cp32_fs_time_bind(cp32_disk_reader,cp32_disk_writer,int (*)(unsigned *));
CP32_FS_TEXT int cp32_fs_stamp(cp32_disk_reader,unsigned char *,unsigned,unsigned);
CP32_FS_TEXT int cp32_fs_data_time(struct cp32_minix_file *,unsigned);
#endif
