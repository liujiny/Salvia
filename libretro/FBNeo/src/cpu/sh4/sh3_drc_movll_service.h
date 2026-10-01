// Included after RL, MOVLL, IRQ handling and the PPC dispatcher are declared.
// Handles ONLY the board-declared idle watched load. No direct RAM shortcut.
#ifndef FBNEO_SH3_DRC_MOVLL_SERVICE_H
#define FBNEO_SH3_DRC_MOVLL_SERVICE_H
template<bool Count> static bool sh3_drc_service_movll(unsigned opcode)
{
 if(!sh3_drc_enabled || (opcode&0xf00fu)!=0x6002u ||
    !sh3_idle_watch_ram || !sh3_idle_watch_pc) {
  if(Count) ++sh3_drc_work.movll_service_rejects;
  return false;
 }
 const unsigned address=m_r[(opcode>>4)&15u], physical=address&AM;
 // Read live board metadata each time. Remapping/handler replacement revokes
 // the existing mirror; never use stale host RAM or a fixed game address.
 const unsigned idle_ram=*sh3_idle_watch_ram, idle_pc=*sh3_idle_watch_pc;
 const unsigned handler_pc=(m_delay?m_pc:m_pc+2u)&AM;
 if(address>=0xe0000000u || (address&3u) || physical!=idle_ram ||
    physical!=sh3_ppc_state.mirror_watch ||
    (physical&~(unsigned)SH3_PAGEM)!=sh3_ppc_state.mirror_page ||
    (uintptr_t)sh3_ppc_state.read_mirror<SH3_MAXHANDLER ||
    (uintptr_t)MemMapR[physical>>SH3_SHIFT]!=sh3_ppc_state.mirror_handler ||
    (handler_pc!=idle_pc && handler_pc!=idle_pc+2u)) {
  if(Count) ++sh3_drc_work.movll_service_rejects;
  return false;
 }
 int before=0;
 if(Count) {
  before=m_sh4_icount; ++sh3_drc_work.movll_services;
  sh3_drc_work.idle_candidates.observe(m_delay?m_delay:m_pc,handler_pc,
   opcode,address,idle_ram,idle_pc,true,m_delay!=0,SH3_FB_UNKNOWN);
 }
 // Exactly the original interpreter instruction prologue. In a delay slot
 // the branch target and PR are already committed by the compiled branch.
 if(m_delay) m_delay=0; else m_pc+=2;
 m_ppc=m_pc;
 MOVLL((UINT16)opcode); // unchanged EA update -> RL -> WaitState/ReadLong
 if(m_test_irq && !m_delay) sh4_check_pending_irq();
 EAT(1); // base instruction cost AFTER handler/IRQ, as in timerhack
 if(Count) sh3_drc_work.movll_service_cycles+=(unsigned)(before-m_sh4_icount);
 return true;
}
#endif
