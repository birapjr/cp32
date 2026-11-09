#ifndef CP32_FORK_H
#define CP32_FORK_H
#define CP32_WAIT_LIVE (-1000)
CP32_IRAM_EXT int cp32_fork_disposition(int,unsigned);
CP32_IRAM_EXT int cp32_fork_sigmask(int,unsigned);
CP32_IRAM_EXT unsigned cp32_fork_sigpending(void);
CP32_IRAM_EXT int cp32_fork_signal_target(struct proc *,int,unsigned);
CP32_IRAM_EXT int cp32_fork_live(void);
CP32_IRAM_EXT void cp32_fork_request(message *);
CP32_IRAM_EXT int cp32_fork_handoff(struct proc **,int *,uint32_t *,uint32_t *,const message *);
/* Two nested fork levels with cooperative exchange at either level.
 * Each parent owns eight wait results.
 * Parent image is backed by MM until child exit. */
CP32_IRAM_EXT int cp32_fork_start(struct proc **active,int pid,int *ppid,uint32_t *floor,uint32_t *brk);
CP32_IRAM_EXT int cp32_fork_active(void);
CP32_IRAM_EXT int cp32_fork_pid_used(int pid);
/* signal0 encodes normal exit; fatal signals occupy the low wait-status bits. */
CP32_IRAM_EXT int cp32_fork_finish(struct proc **active,int status,int *ppid,uint32_t *floor,uint32_t *brk,unsigned signal);
CP32_IRAM_EXT int cp32_fork_wait(int pid,void *status,unsigned options);
CP32_IRAM_EXT void cp32_fork_reset(void);
CP32_IRAM_EXT int cp32_process_group(int pid,int create);
#endif
