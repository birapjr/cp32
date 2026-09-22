#!/usr/bin/env python3
"""Production TTY driver/line discipline with modeled keyboard, memory and IPC."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/kernel/tty.c').read_text()
def extract(name):
    m=re.search(r'^CP32_IRAM_EXT (?:PUBLIC|PRIVATE|static) [^\n]*\b'+name+r'\(',source,re.M)
    assert m, name
    opening=source.index('{',m.start())
    depth=0
    for t in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'+r"'(?:\\.|[^'\\])*'|[{}]",source[opening:],re.S):
        if t.group()=='{': depth+=1
        elif t.group()=='}':
            depth-=1
            if depth==0:return source[m.start():opening+t.end()]
names=['in_transfer','do_read','handle_events','in_process','echo','rawecho',
       'back_over','reprint','out_process','cp32_console_echo','cp32_console_read',
       'cp32_tty_poll_tick','tty_init','scr_init','cp32_cardputer_key']
bodies=[extract(n) for n in names]
# Preserve actual defaults and actual header indexes/layout in the harness.
defaults=re.search(r'PRIVATE struct termios termios_defaults = \{.*?\n\};',source,re.S).group()
prelude=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define PUBLIC
#define EXTERN
#define _PROTOTYPE(f,a) f a
#define NR_CONS 1
#define NR_RS_LINES 0
#define NR_PTYS 0
#define TRUE 1
#define FALSE 0
#define BYTE 255
#define SIGINT 2
#define SIGQUIT 3
#define vir2phys(p) ((uintptr_t)(p))
typedef uintptr_t vir_bytes;
typedef uintptr_t phys_bytes;
typedef unsigned short u16_t;
typedef long clock_t;
struct tty;
struct winsize {unsigned short ws_row,ws_col,ws_xpixel,ws_ypixel;};
'''+f'#include "{ROOT}/src/include/sys/termios.h"\n#include "{ROOT}/src/kernel/tty.h"\n'+r'''
#define tty_addr(n) (tty_table+(n))
enum {OK=0,EFAULT=-14,EINVAL=-22,EIO=-5,EAGAIN=-11,SUSPEND=-998,TASK_REPLY=100,REVIVE=101,TTY=-9};
typedef struct {int m_source,PROC_NR,COUNT,TTY_FLAGS; char *ADDRESS;} message;
static volatile int cp32_console_ready;
static int notifications,depth,used,replies,types[8],statuses[8],mask;
static unsigned char events[64];
static unsigned event_count,event_pos;
static char output[4096], destination[130];
static int allowed;
static void tty_reply(int type,int caller,int proc,int status) {
 assert(caller==1 && proc==1); types[replies]=type; statuses[replies++]=status;
}
static phys_bytes numap(int proc,vir_bytes address,unsigned n) {
 if(proc!=1 || address<0x1000 || address-0x1000>(unsigned)allowed ||
    n>(unsigned)allowed-(address-0x1000)) return 0;
 return (uintptr_t)(destination+1+address-0x1000);
}
static void phys_copy(phys_bytes a,phys_bytes b,unsigned n) {memcpy((void*)b,(void*)a,n);}
static void lock(void) {mask++;}
static void unlock(void) {assert(mask);mask--;}
static void interrupt(int task) {assert(task==TTY);notifications++;}
static int cardputer_keyboard_interrupt_asserted(void) {return event_pos<event_count;}
static int cp32_read_keyboard_event(unsigned char *e) {*e=events[event_pos++]; return 1;}
static void cardputer_display_begin_batch(void) {depth++;}
static void cardputer_display_end_batch(void) {assert(depth);depth--;}
static void cardputer_display_putc(char c) {assert(depth);output[used++]=c;}
static void cardputer_display_defer_newline(void) {
  assert(!depth); output[used++]='\r'; output[used++]='\n';
}
static void settimer(tty_t *tp,int on) {(void)tp;(void)on;}
static void sigchar(tty_t *tp,int sig) {(void)tp;(void)sig;assert(!"unexpected signal");}
static void dev_ioctl(tty_t *tp) {(void)tp;assert(!"unexpected ioctl");}
static void cp32_trace_tty_read(unsigned count) {(void)count;}
static void tty_devnop(tty_t *tp) {(void)tp;}
static void cp32_console_write(tty_t *tp) {(void)tp;}
static void rs_init(tty_t *tp) {(void)tp;}
#define pty_init(tp) ((void)(tp))
'''
prototypes='\n'.join(' '.join(re.match(r'(?:CP32_IRAM_EXT )?(PUBLIC|PRIVATE|static) (\w+) (\w+)\(',b).groups())+'();' for b in bodies)
prototypes=prototypes.replace('static int cp32_cardputer_key();', 'static int cp32_cardputer_key(unsigned char, char *);')
tests=r'''
static tty_t *reset(int count) {
 tty_t *t=tty_table; memset(t,0,sizeof(*t)); tty_init(t);
 notifications=depth=used=replies=event_pos=event_count=mask=0;
 allowed=count; memset(destination,0x55,sizeof(destination));
 return t;
}
static void key(char ch) {
 // Exercise the production Cardputer matrix decoder, including physical BS.
 for(unsigned e=1;e<81;e++) {
  unsigned c=e-1,col=(c/10)*2+(c%10>3),row=(c%10+4)%4;
  static const char keys[4][15]={"`1234567890-=\b","\tqwertyuiop[]\\","\1\2asdfghjkl;'\n","\3\4\5zxcvbnm,./ "};
  if(c/10<7 && c%10<8 && col<14 && keys[row][col]==ch) {events[event_count++]=e|0x80;return;}
 }
 assert(!"missing key");
}
static void input(tty_t *t,char *s) {assert(in_process(t,s,strlen(s))==(int)strlen(s));handle_events(t);}
static void read_request(tty_t *t,int count,int flags) {
 message m={1,1,count,flags,(char*)0x1000};do_read(t,&m);
}
int main(void) {
 tty_t *t=reset(64);
 assert(t->tty_termios.c_cc[VEOF]==4 && t->tty_termios.c_cc[VMIN]==1);
 assert(t->tty_termios.c_cc[VTIME]==0 && t->tty_termios.c_cc[VKILL]==21);
 assert(t->tty_termios.c_cc[VINTR]==3 && t->tty_termios.c_cc[VERASE]==127);
 cp32_console_ready=0; cp32_tty_poll_tick(); assert(!notifications);
 cp32_console_ready=1; cp32_tty_poll_tick();assert(notifications==1 && t->tty_events);
 read_request(t,64,0); assert(replies==1 && statuses[0]==SUSPEND);
 key('l');key('x');key('\b');key('s');handle_events(t);
 assert(replies==1 && t->tty_incount==2 && !t->tty_eotct);
 key('\n');cp32_tty_poll_tick();handle_events(t);
 assert(replies==2 && types[1]==REVIVE && statuses[1]==3);
 assert(!memcmp(destination+1,"ls\n",3) && destination[4]==0x55);
 assert(!memcmp(output,"lx\b \bs\r\n",8) && used==8 && !depth);
 t=reset(64);input(t,"sys\nmm\n");read_request(t,64,0);
 assert(replies==1 && types[0]==TASK_REPLY && statuses[0]==4);
 assert(!memcmp(destination+1,"sys\n",4));read_request(t,64,0);
 assert(replies==2 && statuses[1]==3 && !memcmp(destination+1,"mm\n",3));
 t=reset(64);read_request(t,64,O_NONBLOCK);
 assert(replies==1 && statuses[0]==EAGAIN && !t->tty_inleft);
 t=reset(64);read_request(t,64,0);input(t,"bad\025ok\n");
 assert(statuses[1]==3 && !memcmp(destination+1,"ok\n",3));
 t=reset(64);read_request(t,64,0);input(t,"\004");
 assert(replies==2 && types[1]==REVIVE && statuses[1]==0);
 t=reset(1);t->tty_termios.c_lflag=0;read_request(t,1,0);key('\b');handle_events(t);
 assert(replies==2 && statuses[1]==1 && destination[1]=='\b');
 t=reset(64);for(int i=0;i<10;i++)key('a');cp32_console_read(t);
 assert(event_pos==8 && t->tty_incount==8);cp32_console_read(t);assert(event_pos==10);
 t=reset(64);read_request(t,0,0);assert(statuses[0]==EINVAL);
 t=reset(3);read_request(t,4,0);assert(statuses[0]==EFAULT);
 t=reset(64);read_request(t,64,0);read_request(t,64,0);assert(statuses[1]==EIO);
 puts("TTY device read: canonical edit/echo, raw, immediate/SUSPEND/REVIVE, FIFO bound, defaults and errors passed");
}
'''
with tempfile.TemporaryDirectory(prefix='cp32-tty-device-') as folder:
    p=Path(folder);(p/'test.c').write_text(prelude+prototypes+'\n'+defaults+'\n'+'\n'.join(bodies)+tests)
    subprocess.run(['cc','-std=c99','-Wno-deprecated-non-prototype',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
