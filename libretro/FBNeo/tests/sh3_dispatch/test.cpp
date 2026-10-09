// Host-only control-flow regression for the production dispatcher header.
// Native entries below are synthetic callbacks, NOT an emulation of PPC/SH3.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
enum { AM=1023, SH3_SHIFT=8, SH3_PAGEM=255, SH3_MAXHANDLER=16 };
struct Sh3PpcState { UINT32 pc, delay, r, total; int icount; };
static Sh3PpcState sh3_ppc_state;
#define m_pc sh3_ppc_state.pc
#define m_delay sh3_ppc_state.delay
#define m_sh4_icount sh3_ppc_state.icount
static UINT32 m_test_irq;
static bool sh3_drc_enabled, allocation_failure, recycle;
static unsigned allocations, compilations, native_calls, interpreted, outer_calls;
static unsigned events[16];
static UINT16 ram[2][512];
static UINT8 *MemMapF[4], *MemMapR[4];
static std::vector<UINT32> trace;
static UINT32 rng;
static UINT32 rnd() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
namespace Sh3Ppc {
enum { WAYS=4, CACHE_SETS=8, TABLE_SIZE=WAYS*CACHE_SETS, MAX_INSNS=32 };
#include "../../src/cpu/sh4/sh3_drc_block.h"
struct Lookup { UINT32 tag[WAYS]; unsigned next; };
static Block block_storage[TABLE_SIZE];
static Block *blocks;
static Lookup lookup[CACHE_SETS];
// The production dispatcher cross-checks a block's recorded code generation
// against this page array. The fixture has no guest RAM and no emitter, so a
// zeroed array means "the generation never changes", which is what its
// synthetic records are compiled with.
static UINT32 code_page_gen_storage[8192];
static UINT32 *code_page_gen = code_page_gen_storage;
static bool failed;
static void clear() { memset(block_storage,0,sizeof(block_storage)); memset(lookup,0,sizeof(lookup)); }
// Model the production cold/warm/sticky-failure allocation contract, rather
// than treating a permanently present array as a newly allocated cache.
static bool allocate() {
 ++allocations;
 if(failed)return false;
 if(blocks)return true;
 if(allocation_failure) {failed=true; return false;}
 blocks=block_storage;
 return true;
}
static int native(Sh3PpcState*);
static void compile(Block& b,UINT32 pc,const UINT16* source) {
 ++compilations;
 if(recycle && compilations%17==0) clear();
 b.pc=pc; b.source=source;
 // Exercise every snapshot length, including page tails and later-word writes.
 unsigned available=(SH3_PAGEM+1-(pc&SH3_PAGEM))/2;
 b.words=UINT16(1+((*source>>4)%33));
 if(b.words>available)b.words=UINT16(available);
 memcpy(b.original,source,b.words*2);
 b.cycles=UINT16(1+((*source>>8)&3)); b.check_read_map=(*source>>12)==10;
 b.entry=(*source>>12)==0?NULL:native;
}
// The production dispatcher tells the arena which sector a slot was compiled
// into, so reusing a sector can drop exactly those slots. This fixture has no
// generated code and no arena, so the note is a no-op.
static void arena_note_slot(unsigned,const Block&) {}
}
// Preserved pre-change implementation, independent of the production template.
static bool reference_dispatch() {
 using namespace Sh3Ppc;
 if(m_sh4_icount<=0 || !allocate())return false;
 UINT32 pc=m_pc, phys=pc&AM;
 const UINT8 *page=MemMapF[phys>>SH3_SHIFT];
 if((uintptr_t)page<SH3_MAXHANDLER || (phys&1))return false;
 const UINT16 *source=(const UINT16*)(page+(phys&SH3_PAGEM));
 unsigned index=((pc>>1)^(pc>>11)^(pc>>21))&(CACHE_SETS-1);
 Lookup &set=lookup[index]; unsigned way;
 for(way=0;way<WAYS;way++)if(set.tag[way]==pc)break;
 if(way==WAYS)way=set.next++&(WAYS-1);
 Block &b=blocks[index*WAYS+way];
 if(b.source!=source || b.pc!=pc || memcmp(b.original,source,b.words*2)!=0 ||
    (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page)) {
  compile(b,pc,source); set.tag[way]=pc;
 }
 if(!b.entry || m_sh4_icount<b.cycles)return false;
 return b.entry(&sh3_ppc_state)!=0;
}
template<bool Count> static bool sh3_drc_service_movll(unsigned) { return false; }
#include "../../src/cpu/sh4/sh3_drc_dispatch.h"
static UINT16 fetch(UINT32 pc) {
 UINT8* p=MemMapF[(pc&AM)>>SH3_SHIFT];
 if((uintptr_t)p<SH3_MAXHANDLER) p=(UINT8*)ram[0]+((pc&AM)&~SH3_PAGEM);
 UINT16 op; memcpy(&op,p+((pc&SH3_PAGEM)&~1u),sizeof(op)); return op;
}
static void charge(unsigned n) { m_sh4_icount-=(int)n; sh3_ppc_state.total+=n; }
static int action(UINT16 op,bool native) {
 unsigned kind=op>>12; ++events[kind];
 trace.push_back(m_pc); trace.push_back(op|(native?0x10000u:0));
 // Guard before executing; the outer loop must interpret this instruction.
 if(native && kind==3) return 0;
 sh3_ppc_state.r=(sh3_ppc_state.r<<7)^(sh3_ppc_state.r>>3)^op;
 charge(1+((op>>8)&3));
 m_pc=(m_pc&~UINT32(AM))|((m_pc+2)&AM);
 switch(kind) {
 case 2: m_pc=(op&255)*2; break;
 case 4: if(native) return 0; break; // partial block, one interpreter step next
 case 5: if(native) {m_delay=(m_pc&AM)+1; m_pc=0x80000000u|((op&255)*2);} break;
 case 6: m_test_irq=1; break;
 case 7: ram[0][(m_pc&AM)>>1]^=0x1701; break; // mutable future instruction
 case 8: { unsigned page=(m_pc&AM)>>8; MemMapF[page]=(UINT8*)ram[1]+page*256; break; }
 case 9: sh3_drc_enabled=false; break;
 case 10: MemMapR[(m_pc&AM)>>8]=(UINT8*)ram[1]+((m_pc&AM)&~255u); break;
 case 11: m_pc^=0x80000000u; break; // cached/uncached address tags
 case 12: ram[1][((m_pc+64)&AM)>>1]^=0x0101; break;
 case 13: m_pc|=1; break; // odd fetch must fall back
 case 14: m_pc=UINT32((op&15)*32); break; // hash collisions/looping successors
 default: break;
 }
 return 1;
}
static int Sh3Ppc::native(Sh3PpcState*) {
 ++native_calls;
 unsigned pc=m_pc,index=((pc>>1)^(pc>>11)^(pc>>21))&(CACHE_SETS-1);
 for(unsigned w=0;w<WAYS;++w) if(lookup[index].tag[w]==pc) {
  Block& b=blocks[index*WAYS+w]; return action(b.original[0],true);
 }
 fprintf(stderr,"No cached native block\n"); abort();
}
static void step() {
 ++interpreted;
 UINT16 op=fetch(m_delay?m_delay-1:m_pc); m_delay=0;
 action(op,false);
 // Synthetic interpreter dispatch clears a pending interrupt after the step.
 if(m_test_irq && !m_delay) {trace.push_back(0xfefefefeu); m_test_irq=0; sh3_drc_enabled=true;}
 charge(1); // same place as EAT(1) in the outer interpreter loop
}
struct Result {
 Sh3PpcState state; UINT16 memory[2][512]; unsigned nf[4],nr[4];
 UINT32 cache_tags[Sh3Ppc::CACHE_SETS][Sh3Ppc::WAYS];
 unsigned replacements[Sh3Ppc::CACHE_SETS];
 unsigned native, interpreted, compiled, timers, kinds[16], calls, alloc;
 UINT32 irq; bool enabled; std::vector<UINT32> events;
};
static unsigned mapid(UINT8* p) {
 if((uintptr_t)p<SH3_MAXHANDLER) return (unsigned)(uintptr_t)p;
 for(unsigned i=0;i<2;++i) for(unsigned j=0;j<4;++j)
  if(p==(UINT8*)ram[i]+j*256) return 16+i*4+j;
 abort();
}
static Result run(unsigned seed,int mode,bool hot=false) {
 rng=seed?seed:1; allocations=compilations=native_calls=interpreted=outer_calls=0;
 sh3_drc_work.clear();
 Sh3Ppc::blocks=NULL; Sh3Ppc::failed=false;
 memset(events,0,sizeof(events)); trace.clear(); Sh3Ppc::clear();
 allocation_failure=!hot && seed%29==0; recycle=!hot && seed%3==0;
 for(unsigned b=0;b<2;++b) for(unsigned i=0;i<512;++i)
  ram[b][i]=hot?UINT16(0x1100):UINT16(rnd());
 for(unsigned i=0;i<4;++i) {
  MemMapF[i]=(UINT8*)ram[0]+i*256; MemMapR[i]=MemMapF[i];
 }
 if(!hot && seed%17==0) MemMapF[2]=(UINT8*)1;
 sh3_ppc_state.pc=hot?0:((seed&511)*2); sh3_ppc_state.delay=0;
 sh3_ppc_state.r=seed; sh3_ppc_state.total=0; m_sh4_icount=0;
 m_test_irq=0; sh3_drc_enabled=true;
 unsigned timers=0;
 for(unsigned slice=0;slice<(hot?1u:12u);++slice) {
  if(!hot && slice==4) Sh3Ppc::clear(); // reset/state-load invalidation
  if(!hot && slice==6) {MemMapF[1]=(UINT8*)ram[1]+256; ram[1][128]^=0x2101;}
  if(!hot && slice==8) {MemMapR[0]=(UINT8*)ram[1]; sh3_drc_enabled=true;}
  int budget=hot?8192:int(rnd()%120)-2;
  unsigned before=sh3_ppc_state.total; m_sh4_icount=budget;
  unsigned watchdog=0;
  do {
   if(++watchdog>20000) {fprintf(stderr,"No progress\n"); abort();}
   if(sh3_drc_enabled && !m_delay && !m_test_irq) {
    ++outer_calls;
    bool ok=mode==0?reference_dispatch():(mode==1?sh3_drc_dispatch<false>():
      (mode==3?sh3_drc_dispatch_impl<true,true>():sh3_drc_dispatch<true>()));
    if(ok)continue;
   }
   step();
  } while(m_sh4_icount>0);
  timers=timers*131u+(sh3_ppc_state.total-before); // observe slice timing exactly
 }
 Result r; r.state=sh3_ppc_state; memcpy(r.memory,ram,sizeof(ram));
 for(unsigned i=0;i<Sh3Ppc::CACHE_SETS;++i) {
  memcpy(r.cache_tags[i],Sh3Ppc::lookup[i].tag,sizeof(r.cache_tags[i]));
  r.replacements[i]=Sh3Ppc::lookup[i].next;
 }
 for(unsigned i=0;i<4;++i) {r.nf[i]=mapid(MemMapF[i]);r.nr[i]=mapid(MemMapR[i]);}
 r.native=native_calls;r.interpreted=interpreted;r.compiled=compilations;r.timers=timers;
 r.irq=m_test_irq;r.enabled=sh3_drc_enabled;r.events=trace;
 memcpy(r.kinds,events,sizeof(events)); r.calls=outer_calls;r.alloc=allocations;return r;
}
static bool same(const Result& a,const Result& b) {
 return a.state.pc==b.state.pc && a.state.delay==b.state.delay && a.state.r==b.state.r &&
 a.state.total==b.state.total && a.state.icount==b.state.icount &&
 memcmp(a.memory,b.memory,sizeof(a.memory))==0 && memcmp(a.nf,b.nf,sizeof(a.nf))==0 &&
 memcmp(a.nr,b.nr,sizeof(a.nr))==0 && a.native==b.native && a.interpreted==b.interpreted &&
 a.compiled==b.compiled && a.timers==b.timers && a.irq==b.irq && a.enabled==b.enabled &&
 a.events==b.events && memcmp(a.cache_tags,b.cache_tags,sizeof(a.cache_tags))==0 &&
  memcmp(a.replacements,b.replacements,sizeof(a.replacements))==0;
}
// Exercise the real dispatcher with an initially absent cache, a sticky
// allocation failure, a warm retained cache, and an exit/reallocation cycle.
static bool cache_lifetime() {
 run(1,2,true); // deterministic mapped native callbacks; no external I/O
 recycle=false; Sh3Ppc::blocks=NULL; Sh3Ppc::failed=false;
 allocation_failure=false; Sh3Ppc::clear(); allocations=0;
 m_pc=0; m_delay=0; m_test_irq=0; sh3_drc_enabled=true; m_sh4_icount=0;
 if(sh3_drc_dispatch<false>() || allocations || Sh3Ppc::blocks || Sh3Ppc::failed)return false;
 m_sh4_icount=2; allocation_failure=true;
 if(sh3_drc_dispatch<false>() || allocations!=1 || Sh3Ppc::blocks || !Sh3Ppc::failed)return false;
 allocation_failure=false;
 if(sh3_drc_dispatch<false>() || Sh3Ppc::blocks || !Sh3Ppc::failed)return false;
#ifdef SH3_CACHE_READY_EXPECT_FAST
 if(allocations!=1)return false;
#endif
 // The production exit path resets failed before a new initialization.
 Sh3Ppc::failed=false; allocations=0;
 if(!sh3_drc_dispatch<false>() || allocations!=1 || !Sh3Ppc::blocks)return false;
 Sh3Ppc::clear(); // reset/state-load invalidates code but retains allocation
 unsigned before=allocations;
 for(unsigned i=0;i<64;++i) {
  m_pc=0; m_sh4_icount=2;
  if(!sh3_drc_dispatch<false>())return false;
 }
#ifdef SH3_CACHE_READY_EXPECT_FAST
 if(allocations!=before)return false;
#endif
 printf("PASS cache lifetime: 64 warm single-block entries made %u allocation-helper calls\n",allocations-before);
 // Even a failed+non-null synthetic state must preserve failure precedence.
 Sh3Ppc::failed=true; m_pc=0; m_sh4_icount=2;
 if(sh3_drc_dispatch<false>() || m_pc!=0 || m_sh4_icount!=2)return false;
 Sh3Ppc::blocks=NULL; Sh3Ppc::failed=false; Sh3Ppc::clear(); allocations=0;
 if(!sh3_drc_dispatch<false>() || allocations!=1 || !Sh3Ppc::blocks)return false;
 puts("PASS zero budget, initial failure, sticky failure, warm reset, failure precedence and reallocation");
 return true;
}

