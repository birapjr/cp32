#ifndef CP32_APPLICATION_H
#define CP32_APPLICATION_H
#include "../mm/image.h"
/* Trusted foreground application, one slot; called only by FS. */
typedef void (*cp32_app_output)(const char *,unsigned);
CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,cp32_app_output output,int *status);
#endif
