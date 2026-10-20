#ifndef CP32_APP_UPDATE_H
#define CP32_APP_UPDATE_H
#include "../fs/fs.h"
struct cp32_update_entry {const char *path;const unsigned char *bytes;unsigned size;};
/* Synchronous install; caller reports each entry and stops at the first error. */
CP32_IRAM_EXT int cp32_update_one(cp32_disk_reader,cp32_disk_writer,unsigned,
                               const struct cp32_update_entry *);
#endif
