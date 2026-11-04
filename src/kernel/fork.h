#ifndef CP32_FORK_H
#define CP32_FORK_H
/* One executing child-first fork; up to eight exited children await waitpid.
 * Parent image is backed by MM until child exit. */
CP32_IRAM_EXT int cp32_fork_start(struct proc **active,int pid,int *ppid,uint32_t *floor,uint32_t *brk);
CP32_IRAM_EXT int cp32_fork_active(void);
CP32_IRAM_EXT int cp32_fork_pid_used(int pid);
CP32_IRAM_EXT int cp32_fork_finish(struct proc **active,int status,int *ppid,uint32_t *floor,uint32_t *brk);
CP32_IRAM_EXT int cp32_fork_wait(int pid,void *status,unsigned options);
CP32_IRAM_EXT void cp32_fork_reset(void);
#endif
