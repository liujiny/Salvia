// Shared C++ dispatch control. Included after the SH3 PPC cache/compiler.
// This does not link native blocks or bypass validation of mutable guest code.
#ifndef FBNEO_SH3_DRC_DISPATCH_H
#define FBNEO_SH3_DRC_DISPATCH_H
#include "sh3_drc_source_check.h"
#include "sh3_drc_lookup.h"
#include "sh3_drc_work_profile.h"

// Keep the native loop in the caller: interpreter fallback should not pay a
// second dispatcher stack frame. This does not inline/link generated PPC blocks.
#if defined(_MSC_VER) && defined(_XBOX)
#define SH3_DISPATCH_INLINE __forceinline
#elif defined(__GNUC__)
#define SH3_DISPATCH_INLINE inline __attribute__((always_inline))
#else
#define SH3_DISPATCH_INLINE inline
#endif

template<bool Chained, bool Count> static SH3_DISPATCH_INLINE bool sh3_drc_dispatch_impl()
{
 using namespace Sh3Ppc;
 // Preserve allocate()'s failure precedence and cold initialization. On a
 // warm cache it only returns true, so avoid its out-of-line call here.
 if(Count) ++sh3_drc_work.dispatch_calls;
 if(m_sh4_icount<=0 || failed || (!blocks && !allocate())) {
  if(Count) { ++sh3_drc_work.exit_gate; sh3_drc_work.fallback.last_origin=SH3_FB_GATE; }
  return false;
 }
 // Generated entries are leaf functions: they cannot release the cache or
 // replace its allocation. Keep the dispatcher stack alive across entries.
 do {
  UINT32 pc=m_pc, phys=pc&AM;
  const UINT8 *page=MemMapF[phys>>SH3_SHIFT];
  if((uintptr_t)page<SH3_MAXHANDLER || (phys&1)) {
   if(Count) { ++sh3_drc_work.exit_fetch; sh3_drc_work.fallback.last_origin=SH3_FB_FETCH; }
   return false;
  }
  if(Count) ++sh3_drc_work.lookups;
  const UINT16 *source=(const UINT16*)(page+(phys&SH3_PAGEM));
  unsigned index=((pc>>1)^(pc>>11)^(pc>>21))&(CACHE_SETS-1);
  Lookup &set=lookup[index];
  const unsigned way=sh3_drc_lookup4(set.tag,set.next,pc);
  Block &b=blocks[index*WAYS+way];
  // Recheck EVERY entry, including successors, aliases, DMA/cheat writes and
  // changed fetch/read mappings. No cached host entry bypasses these guards.
  bool rebuild;
  if(Count) {
   // Classify before compile() overwrites the record it is derived from.
   const bool tag_hit=(b.source==source && b.pc==pc);
   const bool same=tag_hit && sh3_drc_source_equal(b.original,source,b.words);
   const bool map_ok=(!b.check_read_map || MemMapR[phys>>SH3_SHIFT]==page);
   if(tag_hit) {
    ++sh3_drc_work.validation_spans;
    sh3_drc_work.validation_words+=b.words;
   }
   if(!tag_hit) ++sh3_drc_work.rebuild_conflict;
   else if(!same) ++sh3_drc_work.rebuild_source;
   else if(!map_ok) ++sh3_drc_work.rebuild_map;
   rebuild=!tag_hit || !same || !map_ok;
  } else {
   rebuild=(b.source!=source || b.pc!=pc || !sh3_drc_source_equal(b.original,source,b.words) ||
     (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page));
  }
  if(rebuild) {
   compile(b,pc,source);
   if(Count) ++sh3_drc_work.rebuilds;
   set.tag[way]=pc;
  }
  if(!b.entry) {
   if(Count) { ++sh3_drc_work.exit_no_entry; sh3_drc_work.fallback.last_origin=SH3_FB_NO_ENTRY; }
   return false;
  }
  if(m_sh4_icount<b.cycles) {
   if(Count) { ++sh3_drc_work.exit_budget; sh3_drc_work.fallback.last_origin=SH3_FB_BUDGET; }
   return false;
  }
  // A partial/guarded block requests one interpreter step at its updated PC,
  // even when earlier blocks in this call completed. Do not return true here.
  int before=0;
  if(Count) {
   before=m_sh4_icount;
   ++sh3_drc_work.native_calls;
   ++sh3_drc_work.snapshot_lengths[b.words<=33?b.words:33];
  }
  const int completed=b.entry(&sh3_ppc_state);
  if(Count) sh3_drc_work.native_cycles+=(unsigned)(before-m_sh4_icount);
  // A compiler-tagged idle/device MOV.L needs one real handler access, but
  // no opcode refetch or trip through the outer interpreter decoder.
  bool serviced=false;
  if((completed&3)==2) serviced=sh3_drc_service_movll<Count>((unsigned)completed>>2);
  if(!completed || ((completed&3)==2 && !serviced)) {
   if(Count) { ++sh3_drc_work.exit_partial; sh3_drc_work.fallback.last_origin=SH3_FB_PARTIAL; }
   return false;
  }
  // These are exactly the outer timerhack loop's eligibility checks. Return
  // to that loop for delay slots, IRQ handling, mode changes or exhausted time.
 } while(Chained && m_sh4_icount>0 && sh3_drc_enabled && !m_delay && !m_test_irq);
 if(Count) ++sh3_drc_work.exit_boundary;
 return true;
}
// Normal callers instantiate no workload bookkeeping and no runtime Count test.
template<bool Chained> static SH3_DISPATCH_INLINE bool sh3_drc_dispatch()
{
 return sh3_drc_dispatch_impl<Chained,false>();
}
#undef SH3_DISPATCH_INLINE
#endif
