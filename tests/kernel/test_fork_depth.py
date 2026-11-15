"""Production fork/MM/FS helpers with five shadow images and resource failures."""
from pathlib import Path
import subprocess,tempfile,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
pre=f.pre[:f.pre.index('static int _sendrec')]
pre=pre.replace('#define NR_PROCS 12','#define NR_PROCS 9').replace('staging[2]','staging[5]').replace('inuse[2]','inuse[5]').replace('base<=124','base<=127')
pre=pre.replace('int m_type,m_source,m1_i1;', 'int m_type,m_source,m1_i1;')
pre+=r'''
static int budget;
static int _sendrec(int nr,message *m){
 assert(nr==MM_PROC_NR && !locked);m->m_source=nr;
 if(m->m_type==CP32_MM_ALLOCATE){
  assert((unsigned)m->m1_i1*256<=sizeof(staging[0]));
  if(allocated==budget || alloc_fail){m->m_type=-12;m->m1_i1=0;return 0;}
  int i;for(i=0;i<5 && inuse[i];i++) {}assert(i<5);
  m->m_type=0;m->m1_i1=123+i;inuse[i]=1;allocated++;allocs++;
 }else{
  int i=m->m1_i1-123;assert(i>=0 && i<5 && inuse[i] && m->m_type==CP32_MM_RELEASE);
  inuse[i]=0;allocated--;frees++;m->m_type=0;
 }return 0;
}
'''
setup=f.test[:f.test.index('int main(void)')]
test=r'''
int main(void){
 for(budget=1;budget<=5;budget++)for(int repeat=0;repeat<3;repeat++) {
  setup();cp32_fork_bind(procs+3);
  struct proc *active=procs+3;int pp=1,depth=0;uint32_t floor=16,brk=32;
  message request={2007,0,45};memset(live,0x40,sizeof(live));
  assert(!cp32_fork_sigmask(0,1U<<14));
  while(depth<budget){
   alloc_fail=1;assert(cp32_fork_start(&active,101+depth,&pp,&floor,&brk)==-12);alloc_fail=0;
   fail_dup=1;assert(cp32_fork_start(&active,101+depth,&pp,&floor,&brk)==-24);fail_dup=0;
   assert(allocated==depth && refs==depth+1 && live[0]==0x40+depth);
   assert(!cp32_fork_start(&active,101+depth,&pp,&floor,&brk));depth++;
   active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
   assert(active->p_pid==100+depth && cp32_app_state.pid==(unsigned)active->p_pid);
   memset(live,0x40+depth,sizeof(live));floor=16+depth;brk=32+depth;
   cp32_app_state.mask=depth;active->user_time=1;
  }
  assert(cp32_fork_start(&active,101+depth,&pp,&floor,&brk)==(budget==5 ? -11:-12));
  assert(allocated==depth && refs==depth+1 && live[0]==0x40+depth);
  assert(!cp32_fork_signal_target(active,-1,15));
  /* Exchange at every depth, then finish and collect each private result. */
  while(depth){
   assert(cp32_fork_sigpending()==1U<<14 && !cp32_fork_signal_ready(0));
   assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==100+depth);
   assert(active->p_pid==99+depth && live[0]==0x3f+depth && floor==15U+depth);
   int childpid=100+depth;
   assert(cp32_fork_wait(childpid,0,1)==0 && cp32_fork_wait(childpid,0,0)==CP32_WAIT_LIVE);
   /* A parked child still prevents another fork, without allocating. */
   assert(cp32_fork_start(&active,9000,&pp,&floor,&brk)==-11);
   assert(!cp32_fork_handoff(&active,&pp,&floor,&brk,&request));
   assert(active->p_pid==childpid && live[0]==0x40+depth && floor==16U+depth);
   assert(cp32_fork_finish(&active,depth,&pp,&floor,&brk,0)==childpid);
   assert(cp32_fork_wait(childpid,0,0)==childpid && cp32_fork_wait(childpid,0,0)==-10);
   depth--;assert(allocated==depth && refs==depth+1 && live[0]==0x40+depth);
  }
  assert(active==procs+3 && active->child_utime==(unsigned)budget && allocs==frees);
  assert(!cp32_fproc_suspended && !cp32_fork_active() && !cp32_fork_live());
  cp32_fproc_exit(&cp32_app_state);assert(!refs);
 }
 puts("fork depth: five levels, every-level exchange, ENOMEM/EAGAIN, private images, accounting and repeated cleanup PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+f.proc+f.fs+f.bridge+f.source+f.compat+setup+f.historic(test))
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
