"""Run production keyboard init against a TCA8418 register/FIFO model."""
from pathlib import Path
import subprocess,sys,tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define CP32_IRAM_EXT
static unsigned kbd_ready,cardputer_keyboard_stale_events;
static unsigned count,popped,reads,writes,cleared,enabled,sticky,upper;
static unsigned fail_read,fail_write;
static int init_write(uint8_t reg,uint8_t value) {
 assert(!kbd_ready);
 if(++writes==fail_write)return 0;
 if(reg==0x02) {assert(!count && value==3);cleared++;}
 if(reg==0x01) {assert(!count && cleared && value==3);enabled++;}
 return 1;
}
static int read_register(uint8_t reg,unsigned char *value) {
 assert(!kbd_ready);
 if(++reads==fail_read)return 0;
 if(reg==3)*value=(unsigned char)(upper|count);
 else {assert(reg==4 && count);*value=(popped&1) ? 0x24:0xa4;popped++;if(!sticky)count--;}
 return 1;
}
/* Models the readiness guard that made the old init drain a no-op. */
int cardputer_keyboard_read_event(unsigned char *value) {
 if(!kbd_ready)return 0;
 if(!count)return 0;
 *value=0xa4;count--;return 1;
}
'''
test=r'''
static void reset(unsigned events) {
 kbd_ready=1;cardputer_keyboard_stale_events=99;count=events;
 popped=reads=writes=cleared=enabled=sticky=upper=fail_read=fail_write=0;
}
int main(void) {
 for(unsigned n=0;n<=10;n++) {
  reset(n);assert(cardputer_keyboard_init()==1);
  assert(kbd_ready && !count && popped==n && cardputer_keyboard_stale_events==n);
  assert(cleared==1 && enabled==1);
  /* Input arriving AFTER startup remains deliverable. */
  unsigned char event=0;count=1;assert(cardputer_keyboard_read_event(&event)==1 && event==0xa4);
 }
 reset(10);upper=0x70;assert(cardputer_keyboard_init()==1 && popped==10);
 for(unsigned i=1;i<=21;i++) {
  reset(10);fail_read=i;assert(!cardputer_keyboard_init());
  assert(!kbd_ready && !enabled && !cleared);
 }
 reset(1);sticky=1;assert(!cardputer_keyboard_init());
 assert(!kbd_ready && !enabled && popped==32 && cardputer_keyboard_stale_events==32);
 for(unsigned i=1;i<=17;i++) {
  reset(0);fail_write=i;assert(!cardputer_keyboard_init() && !kbd_ready);
 }
 puts("keyboard init: stale FIFO drained before readiness; fresh input retained; I2C failures and bounded drain pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extract_function(root/'src/kernel/cardputer.c','cardputer_keyboard_init')+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
