// Shared C++ dispatch control. Included after the SH3 PPC cache/compiler.
// This does not link native blocks or bypass validation of mutable guest code.
#ifndef FBNEO_SH3_DRC_DISPATCH_H
#define FBNEO_SH3_DRC_DISPATCH_H
#include "sh3_drc_source_check.h"
#include "sh3_drc_lookup.h"

template<bool Chained> static bool sh3_drc_dispatch()
{
 using namespace Sh3Ppc;
 if(m_sh4_icount<=0 || !allocate())return false;
 // Generated entries are leaf functions: they cannot release the cache or
 // replace its allocation. Keep the dispatcher stack alive across entries.
 do {
  UINT32 pc=m_pc, phys=pc&AM;
  const UINT8 *page=MemMapF[phys>>SH3_SHIFT];
  if((uintptr_t)page<SH3_MAXHANDLER || (phys&1))return false;
  const UINT16 *source=(const UINT16*)(page+(phys&SH3_PAGEM));
  unsigned index=((pc>>1)^(pc>>11)^(pc>>21))&(CACHE_SETS-1);
  Lookup &set=lookup[index];
  const unsigned way=sh3_drc_lookup4(set.tag,set.next,pc);
  Block &b=blocks[index*WAYS+way];
  // Recheck EVERY entry, including successors, aliases, DMA/cheat writes and
  // changed fetch/read mappings. No cached host entry bypasses these guards.
  if(b.source!=source || b.pc!=pc || !sh3_drc_source_equal(b.original,source,b.words) ||
     (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page)) {
   compile(b,pc,source);
   set.tag[way]=pc;
  }
  if(!b.entry || m_sh4_icount<b.cycles)return false;
  // A partial/guarded block requests one interpreter step at its updated PC,
  // even when earlier blocks in this call completed. Do not return true here.
  if(!b.entry(&sh3_ppc_state))return false;
  // These are exactly the outer timerhack loop's eligibility checks. Return
  // to that loop for delay slots, IRQ handling, mode changes or exhausted time.
 } while(Chained && m_sh4_icount>0 && sh3_drc_enabled && !m_delay && !m_test_irq);
 return true;
}
#endif
