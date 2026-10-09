// Shared C++ dispatch control. Included after the SH3 PPC cache/compiler.
// This does not link native blocks or bypass validation of mutable guest code.
#ifndef FBNEO_SH3_DRC_DISPATCH_H
#define FBNEO_SH3_DRC_DISPATCH_H
#include "sh3_drc_source_check.h"
#include "sh3_drc_lookup.h"
#include "sh3_drc_work_profile.h"
#if defined(_XBOX)
#include "salvia_cv1k_probe.h"
#endif
#ifndef SALVIA_CV1K_PROBE
#define SALVIA_CV1K_PROBE 0
#endif

#if SALVIA_CV1K_STAMP_PROBE
// Write-stamp helpers (docs/optimization/2026-10-09-cv1k-write-stamps.md).
// A clean stamp pair is only meaningful while the registered window is the one
// the generated code stamps against, which is exactly the condition the
// emitter uses to decide whether to stamp a store at all. Anything else -- no
// window, a stale base pointer, a block whose bytes are not in the window --
// reports "not clean" and keeps the ordinary comparison.
static bool sh3_stamp_in_contract()
{
 return Sh3Ppc::code_stamp && Sh3Ppc::ram_window.base &&
   sh3_ppc_state.ram_base==Sh3Ppc::ram_window.base;
}
// The two 64-byte guest lines these bytes occupy, by their offset inside the
// window backing. A block is at most 33 halfwords, so a block can never span
// more than two of them, and an aligned store can never cross one.
static bool sh3_stamp_lines(const UINT16 *source, unsigned words,
 unsigned &first, unsigned &last)
{
 if(!words || !sh3_stamp_in_contract()) return false;
 const UINT32 off=(UINT32)((uintptr_t)(const UINT8*)source-(uintptr_t)Sh3Ppc::ram_window.base);
 const UINT32 end=off+(UINT32)words*2-1;
 if(off>Sh3Ppc::ram_window.mask || end>Sh3Ppc::ram_window.mask) return false;
 first=off>>Sh3Ppc::STAMP_SHIFT;
 last=end>>Sh3Ppc::STAMP_SHIFT;
 return true;
}
static bool sh3_stamp_clean(const UINT16 *source, unsigned words)
{
 unsigned first=0,last=0;
 if(!sh3_stamp_lines(source,words,first,last)) return false;
 return Sh3Ppc::code_stamp[first]==0 && (first==last || Sh3Ppc::code_stamp[last]==0);
}
// Called when a validation has just proved these bytes still match the block's
// snapshot: from here the lines are clean again until a store touches them.
static void sh3_stamp_clear(const UINT16 *source, unsigned words)
{
 unsigned first=0,last=0;
 if(!sh3_stamp_lines(source,words,first,last)) return;
 Sh3Ppc::code_stamp[first]=0;
 if(last!=first) Sh3Ppc::code_stamp[last]=0;
}
#endif

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
#if SALVIA_CV1K_CHAIN_PROBE
 // One chain starts here: nothing in it may be assumed valid yet.
 sh3_chain_dirty=0;
