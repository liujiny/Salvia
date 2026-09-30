// Included after the SH3 maps/state are declared. Called only by Count=true.
#ifndef FBNEO_SH3_FALLBACK_OBSERVER_H
#define FBNEO_SH3_FALLBACK_OBSERVER_H
static void sh3_work_fallback_observe(unsigned opcode, unsigned pc, bool delay)
{
 unsigned size=sh3_fallback_read_size(opcode), address=0, access=SH3_FB_NOT_60_READ;
 if(size) {
  address=m_r[(opcode>>4)&15u];
  unsigned physical=address&AM, watched=0;
  bool mapped=false, mirror=false;
  if(address<0xe0000000u && !(address&(size-1u))) {
   // Inspect existing mapping metadata only. Never call a memory handler or
   // reread an opcode/data word just for diagnostics.
   const UINT8 *page=MemMapR[physical>>SH3_SHIFT];
   mapped=(uintptr_t)page>=SH3_MAXHANDLER;
#ifdef SH3_PPC_DRC
   mirror=!mapped && (uintptr_t)sh3_ppc_state.read_mirror>=SH3_MAXHANDLER &&
    (uintptr_t)page==sh3_ppc_state.mirror_handler &&
    (physical&~(unsigned)SH3_PAGEM)==sh3_ppc_state.mirror_page;
   watched=sh3_ppc_state.mirror_watch;
#endif
  }
  access=sh3_fallback_access(size,address,mapped,mirror,physical,watched);
 }
 sh3_drc_work.fallback.begin(pc,opcode,address,access,delay);
}
#endif
