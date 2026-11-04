#!/usr/bin/env python3
"""Production CPU accounting: Xtensa outer IRQ depth and saved owner."""
from pathlib import Path
import sys, subprocess, tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <stdio.h>
#define CP32_IRAM_EXT
#define HARDWARE 0
#define IDLE 1
#define NIL_PROC ((struct proc *)0)
struct proc {unsigned user_time,sys_time;};
static struct proc table[5],*proc_ptr,*bill_ptr;
static volatile struct proc *cp32_irq_saved_owner;
static int k_reenter;
static unsigned cp32_clock_accounted_ticks;
#define proc_addr(n) (&table[n])
'''
test=r'''
int main(void){
 k_reenter=1;cp32_irq_saved_owner=&table[2];proc_ptr=&table[3];bill_ptr=&table[2];
 cp32_clock_charge(1);
 assert(table[2].user_time==1 && !table[3].user_time && !table[0].user_time && !table[2].sys_time);
 bill_ptr=&table[4];cp32_clock_charge(4);
 assert(table[2].user_time==5 && table[4].sys_time==4);
 cp32_irq_saved_owner=&table[IDLE];cp32_clock_charge(3);
 assert(table[IDLE].user_time==3 && table[4].sys_time==4);
 k_reenter=2;cp32_clock_charge(2);
 assert(table[HARDWARE].user_time==2 && table[4].sys_time==6);
 k_reenter=0;cp32_clock_charge(1);
 assert(table[3].user_time==1 && table[4].sys_time==7);
 k_reenter=1;cp32_irq_saved_owner=0;bill_ptr=0;cp32_clock_charge(1);
 assert(table[HARDWARE].user_time==3 && cp32_clock_accounted_ticks==12);
 puts("process accounting: saved owner, outer/nested IRQs, idle, billing and batched ticks pass");
}
'''
source=root/'src/kernel/clock.c'
handler=source.read_text().split('PRIVATE int clock_handler(irq)',1)[1]
assert handler.index('cp32_clock_charge(ticks)')<handler.index('cp32_tty_poll_tick()')
assert 'ticks=lost_ticks+1;' in handler and 'lost_ticks=0;' in handler
irq=(root/'src/kernel/irq.S').read_text()
assert irq.index('movi    a3, cp32_irq_saved_owner')<irq.index('call0   cp32_irq_dispatch')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extract_function(source,'cp32_clock_charge')+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
