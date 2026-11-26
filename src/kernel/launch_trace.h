#ifndef CP32_LAUNCH_TRACE_H
#define CP32_LAUNCH_TRACE_H

enum cp32_launch_step {
  LAUNCH_LOAD, LAUNCH_ACCEPT, LAUNCH_ALLOC, LAUNCH_SLOT, LAUNCH_FILES,
  LAUNCH_PUBLISH, LAUNCH_REPLY, LAUNCH_FS_RESUME, LAUNCH_RETURN,
  LAUNCH_FORK, LAUNCH_SWAP_TICKS, LAUNCH_SIGNAL, LAUNCH_SIGRETURN,
  LAUNCH_EXIT, LAUNCH_FS_DROP, LAUNCH_DONE
};

void cp32_trace_launch(enum cp32_launch_step step, unsigned value);
void cp32_trace_launch_tick(void);
void cp32_trace_swap_begin(void);
void cp32_trace_swap_end(void);

#endif
