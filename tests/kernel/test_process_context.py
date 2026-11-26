"""Stable production FS/lifecycle records across every historical fork exchange."""
from pathlib import Path
import subprocess,sys,tempfile
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
# Reuse the production scenarios, but additionally snapshot every process's
# ownership addresses across each exchange. The old copy-and-swap design fails.
compat=f.compat.replace('int r=cp32_fork_handoff(p,m);',r'''
struct cp32_fproc *files[NR_PROCS];struct cp32_fork_state *states[NR_PROCS];
unsigned backing[NR_PROCS];
for(int n=3;n<NR_PROCS;n++) {
 files[n]=procs[n].p_files;
 states[n]=files[n] ? files[n]->lifecycle:0;
 backing[n]=states[n] ? states[n]->backing:0;
}
int r=cp32_fork_handoff(p,m);
for(int n=3;n<NR_PROCS;n++) {
 assert(files[n]==procs[n].p_files);
 if(files[n]) {
  assert(states[n]==files[n]->lifecycle && states[n]->owner==procs+n);
  assert(files[n]->pid==(unsigned)procs[n].p_pid);
  assert(states[n]->backing==backing[n] && backing[n]);
  assert(!procs[n].p_shadow || procs[n].p_shadow==backing[n]);
 }
}
assert(cp32_active_files==(*p)->p_files && active_state==(*p)->p_files->lifecycle);
''')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(f.pre+f.proc+f.fs+f.bridge+f.source+compat+f.historic(f.test).replace(' puts("fork:', ''' cp32_fork_reset();assert(!root_inuse);
 alloc_fail=1;assert(cp32_fork_bind(procs+3)==-12 && !root_inuse);alloc_fail=0;
 window_busy=1;assert(cp32_fork_bind(procs+3)==-16 && !root_inuse);window_busy=0;
 assert(!cp32_fork_bind(procs+3) && root_inuse);
 window_busy=1;assert(cp32_fork_start(&active,2000)==-16);
 assert(cp32_fork_handoff(&active,&(message){0})==-16);window_busy=0;
 cp32_fork_reset();assert(!root_inuse && !allocated);
 puts("fork:'''))
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('process contexts: stable FS/wait owners, nested exchange, rollback and slot reuse PASS')
