// Host correctness tests for the production four-way tag probe, not Xbox FPS.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
typedef uint32_t UINT32;
#include "../../src/cpu/sh4/sh3_drc_lookup.h"
static unsigned cases;
static UINT32 rng=0x471bed23u;
static UINT32 random32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned reference(const UINT32 (&tags)[4],unsigned &next,UINT32 pc){
 unsigned way;
 for(way=0;way<4;++way)if(tags[way]==pc)break;
 if(way==4)way=next++&3u;
 return way;
}
static bool check(const UINT32 (&tags)[4],unsigned next,UINT32 pc){
 UINT32 a[4],b[4];memcpy(a,tags,sizeof(a));memcpy(b,tags,sizeof(b));
 unsigned oldnext=next,newnext=next;
 unsigned oldway=reference(a,oldnext,pc),newway=sh3_drc_lookup4(b,newnext,pc);
 ++cases;
 if(oldway!=newway||oldnext!=newnext||memcmp(a,b,sizeof(a))||memcmp(a,tags,sizeof(a))){
  printf("FAIL probe case=%u pc=%08x next=%u ways=%u/%u cursors=%u/%u\n",cases,pc,next,oldway,newway,oldnext,newnext);return false;
 }
 return true;
}
int main(){
 const UINT32 values[]={0u,2u,0x7ffffffeu,0x80000000u,0xffffffffu};
 const unsigned cursors[]={0u,1u,2u,3u,4u,0x80000000u,UINT_MAX-2,UINT_MAX-1,UINT_MAX};
 for(unsigned code=0;code<625;++code){
  unsigned v=code;UINT32 tags[4];
  for(unsigned i=0;i<4;++i){tags[i]=values[v%5];v/=5;}
  for(unsigned p=0;p<5;++p)for(unsigned n=0;n<9;++n)
   if(!check(tags,cursors[n],values[p]))return 1;
 }
 printf("PASS exhaustive tag/duplicate/zero/high-bit combinations and counter wrap: %u cases\n",cases);
 for(unsigned i=0;i<200000;++i){
  UINT32 tags[4]={random32(),random32(),random32(),random32()};
  UINT32 pc=i%5<4?tags[i%5]:random32();
  if(!check(tags,random32(),pc))return 2;
 }
 UINT32 oldtags[4]={0},newtags[4]={0};unsigned oldnext=UINT_MAX-3,newnext=oldnext;
 for(unsigned i=0;i<1000000;++i){
  if(i%997==0){memset(oldtags,0,sizeof(oldtags));memset(newtags,0,sizeof(newtags));oldnext=newnext=i%1994?0:UINT_MAX-3;}
  UINT32 pc=i%7<4?oldtags[i%7]:random32();
  unsigned a=reference(oldtags,oldnext,pc),b=sh3_drc_lookup4(newtags,newnext,pc);
  if(a!=b||oldnext!=newnext)return 3;
  oldtags[a]=pc;newtags[b]=pc;
  if(memcmp(oldtags,newtags,sizeof(oldtags)))return 4;
 }
 printf("PASS %u independent probes and 1000000 stateful replacement/reset probes\n",cases);
 puts("Scope: exact lookup semantics; not PPC execution or console frame rate");return 0;
}
