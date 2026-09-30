// Actual detail accumulator/classifier/observer, with mapping metadata only.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>
#include "../../src/cpu/sh4/sh3_drc_work_profile.h"
typedef uint8_t UINT8;
enum { AM=0x1fffffff, SH3_SHIFT=16, SH3_PAGEM=65535, SH3_MAXHANDLER=8 };
static unsigned m_r[16];
static UINT8 *MemMapR[8192];
static struct { UINT8* read_mirror; unsigned mirror_page,mirror_watch,mirror_handler; } sh3_ppc_state;
#define SH3_PPC_DRC 1
#include "../../src/cpu/sh4/sh3_fallback_observer.h"
static unsigned cases;
static void require(bool b) {++cases;if(!b){fprintf(stderr,"FAIL detail case=%u\n",cases);exit(1);}}
static unsigned rng=0x195231abu;
static unsigned next() {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static std::vector<std::string> lines;
static void output(const char *s) {require(strlen(s)<768);lines.push_back(s);}
static void classifier() {
 for(unsigned op=0;op<65536;++op) {
  unsigned want=0;
  if(op>=0x6000 && op<=0x60ff) {
   switch(op%16) {case 0:case 4:want=1;break;case 1:case 5:want=2;break;case 2:case 6:want=4;break;}
  }
  require(sh3_fallback_read_size(op)==want);
 }
 const unsigned addresses[]={0,1,2,3,0x0c000020,0x8c000020,0xac000020,0xcc000020,0xe0000020,0xffffffff};
 for(unsigned si=0;si<4;++si)for(unsigned ai=0;ai<10;++ai)
 for(unsigned mapped=0;mapped<2;++mapped)for(unsigned mirror=0;mirror<2;++mirror)
 for(unsigned watched=0;watched<2;++watched) {
  unsigned size=si?1u<<(si-1):0,a=addresses[ai],p=a&AM,w=watched?p:p+4;
  unsigned want=!size?SH3_FB_NOT_60_READ:a>=0xe0000000u?SH3_FB_INTERNAL:
   (a%size)?SH3_FB_UNALIGNED:mapped?SH3_FB_MAPPED:
   mirror?(size==4 && p==w?SH3_FB_WATCHED:SH3_FB_MIRROR):SH3_FB_HANDLER;
  require(sh3_fallback_access(size,a,mapped!=0,mirror!=0,p,w)==want);
 }
}
static void observer() {
 memset(MemMapR,0,sizeof(MemMapR));memset(m_r,0,sizeof(m_r));sh3_drc_work.clear();
 sh3_ppc_state.read_mirror=(UINT8*)(uintptr_t)0x100000; // never dereferenced
 sh3_ppc_state.mirror_page=0x0c000000;sh3_ppc_state.mirror_watch=0x0c000020;sh3_ppc_state.mirror_handler=1;
 const unsigned a[]={0x0c000020,0xac000020,0x0c000020,0x0c000020,0x0c000024,0x0c000021,0xe0000020,0x0c000020,0x0c000020,0x0c000020};
 const unsigned op[]={0x6012,0x6012,0x6011,0x6010,0x6012,0x6012,0x6012,0x6012,0x6012,0x6013};
 const unsigned cls[]={SH3_FB_WATCHED,SH3_FB_WATCHED,SH3_FB_MIRROR,SH3_FB_MIRROR,SH3_FB_MIRROR,SH3_FB_UNALIGNED,SH3_FB_INTERNAL,SH3_FB_MAPPED,SH3_FB_HANDLER,SH3_FB_NOT_60_READ};
 for(unsigned i=0;i<10;++i) {
  MemMapR[0xc00]=i==7?(UINT8*)(uintptr_t)0x200000:(UINT8*)(uintptr_t)(i==8?2:1);
  m_r[1]=a[i];sh3_drc_work.fallback.last_origin=SH3_FB_PARTIAL;
  sh3_work_fallback_observe(op[i],0x0c001000+i*2,i==1);
  require(sh3_drc_work.fallback.pending_access==cls[i]);
  require(m_r[1]==a[i]);
  unsigned at=sh3_drc_work.fallback.pending_site;
  if(at<Sh3FallbackDetail::SITES) {
   require(sh3_drc_work.fallback.sites[at].pc==0x0c001000+i*2);
   require(sh3_drc_work.fallback.sites[at].delay==(i==1?1u:0u));
  }
  sh3_drc_work.fallback.finish(100+i);
 }
 require(sh3_drc_work.fallback.observed==10);
 require(sh3_drc_work.fallback.accesses[SH3_FB_WATCHED]==2);
 require(sh3_drc_work.fallback.accesses[SH3_FB_MAPPED]==1);
}
static void accumulation() {
 Sh3FallbackDetail p={};Sh3WorkCount totals[SH3_FB_ORIGINS]={},cycles[SH3_FB_ORIGINS]={};
 Sh3WorkCount ops[256]={},opcycles[256]={},accesses[SH3_FB_ACCESSES]={};
 Sh3WorkCount all_cycles=0;
 for(unsigned i=0;i<200000;++i) {
  unsigned origin=next()%SH3_FB_ORIGINS,access=next()%SH3_FB_ACCESSES;
  unsigned op=(i%3)?0x6000u|(next()&255u):next()&65535u;
  unsigned pc=(next()%128)*2,addr=next(),cost=next()&0x00ffffffu;
  p.last_origin=origin;p.begin(pc,op,addr,access,(i&1)!=0);p.finish(cost);
  ++totals[origin];cycles[origin]+=cost;all_cycles+=cost;
  if((op&0xff00u)==0x6000) {++ops[op&255];opcycles[op&255]+=cost;++accesses[access];}
 }
 require(p.observed==200000);require(p.dropped_sites>0);
 require(!memcmp(totals,p.origins,sizeof(totals)));require(!memcmp(cycles,p.origin_cycles,sizeof(cycles)));
 require(!memcmp(ops,p.hot60,sizeof(ops)));require(!memcmp(opcycles,p.hot60_cycles,sizeof(opcycles)));
 require(!memcmp(accesses,p.accesses,sizeof(accesses)));
 Sh3WorkCount n=p.dropped_sites,c=p.dropped_site_cycles;
 for(unsigned i=0;i<p.SITES;++i) {n+=p.sites[i].count;c+=p.sites[i].guest_cycles;}
 require(n==p.observed);require(c==all_cycles);require(all_cycles>0xffffffffu);
 for(unsigned a=0;a<SH3_FB_ACCESSES;++a) {
  n=0;for(unsigned o=0;o<SH3_FB_ORIGINS;++o)n+=p.hot60_cross[o][a];require(n==accesses[a]);
 }
 sh3_fallback_report(p,output);require(lines.size()<=46);require(!lines.empty());
 // Address changes aggregate under the same exact PC/opcode/context key.
 memset(&p,0,sizeof(p));p.last_origin=SH3_FB_PARTIAL;
 p.begin(0x1234,0x6012,0x100,SH3_FB_MAPPED,false);unsigned at=p.pending_site;p.finish(7);
 p.begin(0x1234,0x6012,0x200,SH3_FB_MAPPED,false);p.finish(9);
 require(p.pending_site==at && p.sites[at].count==2);
 require(p.sites[at].first_address==0x100 && p.sites[at].last_address==0x200 && p.sites[at].guest_cycles==16);
 p.last_origin=999;p.begin(0,0,0,999,false);p.finish(1);require(p.origins[SH3_FB_UNKNOWN]==1);
 // Maximum formatted counters remain bounded. Do not increment them here.
 for(unsigned i=0;i<SH3_FB_ORIGINS;++i)p.origins[i]=p.origin_cycles[i]=~(Sh3WorkCount)0;
 lines.clear();sh3_fallback_report(p,output);require(lines[1].find("18446744073709551615")!=std::string::npos);
 sh3_drc_work.clear();Sh3DrcWorkProfile zero={};require(!memcmp(&zero,&sh3_drc_work,sizeof(zero)));
 require(sizeof(Sh3FallbackDetail)<=12288);
}
int main() {
 classifier();observer();accumulation();
 printf("PASS fallback detail %u checks: 65536 opcode widths, exact metadata categories, 200000 accumulated events, collisions, cycles and pause format\n",cases);
 printf("PASS bounded profile: detail=%u bytes whole=%u; no guest data/opcode reads in observer\n",(unsigned)sizeof(Sh3FallbackDetail),(unsigned)sizeof(Sh3DrcWorkProfile));
 puts("Scope: production classifier/observer over modeled mapping metadata, not native guard execution or host-time measurement");
 return 0;
}
