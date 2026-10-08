#ifndef CP32_APPLICATION_H
#define CP32_APPLICATION_H
#include "../mm/image.h"
/* Trusted foreground application, one slot; called only by FS. */
typedef void (*cp32_app_output)(const char *,unsigned);
CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,unsigned argc,const char *const argv[],cp32_app_output output,int *status);
/* FS-owned foreground console bridge; returns byte count or error. */
CP32_IRAM_EXT int cp32_shell_app_input(char *buffer,unsigned count);
/* Foreground child owns a separate bounded fd namespace; FS context only. */
CP32_IRAM_EXT void cp32_shell_app_files_reset(void);
CP32_IRAM_EXT int cp32_shell_app_file(unsigned op,int fd,void *buffer,unsigned arg);
#endif
