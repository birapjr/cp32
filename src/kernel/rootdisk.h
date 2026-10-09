#ifndef CP32_ROOTDISK_H
#define CP32_ROOTDISK_H
#include "ramdisk.h" /* shared placement annotation; legacy optional backend */
#ifndef CP32_ROOT_RAM
#define CP32_ROOT_RAM 0
#endif
#if CP32_ROOT_RAM
#define cp32_root_init cp32_minix_demo_init
#define cp32_root_capacity cp32_ramdisk_capacity
#define cp32_root_read cp32_ramdisk_read_bytes
#define cp32_root_write cp32_ramdisk_write_bytes
#define cp32_root_writable() 1
#define CP32_ROOT_NAME "ram"
#else
#define CP32_ROOT_NAME "sd"
#define cp32_root_writable() 0
CP32_IRAM_EXT int cp32_root_init(void);
CP32_IRAM_EXT unsigned cp32_root_capacity(void);
CP32_IRAM_EXT int cp32_root_read(unsigned,void *,unsigned);
CP32_IRAM_EXT int cp32_root_write(unsigned,const void *,unsigned);
#endif
#endif
