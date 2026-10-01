// Production service guards/prologue over modeled handler and IRQ effects.
// Generated code and real instruction helpers are tested separately on PPC.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint16_t UINT16;
enum { AM=0x1fffffff, SH3_SHIFT=16, SH3_PAGEM=65535, SH3_MAXHANDLER=8 };
struct State {
 unsigned r[16],pc,ppc,ea,delay,total;
 int icount;
 unsigned char *read_mirror;
 unsigned mirror_watch,mirror_page,mirror_handler;
};
static State sh3_ppc_state;
#define m_r sh3_ppc_state.r
#define m_pc sh3_ppc_state.pc
#define m_ppc sh3_ppc_state.ppc
#define m_ea sh3_ppc_state.ea
#define m_delay sh3_ppc_state.delay
#define m_sh4_icount sh3_ppc_state.icount
static unsigned idle_ram=0x0c002310,idle_pc=0x0c1d1346;
static const unsigned *sh3_idle_watch_ram=&idle_ram,*sh3_idle_watch_pc=&idle_pc;
static bool sh3_drc_enabled=true;
static unsigned m_test_irq;
static unsigned char *MemMapR[8192];
typedef unsigned (*pSh3ReadLongHandler)(unsigned);
struct Sh3DrcDeviceRead {unsigned address,handler;pSh3ReadLongHandler callback;};
static Sh3DrcDeviceRead sh3_device_reads[2];
static pSh3ReadLongHandler ReadLong[8];
#include "../../src/cpu/sh4/sh3_drc_work_profile.h"
static unsigned reads,irq_calls,read_pc,read_address;
#define EAT(n) { m_sh4_icount-=(n); sh3_ppc_state.total+=(n); }
static unsigned RL(unsigned a) {
 ++reads; read_pc=m_pc&AM; read_address=a&AM;
 // Modeled WaitState and original handler burn precede the base step.
 EAT(7); EAT(1024);
 return (a&AM)^read_pc;
}
static void MOVLL(UINT16 op) { m_ea=m_r[(op>>4)&15];m_r[(op>>8)&15]=RL(m_ea); }
static void sh4_check_pending_irq() {
 ++irq_calls;m_test_irq=0;m_pc=0x0c000600;m_ppc^=0x1234;EAT(3);
}
#include "../../src/cpu/sh4/sh3_drc_movll_service.h"
static void setup(unsigned variant,unsigned n,unsigned m) {
 memset(&sh3_ppc_state,0,sizeof(sh3_ppc_state));
 for(unsigned i=0;i<16;++i)m_r[i]=i*131u+variant;
 m_pc=(variant&1)?idle_pc:idle_pc-2;m_delay=(variant&1)?idle_pc+6:0;
 m_r[m]=(variant&2)?idle_ram|0xa0000000u:idle_ram;
 m_sh4_icount=(int)(variant%10)-3;
 sh3_ppc_state.read_mirror=(unsigned char*)(uintptr_t)0x100000;
 sh3_ppc_state.mirror_watch=idle_ram;sh3_ppc_state.mirror_page=idle_ram&~65535u;
 sh3_ppc_state.mirror_handler=1;MemMapR[idle_ram>>16]=(unsigned char*)(uintptr_t)1;
 reads=irq_calls=read_pc=read_address=0;m_test_irq=(variant&4)?1:0;
 sh3_idle_watch_ram=&idle_ram;sh3_idle_watch_pc=&idle_pc;sh3_drc_enabled=true;
 sh3_drc_work.clear(); (void)n;
}
int main() {
 unsigned cases=0;
 for(unsigned v=0;v<16;++v)for(unsigned n=0;n<16;++n)for(unsigned m=0;m<16;++m) {
  setup(v,n,m);State before=sh3_ppc_state;unsigned irq_before=m_test_irq;
  unsigned op=0x6002|(n<<8)|(m<<4);
  bool counted=(v&8)!=0;
  bool done=counted?sh3_drc_service_movll<true>(op):sh3_drc_service_movll<false>(op);
  assert(done && reads==1 && irq_calls==((v&4)?1u:0u));
  assert(read_pc==idle_pc && read_address==idle_ram);
  assert(sh3_drc_work.movll_services==(counted?1u:0u));
  assert(sh3_drc_work.interpreter_steps==0 && sh3_drc_work.exit_partial==0);
  State got=sh3_ppc_state;unsigned irq_got=m_test_irq;
  sh3_ppc_state=before;m_test_irq=irq_before;reads=irq_calls=0;
  // Independent original timerhack prologue + existing instruction helper.
  if(m_delay)m_delay=0;else m_pc+=2;m_ppc=m_pc;
  MOVLL((UINT16)op);
  if(m_test_irq && !m_delay)sh4_check_pending_irq();EAT(1);
  assert(!memcmp(&got,&sh3_ppc_state,sizeof(got)) && irq_got==m_test_irq);
  ++cases;
 }
 for(unsigned bad=0;bad<11;++bad) {
  setup(0,0,2);unsigned op=0x6022;
  switch(bad) {
   case 0:sh3_drc_enabled=false;break;
   case 1:op=0x6026;break;
   case 2:sh3_idle_watch_pc=NULL;break;
   case 3:m_r[2]=0xec002310;break;
   case 4:m_r[2]++;break;
   case 5:m_pc+=4;break;
   case 6:sh3_ppc_state.mirror_watch+=4;break;
   case 7:sh3_ppc_state.mirror_page+=65536;break;
   case 8:sh3_ppc_state.read_mirror=NULL;break;
   case 9:MemMapR[idle_ram>>16]=(unsigned char*)(uintptr_t)2;break;
   case 10:idle_ram+=4;break;
  }
  State before=sh3_ppc_state;
  assert(!sh3_drc_service_movll<true>(op));
  assert(!memcmp(&before,&sh3_ppc_state,sizeof(before)) && reads==0 && irq_calls==0);
  assert(sh3_drc_work.movll_service_rejects==1);++cases;
  idle_ram=0x0c002310;
 }
 const unsigned device_address[2]={0x18000010,0x0400002c};
 for(unsigned slot=0;slot<2;++slot) {
  unsigned handler=slot?7:0;
  sh3_device_reads[slot].address=device_address[slot];
  sh3_device_reads[slot].handler=handler;
  sh3_device_reads[slot].callback=RL;ReadLong[handler]=RL;
  MemMapR[device_address[slot]>>16]=(unsigned char*)(uintptr_t)handler;
  for(unsigned v=0;v<16;++v)for(unsigned n=0;n<16;++n)for(unsigned m=0;m<16;++m) {
   setup(v,n,m);sh3_idle_watch_ram=sh3_idle_watch_pc=NULL;
   m_r[m]=device_address[slot]|((v&2)?0xa0000000u:0);
   unsigned op=0x6002|(n<<8)|(m<<4);State before=sh3_ppc_state;unsigned irq_before=m_test_irq;
   bool counted=(v&8)!=0;
   assert(counted?sh3_drc_service_movll<true>(op):sh3_drc_service_movll<false>(op));
   assert(reads==1 && read_address==device_address[slot] && read_pc==idle_pc);
   assert(sh3_drc_work.device_services[slot]==(counted?1u:0u));
   assert(sh3_drc_work.device_service_cycles[slot]==(counted?(1032u+((v&4)?3u:0u)):0u));
   assert(sh3_drc_work.idle_candidates.watched_movll==0);
   State got=sh3_ppc_state;unsigned irq_got=m_test_irq;
   sh3_ppc_state=before;m_test_irq=irq_before;reads=irq_calls=0;
   if(m_delay)m_delay=0;else m_pc+=2;m_ppc=m_pc;
   MOVLL((UINT16)op);if(m_test_irq&&!m_delay)sh4_check_pending_irq();EAT(1);
   assert(!memcmp(&got,&sh3_ppc_state,sizeof(got)) && irq_got==m_test_irq);++cases;
  }
  for(unsigned bad=0;bad<5;++bad) {
   setup(0,0,1);m_r[1]=device_address[slot];
   switch(bad) {
    case 0:ReadLong[handler]=NULL;break;
    case 1:MemMapR[device_address[slot]>>16]=(unsigned char*)(uintptr_t)0x100000;break;
    case 2:sh3_device_reads[slot].callback=NULL;break;
    case 3:m_r[1]+=4;break;
    case 4:m_r[1]|=0xe0000000u;break;
   }
   State before=sh3_ppc_state;
   assert(!sh3_drc_service_movll<true>(0x6012) && reads==0 && irq_calls==0);
   assert(!memcmp(&before,&sh3_ppc_state,sizeof(before)));++cases;
   ReadLong[handler]=RL;sh3_device_reads[slot].callback=RL;
   MemMapR[device_address[slot]>>16]=(unsigned char*)(uintptr_t)handler;
  }
  sh3_device_reads[slot].callback=NULL;
 }
 printf("PASS %u idle/device service prologue/operand/alias/budget/IRQ and guarded rejection cases\n",cases);
 puts("Scope: production service over modeled RL/IRQ; actual emitter/helper execution is in the PPC suite");
}
