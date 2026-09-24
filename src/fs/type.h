#ifndef CP32_FS_TYPE_H
#define CP32_FS_TYPE_H
/* Byte-oriented device reader: count/error result, offsets in bytes. */
typedef int (*cp32_disk_reader)(unsigned offset, char *buffer, int count);
#ifndef CP32_IRAM_EXT
#if defined(__XTENSA__)
#define CP32_IRAM_EXT __attribute__((section(".iram_ext.text")))
#else
#define CP32_IRAM_EXT
#endif
#endif
/* Bounded metadata result; not the complete MINIX struct stat ABI. */
struct cp32_minix_stat {
  unsigned inode, mode, links, uid, gid, size, atime, mtime, ctime;
};
#endif
