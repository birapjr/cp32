#ifndef CP32_APP_ABI_H
#define CP32_APP_ABI_H
/* Bootstrap ABI v1: trusted call0 callbacks, no privilege boundary.
 * Loader supplies this table in a2; stack is built by cp32_exec_stack.
 * write returns bytes written or a negative error; exit must not return.
 * Kernel bridge and process lifecycle are not implemented yet. */
struct cp32_app_services {
  unsigned version;
  int (*write)(const char *,unsigned);
  void (*exit)(int);
};
#endif
