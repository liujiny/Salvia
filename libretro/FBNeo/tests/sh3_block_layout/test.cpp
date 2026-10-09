// Test actual production Block snapshots and metadata together on the host.
// Synthetic entry callbacks do not execute PowerPC or predict console FPS.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT32;
typedef uint16_t UINT16;
struct Sh3PpcState { unsigned value; };
namespace Sh3Ppc {
enum { MAX_INSNS=32 };
#include "../../src/cpu/sh4/sh3_drc_block.h"
}
#include "../../src/cpu/sh4/sh3_drc_source_check.h"
// The reference layout, kept independent of the production record so the
// snapshot comparison can be checked against a second implementation. It gains
// the same per-page code generation the production record gained after its
// snapshot, which is the only change to that layout since this test was added.
struct OldBlock {
 UINT32 pc;
 const UINT16 *source;
 UINT16 original[33], words, cycles;
 bool check_read_map;
 int (*entry)(Sh3PpcState*);
 UINT32 code_gen;
};
typedef char SameRecordSize[(sizeof(OldBlock)==sizeof(Sh3Ppc::Block))?1:-1];
static int entry(Sh3PpcState *state) { return (int)++state->value; }
static unsigned cases;
static bool check(const Sh3Ppc::Block &b,const OldBlock &old,const UINT16 *source) {
 ++cases;
 bool expected=memcmp(old.original,source,old.words*sizeof(UINT16))==0;
 if(sh3_drc_source_equal(b.original,source,b.words)!=expected) return false;
 return b.pc==old.pc && b.source==old.source && b.entry==old.entry &&
  b.words==old.words && b.cycles==old.cycles && b.check_read_map==old.check_read_map;
}
int main() {
 Sh3Ppc::Block blocks[64]; OldBlock old[64]; UINT16 source[40];
 memset(blocks,0,sizeof(blocks)); memset(old,0,sizeof(old));
 for(unsigned row=0;row<64;++row) for(unsigned offset=0;offset<4;++offset)
  for(unsigned words=0;words<=33;++words) {
   for(unsigned i=0;i<40;++i) source[i]=(UINT16)(i*3511u+row*71u+words*5u);
   Sh3Ppc::Block &b=blocks[row]; OldBlock &r=old[row];
   memset(&b,0,sizeof(b)); memset(&r,0,sizeof(r));
   b.pc=r.pc=0xac000100u+row*2u;
   b.source=r.source=source+offset;
   b.entry=r.entry=entry;
   b.words=r.words=(UINT16)words; b.cycles=r.cycles=(UINT16)(words+2);
   b.check_read_map=r.check_read_map=(row&1)!=0;
   memcpy(b.original,source+offset,words*sizeof(UINT16));
   memcpy(r.original,source+offset,words*sizeof(UINT16));
   if(!check(b,r,source+offset))return 1;
   for(unsigned pos=0;pos<words;++pos) for(unsigned bit=0;bit<16;++bit) {
    source[offset+pos]^=(UINT16)(1u<<bit);
    if(!check(b,r,source+offset))return 2;
    source[offset+pos]^=(UINT16)(1u<<bit);
   }
   // Bytes after the exact validation range must not affect equality.
   source[offset+words]^=0x8001u;
   if(!check(b,r,source+offset))return 3;
   if(words<33) {
    b.original[words]^=0x8001u;
    if(!check(b,r,source+offset))return 4;
   }
   Sh3PpcState state={0};
   if(!b.entry || b.entry(&state)!=1 || state.value!=1)return 5;
  }
 memset(blocks,0,sizeof(blocks));
 for(unsigned row=0;row<64;++row) {
  const Sh3Ppc::Block &b=blocks[row];
  if(b.pc || b.source || b.entry || b.words || b.cycles || b.check_read_map)return 6;
  for(unsigned i=0;i<33;++i)if(b.original[i])return 7;
 }
 printf("PASS production Block: %u snapshot/metadata checks; 64 array positions, 0..33 lengths, every bit mutation, tails and reset\n",cases);
 printf("HOST ABI pointer=%u old_record=%u candidate_record=%u entry_offset=%u snapshot_offset=%u\n",
  (unsigned)sizeof(void*),(unsigned)sizeof(OldBlock),(unsigned)sizeof(Sh3Ppc::Block),
  (unsigned)offsetof(Sh3Ppc::Block,entry),(unsigned)offsetof(Sh3Ppc::Block,original));
 puts("Scope: host correctness; native 32-bit assertions and console measurement are separate");
 return 0;
}