#endif
 if(m_sh4_icount<=0 || failed || (!blocks && !allocate())) {
  if(Count) { ++sh3_drc_work.exit_gate; sh3_drc_work.fallback.last_origin=SH3_FB_GATE; }
  return false;
 }
 // Generated entries are leaf functions: they cannot release the cache or
 // replace its allocation. Keep the dispatcher stack alive across entries.
 do {
#if SALVIA_CV1K_PROBE
  // Three spans per sampled entry: the pre-entry work (fetch, lookup,
  // validation, any recompile), the generated block call, and the post-entry
  // work. Compared with the whole drc_dispatch phase they show how much of the
  // dispatch path is instructions and how much is the unattributed remainder.
  static unsigned probeTick=0;
  const bool probeNow=((++probeTick&63u)==0);
  unsigned long long probeT0=0,probeStart=0,probeEnd=0;
  if(probeNow) probeT0=salvia_cv1k_tick();
#endif
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
#if SALVIA_CV1K_STAMP_PROBE
    // Write-stamp cross-check. This specialization compares anyway, so it is
    // the audit of the skip below: `clean` is the share of validations a clean
    // stamp pair would have removed, and `missed` counts the entries where the
    // stamps said clean while the comparison said the bytes had changed --
    // a write path that is not hooked, and the one result that makes the skip
    // unsafe. It must stay zero.
    const bool stamps_clean=sh3_stamp_clean(source,b.words);
    ++sh3_drc_work.stamp_entries;
    if(stamps_clean) {
     ++sh3_drc_work.stamp_clean;
     if(!same) ++sh3_drc_work.stamp_missed;
    } else ++sh3_drc_work.stamp_dirty;
    // The ordinary validation credits the stamps, exactly as it will when the
    // hot path is allowed to rely on them.
    if(same) sh3_stamp_clear(source,b.words);
#endif
#if SALVIA_CV1K_CHAIN_PROBE
    // Could this entry have been linked? Only if no store has run since the
    // chain started, which is exactly what a chain-scoped link would test.
    if(sh3_chain_dirty) ++sh3_drc_work.chain_dirty_entries;
    else ++sh3_drc_work.chain_clean_entries;
#endif
   }
   if(!tag_hit) ++sh3_drc_work.rebuild_conflict;
   else if(!same) ++sh3_drc_work.rebuild_source;
   else if(!map_ok) ++sh3_drc_work.rebuild_map;
   rebuild=!tag_hit || !same || !map_ok;
  } else {
#if SALVIA_CV1K_STAMP_PROBE >= 2
   // Rely on the stamps: no store has written the lines this block's bytes
   // occupy since the dispatcher last proved them equal to its snapshot, so
   // the comparison cannot have changed its answer. Everything else -- the tag
   // match, the fetch page, the source pointer, the read-map test -- is
   // checked exactly as before, and a dirty pair still takes the comparison.
   const bool tag_hit=(b.source==source && b.pc==pc);
   const bool stamps_clean=tag_hit && sh3_stamp_clean(source,b.words);
   const bool same=tag_hit && (stamps_clean || sh3_drc_source_equal(b.original,source,b.words));
   if(same) sh3_stamp_clear(source,b.words);
   rebuild=!same || (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page);
#else
   rebuild=(b.source!=source || b.pc!=pc || !sh3_drc_source_equal(b.original,source,b.words) ||
     (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page));
#endif
  }
  if(rebuild) {
   compile(b,pc,source);
   arena_note_slot(index*WAYS+way,b);
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
   ++sh3_drc_work.block_ends[slot_sector[index*WAYS+way]>>5];
   // Guest stores this block will perform, weighted by its entry count. Only
   // the sampled dispatch specialization reads the diagnostic array.
   sh3_drc_work.block_stores+=slot_stores[index*WAYS+way];
  }
#if SALVIA_CV1K_PROBE
  if(probeNow) probeStart=salvia_cv1k_tick();
#endif
  const int completed=b.entry(&sh3_ppc_state);
#if SALVIA_CV1K_PROBE
  if(probeNow) probeEnd=salvia_cv1k_tick();
#endif
  if(Count) sh3_drc_work.native_cycles+=(unsigned)(before-m_sh4_icount);
  // A compiler-tagged idle/device MOV.L needs one real handler access, but
  // no opcode refetch or trip through the outer interpreter decoder.
  bool serviced=false;
  if((completed&3)==2) serviced=sh3_drc_service_movll<Count>((unsigned)completed>>2);
#if SALVIA_CV1K_PROBE
  if(probeNow) {
   const unsigned long long probeT3=salvia_cv1k_tick();
   if(probeT0 && probeStart) salvia_cv1k_probe[12]+=probeStart-probeT0;
   if(probeStart && probeEnd) { salvia_cv1k_probe[6]+=probeEnd-probeStart; ++salvia_cv1k_probe[7]; }
   if(probeEnd && probeT3) salvia_cv1k_probe[13]+=probeT3-probeEnd;
  }
#endif
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
 // The CV1000 frame loop enters sh3_drc_dispatch_impl directly, so a probe here
 // would read zero. Probe the chained loop itself when that number is needed.
 return sh3_drc_dispatch_impl<Chained,false>();
}
#undef SH3_DISPATCH_INLINE
#endif
