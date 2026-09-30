// Compare the production split table with an independent old interleaved table.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
typedef uint32_t UINT32;
enum { CACHE_SETS=8192, WAYS=4 };
#include "../../src/cpu/sh4/sh3_drc_lookup_table.h"
#include "../../src/cpu/sh4/sh3_drc_lookup.h"
struct OldSet { UINT32 tag[WAYS]; unsigned next; };
static OldSet old_table[CACHE_SETS];
static unsigned rng=0x36f07a1du;
static unsigned random32() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static bool equal(const LookupTable& t) {
 for(unsigned s=0;s<CACHE_SETS;++s)
  if(memcmp(old_table[s].tag,t.tag[s],sizeof(t.tag[s])) || old_table[s].next!=t.next[s])return false;
 return true;
}
int main() {
 LookupTable* t=(LookupTable*)calloc(1,sizeof(LookupTable));
 if(!t)return 1;
 if(sizeof(*t)!=sizeof(old_table) || offsetof(LookupTable,next)!=CACHE_SETS*16)return 2;
 if(!equal(*t))return 3;
 unsigned clears=0, hits=0, misses=0;
 for(unsigned i=0;i<2000000;++i) {
  unsigned s=random32()&(CACHE_SETS-1);
  if(i%10007==0) {
   if(!equal(*t))return 4;
   memset(old_table,0,sizeof(old_table)); memset(t,0,sizeof(*t)); ++clears;
   // All bits, duplicated and zero tags; cursor wrap must remain unsigned.
   for(unsigned j=0;j<CACHE_SETS;++j) {
    old_table[j].next=t->next[j]=UINT_MAX-(j&3);
    for(unsigned w=0;w<WAYS;++w)old_table[j].tag[w]=t->tag[j][w]=(j&1)?0:0x80000000u;
   }
  }
  UINT32 pc=(i%7<4)?old_table[s].tag[i%7]:random32();
  unsigned way;
  for(way=0;way<WAYS;++way)if(old_table[s].tag[way]==pc)break;
  if(way==WAYS) {way=old_table[s].next++&3u;++misses;} else ++hits;
  unsigned got=sh3_drc_lookup4(t->tag[s],t->next[s],pc);
  if(way!=got || old_table[s].next!=t->next[s])return 5;
  old_table[s].tag[way]=pc;t->tag[s][got]=pc;
  // Model compile() recycling the whole arena after a way was selected.
  if(i%7919==0) {
   memset(old_table,0,sizeof(old_table));memset(t,0,sizeof(*t));++clears;
   old_table[s].tag[way]=pc;t->tag[s][got]=pc;
  }
 }
 if(!equal(*t))return 6;
 printf("PASS 2000000 independent old/split-table operations: hits=%u misses=%u clears=%u; all tags/cursors identical\n",hits,misses,clears);
 printf("PASS one allocation=%u bytes, hot tags=%u bytes, miss-only cursors=%u bytes, set stride=16\n",(unsigned)sizeof(*t),(unsigned)sizeof(t->tag),(unsigned)sizeof(t->next));
 free(t);
 t=(LookupTable*)calloc(1,sizeof(LookupTable));
 if(!t)return 7;
 memset(old_table,0,sizeof(old_table));if(!equal(*t))return 8;free(t);
 puts("PASS free/reallocate/reset; scope: host semantics, not PPC execution or console FPS");
 return 0;
}
