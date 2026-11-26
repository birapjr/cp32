"""Production IPC copies to/from nonresident application images."""
from pathlib import Path
import subprocess,sys,tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
code='\n'.join(extract_function(root/'src/kernel/system.c',n) for n in ['numap','umap','mem_copy'])
code+='\n'+extract_function(root/'src/kernel/proc.c','copy_message')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHIP 4
#define ESP32_S3 4
#define CP32_IRAM_EXT
#define PUBLIC
#define PRIVATE static
#define NR_SEGS 3
#define NR_PROCS 8
#define NR_TASKS 1
#define LOW_USER 2
#define CLICK_SHIFT 8
#define P_SLOT_FREE 1
#define P_ZOMBIE 0200
#define FS_PROC_NR 1
#define MM_PROC_NR 0
#define CP32_APP_DATA 0x3fcec000U
#define CP32_APP_TOP 0x3fcf0000U
#define OK 0
#define EFAULT -14
#define NIL_PROC ((struct proc *)0)
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
typedef struct {int m_source,m_type,data[6];} message;
#define MESS_SIZE sizeof(message)
struct map {unsigned mem_vir,mem_phys,mem_len;};
struct proc {int p_nr,p_flags;unsigned p_shadow;struct map p_map[3];};
static struct proc procs[NR_PROCS+NR_TASKS];
#define proc_addr(n) (procs+((n)+NR_TASKS))
#define BEG_PROC_ADDR procs
#define END_PROC_ADDR (procs+NR_PROCS+NR_TASKS)
#define isokprocn(n) ((n)>=-NR_TASKS && (n)<NR_PROCS)
#define istaskp(p) ((p)->p_nr<0)
static unsigned char live[32768],first[32768],second[32768],server[512];
static unsigned backing_a=0x3fc900,backing_b=0x3fca00;
static int a_owner=FS_PROC_NR,b_owner=MM_PROC_NR;
static int cp32_mem_owned(unsigned base,unsigned clicks,int owner){
 assert(clicks==128);
 return (base==backing_a && owner==a_owner) || (base==backing_b && owner==b_owner);
}
static int lock_depth;
static int lock_save(void){return lock_depth++;}
static void restore_lock(int old){assert(lock_depth==old+1);lock_depth=old;}
static void *physical(uintptr_t p,unsigned n){
 if(p>=0x3fce8000 && p<=0x3fcf0000 && n<=0x3fcf0000-p)return live+(p-0x3fce8000);
 if(p>=0x3fc90000 && p<=0x3fc98000 && n<=0x3fc98000-p)return first+(p-0x3fc90000);
 if(p>=0x3fca0000 && p<=0x3fca8000 && n<=0x3fca8000-p)return second+(p-0x3fca0000);
 if(p>=0x3fcb0000 && p<=0x3fcb0200 && n<=0x3fcb0200-p)return server+(p-0x3fcb0000);
 assert(p>UINT32_MAX);return (void *)p;
}
static void phys_copy(phys_bytes src,phys_bytes dst,unsigned n){assert(lock_depth);memmove(physical(dst,n),physical(src,n),n);}
static void usbj_print(const char *s){(void)s;}
static void usbj_print_u32(unsigned n){(void)n;}
'''
test=r'''
int main(void){
 for(int n=-1;n<NR_PROCS;n++){proc_addr(n)->p_nr=n;proc_addr(n)->p_flags=P_SLOT_FREE;}
 struct proc *a=proc_addr(3),*b=proc_addr(4),*c=proc_addr(5),*fs=proc_addr(1);
 a->p_flags=b->p_flags=c->p_flags=fs->p_flags=0;
 for(int n=3;n<=5;n++)proc_addr(n)->p_map[1]=(struct map){CP32_APP_DATA,CP32_APP_DATA>>8,64};
 fs->p_map[1]=(struct map){0x3fcb0000,0x3fcb00,2};
 a->p_shadow=backing_a;b->p_shadow=backing_b;
 const uintptr_t v=0x3fcefe00;unsigned at=v-0x3fce8000;
 message sent={999,777,{1,2,3,4,5,6}},read;
 memcpy(first+at,&sent,sizeof(sent));memset(live,0x55,sizeof(live));memset(second,0x66,sizeof(second));
 assert(numap(3,v,MESS_SIZE)==0x3fc90000+at);
 assert(umap(a,1,v,MESS_SIZE)==0x3fc90000+at);
 assert(!umap(a,-1,v,1) && !umap(a,3,v,1) && !umap(0,1,v,1));
 assert(numap(4,v,MESS_SIZE)==0x3fca0000+at);
 assert(numap(5,v,MESS_SIZE)==v);
 /* Same virtual source/destination address, distinct image owners. */
 int saved=lock_save();assert(!copy_message(a,(message *)v,b,(message *)v));restore_lock(saved);
 memcpy(&read,second+at,sizeof(read));assert(read.m_source==3 && read.m_type==777 && read.data[5]==6);
 assert(!memcmp(first+at,&sent,sizeof(sent)) && live[at]==0x55 && !lock_depth);
 /* Server reply to parked process, then parked sender to the server. */
 sent.m_type=888;memcpy(server,&sent,sizeof(sent));saved=lock_save();
 assert(!copy_message(fs,(message *)0x3fcb0000,a,(message *)v));
 assert(!copy_message(a,(message *)v,fs,(message *)0x3fcb0040));restore_lock(saved);
 memcpy(&read,first+at,sizeof(read));assert(read.m_source==1 && read.m_type==888);
 memcpy(&read,server+64,sizeof(read));assert(read.m_source==3 && read.m_type==888);
 /* Ownership transitions select the new backing; resident sibling unchanged. */
 a->p_shadow=0;b->p_shadow=backing_a;
 assert(numap(3,v,MESS_SIZE)==v && numap(4,v,MESS_SIZE)==0x3fc90000+at);
 assert(!numap(4,CP32_APP_TOP-4,8));assert(!numap(4,CP32_APP_DATA-1,1));
 assert(!numap(4,UINTPTR_MAX-3,8));assert(!numap(4,v,0));
 b->p_flags=P_ZOMBIE;assert(!numap(4,v,1));b->p_flags=0;
 b->p_map[1].mem_phys++;assert(!numap(4,v,1));b->p_map[1].mem_phys--;
 a_owner=7;assert(!numap(4,v,1));a_owner=FS_PROC_NR;
 backing_a=0;assert(mem_copy(4,v,1,0x3fcb0000,1)==EFAULT && !lock_depth);
 assert(!numap(99,v,1));
 puts("image IPC: saved sender/receiver, trusted source, resident isolation, ownership and range errors PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+code+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
