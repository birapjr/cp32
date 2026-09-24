#ifndef CP32_MM_PROTO_H
#define CP32_MM_PROTO_H
/* Include via mm.h for the shared kernel/message types. */
/* alloc.c */
CP32_IRAM_EXT phys_clicks cp32_mem_alloc(phys_clicks clicks, int owner);
CP32_IRAM_EXT int cp32_mem_free(phys_clicks base, int owner);
CP32_IRAM_EXT int cp32_mem_owned(phys_clicks base, phys_clicks clicks, int owner);
/* main.c */
CP32_IRAM_EXT int cp32_mm_handle_request(message *m);
CP32_IRAM_EXT void mm_task(void);
#endif