static bool workload_profile_checks() {
 Result plain=run(1,2,true),counted=run(1,3,true);
 if(!same(plain,counted))return false;
 Sh3WorkCount sum=0;
 for(unsigned i=0;i<34;++i)sum+=sh3_drc_work.snapshot_lengths[i];
 if(sh3_drc_work.native_calls!=counted.native || sum!=counted.native ||
    sh3_drc_work.native_cycles!=counted.state.total ||
    sh3_drc_work.rebuilds!=counted.compiled)return false;
 // Warm ordinary entry must not write any profiling byte.
 Sh3DrcWorkProfile saved=sh3_drc_work;
 m_pc=0; m_sh4_icount=2; m_delay=0; m_test_irq=0; sh3_drc_enabled=true;
 if(!sh3_drc_dispatch<false>() || memcmp(&saved,&sh3_drc_work,sizeof(saved)))return false;
 // The long chain exceeds this fixture's tiny cache and may have zero hits.
 // Explicitly repeat the now-warm PC to verify the source-check counter.
 m_pc=0; m_sh4_icount=2;
 if(!sh3_drc_dispatch_impl<false,true>() ||
    sh3_drc_work.validation_spans!=saved.validation_spans+1)return false;
 puts("PASS counted/ordinary dispatcher states and cycles match; histogram totals match native calls; ordinary entry leaves profile untouched");
 return true;
}

