#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/cpu/sh4/sh3_drc_work_profile.h"
static unsigned lines;
static void emit(const char* text) { assert(strlen(text)<768); ++lines; }
int main()
{
 Sh3IdleCandidates p={};
 // Same fetched PC, different handler-visible PCs: a delay-slot branch target
 // must determine eligibility, not the slot address printed by fallback logs.
 p.observe(0x0c1d134c,0x0c1d1346,0x6022,0x0c002310,
           0x0c002310,0x0c1d1346,true,true,SH3_FB_PARTIAL);
 p.observe(0x0c1d134c,0x0c1d1348,0x6022,0xac002310,
           0x0c002310,0x0c1d1346,true,true,SH3_FB_PARTIAL);
 p.observe(0x0c1d134c,0x0c1d134e,0x6022,0x0c002310,
           0x0c002310,0x0c1d1346,true,false,SH3_FB_PARTIAL);
 p.observe(0x0c1d134c,0x0c1d1346,0x6022,0x0c002310,
           0x0c002310,0x0c1d1346,false,true,SH3_FB_PARTIAL);
 assert(p.watched_movll==4 && p.matching==2 && p.unregistered==1);
 // Dynamic driver values produce distinct sites and update match semantics.
 p.observe(2,6,0x6012,0x0c004000,0x0c004000,4,true,false,SH3_FB_NO_ENTRY);
 assert(p.matching==3);
 for(unsigned i=0;i<1000;++i)
  p.observe(i*4,4,0x6022,0x0c002310,0x0c002310,4,true,false,SH3_FB_PARTIAL);
 assert(p.watched_movll==1005 && p.matching==1003 && p.untracked>0);
 Sh3WorkCount tracked=0;
 for(unsigned i=0;i<p.SITES;++i) tracked+=p.sites[i].hits;
 assert(tracked+p.untracked==p.watched_movll);
 sh3_idle_candidate_report(p,emit); assert(lines==17);
 sh3_idle_candidate_report(p,NULL);
 memset(&sh3_drc_work,255,sizeof(sh3_drc_work)); sh3_drc_work.clear();
 assert(sh3_drc_work.idle_candidates.watched_movll==0);
 puts("PASS MOV.L candidate/idle-PC distinction, aliases, dynamic metadata, bounded admission and reset");
}
