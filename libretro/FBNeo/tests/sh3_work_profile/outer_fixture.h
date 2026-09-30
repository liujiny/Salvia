// Synthetic CPU/device callbacks around extracted production outer loops.
// This checks control-flow/cycle preservation, not generated PPC execution.
// Instruction decoding is abstracted here; sh3_hot_fallback separately checks
// the production selector with every opcode and the real 0x6 helper bodies.
#define sh3_execute_hot_fallback execute_one
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/cpu/sh4/sh3_drc_work_profile.h"
typedef uint16_t UINT16;
typedef uint32_t UINT32;
enum { AM=1023 };
struct State {
 int icount,total,end_run,off;
 UINT32 pc,ppc,delay,irq,r,timer_hash;
 unsigned native_calls,interpreter_calls,interpreter_cycles;
 bool drc;
 UINT16 memory[512];
};
static State s;
static bool in_interpreter;
#define m_sh4_icount s.icount
#define sh3_total_cycles s.total
#define sh3_end_run s.end_run
#define m_cpu_off s.off
#define m_pc s.pc
#define m_ppc s.ppc
#define m_delay s.delay
#define m_test_irq s.irq
#define sh3_drc_enabled s.drc
static void charge(unsigned n) {
 s.icount-=int(n);s.total+=int(n);
 if(in_interpreter)s.interpreter_cycles+=n;
}
#define EAT(n) charge(n)
static UINT16 sh3_cpu_readop16(UINT32 address) {return s.memory[(address&AM)>>1];}
static void execute_one(UINT16 opcode) {
 ++s.interpreter_calls;in_interpreter=true;
 charge(1+((opcode>>8)&3));
 s.r=s.r*1664525u+opcode+1013904223u;
 switch(opcode>>12) {
 case 1: s.pc=(opcode&255u)*2; break;
 case 2: s.delay=((opcode&255u)*2)|2; break;
 case 3: s.irq=1; break;
 case 4: s.memory[((s.pc+36)&AM)>>1]^=0x811; break;
 case 5: s.drc=(opcode&1)!=0; break;
 case 6: charge(3); break;
 default: break;
 }
}
static void sh4_check_pending_irq() {s.r^=0xc001d00du;s.irq=0;charge(3);}
static void sh4_run_timers(int cycles) {
 in_interpreter=false;s.timer_hash=s.timer_hash*131u+unsigned(cycles);
 if((s.timer_hash&7)==1)s.irq=1;
 if((s.timer_hash&15)==3)s.drc=true;
}
#define SH3_PPC_DRC 1
// This fixture isolates outer-loop ordering. The detail suite separately uses
// the real observer and real mapping classifier without guest memory reads.
static void sh3_work_fallback_observe(unsigned op,unsigned pc,bool delay) {
 sh3_drc_work.fallback.begin(pc,op,0,SH3_FB_ACCESS_UNKNOWN,delay);
}
template<bool Chained,bool Count> static bool sh3_drc_dispatch_impl() {
 in_interpreter=false;
 if(Count)++sh3_drc_work.dispatch_calls;
 if(s.icount<2 || (s.pc&6)==2)return false;
 ++s.native_calls;
 if(Count) {++sh3_drc_work.native_calls;sh3_drc_work.native_cycles+=2;}
 charge(2);s.r=(s.r<<3)^(s.r>>29)^s.pc;s.pc=(s.pc+2)&AM;
 // Occasionally request interpreter work after partial native execution.
 return (s.r&7)!=0;
}
template<bool Chained> static bool sh3_drc_dispatch() {return sh3_drc_dispatch_impl<Chained,false>();}
static int reference_timerhack(int cycles);
template<bool Count> static int Sh3Run_timerhack_impl(int cycles);
static State run(unsigned seed,unsigned mode) {
 memset(&s,0,sizeof(s));sh3_drc_work.clear();in_interpreter=false;
 UINT32 rng=seed;
 for(unsigned i=0;i<512;++i) {rng=rng*1664525u+1013904223u;s.memory[i]=UINT16(rng>>8);}
 s.pc=(seed*14)&AM;s.r=seed;s.drc=(seed%5)!=0;s.off=(seed%79)==0;
 for(unsigned slice=0;slice<12;++slice) {
  rng=rng*1664525u+1013904223u;int budget=int(rng%120)-2;
  if(slice==4)s.delay=34;
  if(slice==7){s.irq=1;s.drc=true;}
  in_interpreter=false;
  int done=mode==0?reference_timerhack(budget):
    (mode==1?Sh3Run_timerhack_impl<false>(budget):Sh3Run_timerhack_impl<true>(budget));
  s.r^=UINT32(done)*17u;
 }
 return s;
}
int main() {
 for(unsigned seed=1;seed<=12000;++seed) {
  State reference=run(seed,0),normal=run(seed,1),counted=run(seed,2);
  if(memcmp(&reference,&normal,sizeof(s)) || memcmp(&reference,&counted,sizeof(s))) {
   printf("FAIL outer loop seed=%u\n",seed);return 1;
  }
  if(sh3_drc_work.fallback.observed!=counted.interpreter_calls)return 4;
  Sh3WorkCount detail_steps=0,detail_cycles=0;
  for(unsigned i=0;i<SH3_FB_ORIGINS;++i) {
   detail_steps+=sh3_drc_work.fallback.origins[i];
   detail_cycles+=sh3_drc_work.fallback.origin_cycles[i];
  }
  if(detail_steps!=counted.interpreter_calls || detail_cycles!=counted.interpreter_cycles)return 5;
  Sh3WorkCount histogram=0;
  for(unsigned i=0;i<256;++i)histogram+=sh3_drc_work.interpreter_hi8[i];
  if(sh3_drc_work.slices!=12 || sh3_drc_work.interpreter_steps!=counted.interpreter_calls ||
     histogram!=counted.interpreter_calls || sh3_drc_work.interpreter_cycles!=counted.interpreter_cycles ||
     sh3_drc_work.native_cycles!=counted.native_calls*2u)return 2;
 }
 run(1,1);
 Sh3DrcWorkProfile zero={};
 if(memcmp(&zero,&sh3_drc_work,sizeof(zero)))return 3;
 puts("PASS extracted production outer loop: 12000 seeds x 12 slices, baseline/normal/counted states, memory and cycles identical");
 puts("PASS interpreter opcode families/cycle accounting, CPU-off, delay/IRQ, zero/negative budgets and timer calls; normal specialization has no counter writes");
 puts("Scope: real loop source with synthetic callbacks, not PPC execution or game replay");
 return 0;
}
