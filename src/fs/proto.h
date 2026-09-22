#ifndef CP32_FS_PROTO_H
#define CP32_FS_PROTO_H
#include "type.h"
struct cp32_minix_super;
struct cp32_minix_dir;
struct cp32_minix_file;
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
/* read.c: up to 64 bytes, zero at EOF, negative status on failure. */
CP32_IRAM_EXT int cp32_minix_file_read(struct cp32_minix_file *file,
                                      char *buffer, unsigned count);
#endif