static bool fallback_origin_checks() {
 for(unsigned which=0;which<5;++which) {
  run(1,2,true);Sh3Ppc::clear();m_pc=0;m_sh4_icount=8;
  m_delay=0;m_test_irq=0;sh3_drc_enabled=true;recycle=false;
  sh3_drc_work.fallback.last_origin=SH3_FB_UNKNOWN;
  unsigned expected=SH3_FB_UNKNOWN;
  if(which==0){m_sh4_icount=0;expected=SH3_FB_GATE;}
  if(which==1){m_pc=1;expected=SH3_FB_FETCH;}
  if(which==2){ram[0][0]=0;expected=SH3_FB_NO_ENTRY;}
  if(which==3){m_sh4_icount=1;expected=SH3_FB_BUDGET;}
  if(which==4){ram[0][0]=0x3100;expected=SH3_FB_PARTIAL;}
  if(sh3_drc_dispatch_impl<false,true>() || sh3_drc_work.fallback.last_origin!=expected)return false;
 }
 puts("PASS exact dispatcher fallback origins: gate, fetch, no-entry, budget and partial");
 return true;
}

int main() {
 if(!fallback_origin_checks()) {fputs("FAIL fallback origins\n",stderr);return 7;}
 if(!workload_profile_checks()) {fputs("FAIL workload profile\n",stderr);return 6;}
 if(!cache_lifetime()) {fputs("FAIL allocation lifecycle\n",stderr); return 4;}
 unsigned coverage[16]={0};
 for(unsigned s=1;s<=12000;++s) {
  Result a=run(s,0),b=run(s,1),c=run(s,2),d=run(s,3);
  if(!same(a,b)||!same(a,c)||!same(a,d)) {fprintf(stderr,"FAIL dispatch seed=%u native=%u/%u steps=%u/%u compiles=%u/%u\n",s,a.native,c.native,a.interpreted,c.interpreted,a.compiled,c.compiled);return 1;}
  for(unsigned i=0;i<16;++i) coverage[i]+=a.kinds[i];
 }
 for(unsigned i=0;i<16;++i) if(!coverage[i])return 2;
 Result a=run(1,0,true),b=run(1,2,true),single=run(1,1,true);
 if(!same(a,b)||!same(a,single)||b.calls>=a.calls||b.alloc>=a.alloc)return 3;
#ifdef SH3_CACHE_READY_EXPECT_FAST
 if(single.alloc!=1 || b.alloc!=1)return 5;
 printf("PASS warm-entry check: %u native single-block entries, allocation-helper calls %u -> %u\n",a.native,a.alloc,single.alloc);
#endif
 puts("PASS 12000 randomized traces x 12 slices: original/single/fused dispatcher states, memory, mappings, callback order and cycles identical");
 puts("PASS guards, partial exits, delay slots, IRQ gates, engine mode, exhausted/short budgets, mutable code, aliases, hash eviction and arena reset");
 puts("PASS all final cache tags and replacement cursors match the original loop");
 printf("PASS synthetic straight chain: native blocks=%u, C++ entries=%u -> %u, allocation checks=%u -> %u\n",a.native,a.calls,b.calls,a.alloc,b.alloc);
 puts("Scope: production C++ dispatcher with synthetic entries, not PPC execution or console FPS");
 return 0;
}
