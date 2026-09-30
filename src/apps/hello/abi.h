#ifndef CP32_APP_ABI_H
#define CP32_APP_ABI_H
/* Bootstrap ABI v5 (earlier prefixes preserved): trusted call0 callbacks, no privilege boundary.
 * Loader supplies this table in a2; stack is built by cp32_exec_stack.
 * write returns bytes written or a negative error; exit must not return.
 * The kernel bridge supports one foreground process with write/exit/getpid/getppid IPC.
 * v2 appends getpid; v3 appends getppid after the v2 fields. */
struct cp32_app_services {
  unsigned version;
  int (*write)(const char *,unsigned);
  void (*exit)(int);
  int (*getpid)(void);
  int (*getppid)(void); /* v3: launching shell PID */
  void *(*sbrk)(int); /* v4: old break, or (void *)-1; zero-filled growth */
  int (*read)(char *,unsigned); /* v5: canonical console input, at most 64 bytes */
};
#endif
