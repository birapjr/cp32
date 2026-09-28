#ifndef CP32_MM_IMAGE_H
#define CP32_MM_IMAGE_H
#include "exec.h"
/* Fixed trusted-application slot. Instruction alias offset is 0x6f0000,
 * matching the kernel linker's existing D/IRAM mapping. */
#define CP32_APP_TEXT 0x403d8000U
#define CP32_APP_TEXT_END 0x403dc000U
#define CP32_APP_DATA 0x3fcec000U
#define CP32_APP_STACK 0x3fcef000U
#define CP32_APP_TOP 0x3fcf0000U
struct cp32_image_segment { uint32_t offset, address, filesz, memsz; };
struct cp32_image { uint32_t entry; struct cp32_image_segment text, data; };
/* Reader: 0 on exact read, negative on failure. No writes to target memory.
 * Output remains unchanged on failure; all metadata is decoded bytewise.
 * v1 permits exactly two PT_LOAD entries, RX then RW; no relocations/dynamic
 * linking. This validator does not prove that code obeys the call0 ABI. */
typedef int (*cp32_image_reader)(void *,uint32_t,unsigned char *,unsigned);
CP32_IRAM_EXT int cp32_image_read(cp32_image_reader read,void *context,
                                uint32_t size,struct cp32_image *out);
/* Inactive-slot staging only. Caller supplies disjoint writable buffers
 * (16K code, 12K data) and guarantees source stability/nonaliasing. No MMIO
 * or instruction synchronization occurs here. Validation failure preserves
 * buffers; payload failure clears both regions. *out publishes only success.
 * Never use on an executing application: this is not transactional exec. */
CP32_IRAM_EXT int cp32_image_load(cp32_image_reader read,void *context,
    uint32_t size,unsigned char *text,unsigned char *data,struct cp32_image *out);
#endif
