#!/usr/bin/env python3
"""Compare batched self-jumps against instruction stepping, including timers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/cpu/nec/v25.cpp').read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


preamble = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
typedef uint32_t UINT32;
typedef uint8_t UINT8;
typedef int32_t INT32;
struct v25_state_t {
 UINT32 pc, pending_irq, unmasked_irq;
 int icount, stop_run, TF, no_interrupt, seg_prefix;
 int prefetch_reset, prefetch_count, prefetch_size, prefetch_cycles;
 int timer_enabled[4], timer_cycles_until_trigger[4];
 int timer_cycles_period[4], timer_flags[4];
};
static v25_state_t sChips[4];
static UINT32 idle_start[4], idle_end[4];
#define PC(s) ((s)->pc)
'''

test = r'''
static uint32_t rng=314159;
static unsigned next() {rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng;}
static v25_state_t initial() {
 v25_state_t s={}; s.pc=0x422; s.icount=1024;
 s.prefetch_count=s.prefetch_size=4; s.prefetch_cycles=4;
 return s;
}
// One reference EB FE plus the emulator's unmodified prefetch routine.
static void step(v25_state_t &s, int budget, std::vector<int> &events) {
 if (s.pending_irq & s.unmasked_irq) {
  events.push_back(-1); events.push_back(budget-s.icount);
  s.stop_run=1; return;
 }
 if (s.no_interrupt) --s.no_interrupt;
 int before=s.icount;
 s.prefetch_count-=2;
 s.icount-=12;
 do_prefetch(&s,before);
 for(int i=0;i<4;i++) if(s.timer_enabled[i]) {
  s.timer_cycles_until_trigger[i]-=before-s.icount;
  if(s.timer_cycles_until_trigger[i]<=0) {
   events.push_back(i); events.push_back(budget-s.icount);
   if(s.timer_flags[i]) s.timer_cycles_until_trigger[i]=s.timer_cycles_period[i];
   else s.timer_enabled[i]=0;
   s.pending_irq|=1U<<i;
  }
 }
}
static void rejected() {
 v25_state_t before=sChips[0];
 v25_skip_self_jump(&sChips[0],0x422,0xeb,12);
 assert(!memcmp(&before,&sChips[0],sizeof(before)));
}
int main() {
 idle_end[0]=0xffff;
 unsigned reduced=0;
 for(int trial=0;trial<20000;trial++) {
  v25_state_t seed=initial(); seed.icount=1+next()%8192;
  seed.prefetch_size=2+next()%5; seed.prefetch_cycles=1+next()%8;
  seed.prefetch_count=next()%(seed.prefetch_size+1); seed.prefetch_reset=next()%2;
  seed.no_interrupt=next()%3; seed.unmasked_irq=next()%16;
  for(int i=0;i<4;i++) {
   seed.timer_enabled[i]=next()%2; seed.timer_flags[i]=next()%2;
   seed.timer_cycles_until_trigger[i]=1+next()%8192;
   seed.timer_cycles_period[i]=1+next()%1024;
  }
  v25_state_t reference=seed; sChips[0]=seed;
  std::vector<int> a,b;
  while(reference.icount>0&&!reference.stop_run) step(reference,seed.icount,a);
  while(sChips[0].icount>0&&!sChips[0].stop_run) {
   int before=sChips[0].icount;
   step(sChips[0],seed.icount,b);
   int remaining=sChips[0].icount;
   v25_skip_self_jump(&sChips[0],0x422,0xeb,before-remaining);
   reduced+=sChips[0].icount<remaining;
  }
  assert(!memcmp(&reference,&sChips[0],sizeof(reference)));
  assert(a==b); // identical callback and interrupt instruction boundaries
 }
 assert(reduced>1000);
 // Every remainder, including exactly zero and an expiry exactly at the budget.
 for(int cycles=0;cycles<512;cycles++) for(int timer=1;timer<48;timer++) {
  v25_state_t reference=initial(); reference.icount=cycles;
  reference.timer_enabled[0]=1; reference.timer_cycles_until_trigger[0]=timer;
  sChips[0]=reference; std::vector<int>a,b;
  while(reference.icount>0)step(reference,cycles,a);
  while(sChips[0].icount>0) {
   int before=sChips[0].icount;step(sChips[0],cycles,b);
   v25_skip_self_jump(&sChips[0],0x422,0xeb,before-sChips[0].icount);
  }
  assert(!memcmp(&reference,&sChips[0],sizeof(reference))&&a==b);
 }
 sChips[0]=initial(); sChips[0].TF=1; rejected();
 sChips[0]=initial(); sChips[0].stop_run=1; rejected();
 sChips[0]=initial(); sChips[0].seg_prefix=1; rejected();
 sChips[0]=initial(); sChips[0].no_interrupt=1; rejected();
 sChips[0]=initial(); sChips[0].pc++; rejected();
 sChips[0]=initial(); sChips[0].prefetch_reset=1; rejected();
 sChips[0]=initial(); sChips[0].prefetch_count--; rejected();
 sChips[0]=initial(); sChips[0].prefetch_cycles=0; rejected();
 sChips[0]=initial(); sChips[0].pending_irq=sChips[0].unmasked_irq=1; rejected();
 sChips[0]=initial(); idle_start[0]=~0U; rejected(); idle_start[0]=0;
 idle_end[0]=0x422; rejected(); idle_end[0]=0xffff;
 for(int op=0;op<256;op++) if(op!=0xeb) {
  sChips[0]=initial(); v25_skip_self_jump(&sChips[0],0x422,op,12);
  assert(sChips[0].icount==1024);
 }
 sChips[0]=initial(); v25_skip_self_jump(&sChips[0],0x422,0xeb,16);
 assert(sChips[0].icount==1024);
 puts("PASS V25: 20,000 randomized timer/prefetch cases, cycle remainders, IRQ/ROM guards");
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(
        preamble + function('static void do_prefetch(')
        + function('static void v25_skip_self_jump(') + test)
    subprocess.run(['g++', '-O2', '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
