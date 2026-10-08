// SH3 integer block recompiler for the Xbox 360's big-endian PowerPC CPU.
// Included by sh4.cpp: the interpreter remains the authority for slow paths.
// Generated leaf functions use only volatile r0/r3-r12, CR0 and XER. No calls,
// stack frames, nonvolatile registers, or floating point state are involved.
#ifndef FBNEO_SH3_DRC_PPC_H
#define FBNEO_SH3_DRC_PPC_H

#if defined(LSB_FIRST) || (!defined(_XBOX) && !defined(__powerpc__))
#error The SH3 PPC recompiler requires a 32-bit big-endian PowerPC ABI
#endif
#ifdef _XBOX
#include <ppcintrinsics.h>
#else
#include <sys/mman.h>
#endif

// Arena size override, so the host differential harness can select an arena
// size from otherwise identical sources. Overflow runs the full reset in
// compile(), which discards every compiled block and makes the guest rebuild
// its whole native working set through the interpreter fallback.
//
// The 131072-entry table removes the set conflicts that made the arena fill
// up, so the measured knee is now 20 MiB: the host sweep recorded the same
// 43214 rebuilds and the same 17 MiB high-water mark at 20, 24, 32 and 64 MiB,
// while 16 MiB already regressed to 58511 rebuilds. On a 512 MiB console the
// arena is a static array in .bss, and the Xenos compositor still has to find
// room for its atlas, so the arena is pinned to the knee instead of the cap.
#ifndef SH3_PPC_CACHE_BYTES
#define SH3_PPC_CACHE_BYTES (20 * 1024 * 1024)
#endif

// Number of block-table entries, four ways per set. The original 32768-entry
// table thrashing on set conflicts was measured as the dominant rebuild cause
// on CV1000; the override lets host differential tests size it from identical
// sources.
#ifndef SH3_PPC_TABLE_SIZE
#define SH3_PPC_TABLE_SIZE 131072
#endif

namespace Sh3Ppc {
enum { CACHE_BYTES = SH3_PPC_CACHE_BYTES, TABLE_SIZE = SH3_PPC_TABLE_SIZE, WAYS = 4, CACHE_SETS = TABLE_SIZE / WAYS,
       MAX_INSNS = 32, MAX_WORDS = 4096, HOST_REGS = 6 };
// Host general registers used by the generated code: r3 is the state pointer,
// r4 keeps the registered RAM window base for the whole block, r5..r10 are the
// cached guest register slots, r11/r12 and r0 are the address/immediate
// scratch. Keeping the base in r4 costs one guest slot but removes the 32-bit
// host-pointer materialisation from every translated memory access.
enum { FIRST_SLOT = 5, RAM_BASE_REG = 4 };
enum { G_SR = 16, G_MACL, G_MACH, G_PR, G_GBR };
#define SO(field) ((int)offsetof(Sh3PpcState, field))

#include "sh3_drc_block.h"
// Four tags share one cache line. Separate metadata keeps misses from
// touching four instruction snapshots. Round-robin eviction runs on misses.
struct Lookup { UINT32 tag[WAYS]; unsigned next; };
static Lookup *lookup;
static Block *blocks;
typedef char PointerAbiMustBe32Bits[(sizeof(void*) == 4) ? 1 : -1];
static UINT32 *code;
static unsigned used;
static bool failed;
// Board-declared data RAM. Registration verifies every page, including the
// optional read-handler mirror. Mapping/handler changes revoke this contract
// and flush generated code before any embedded host pointer can be reused.
struct RamWindow {
 UINT8 *base;
 UINT32 start, span, mask, watch;
 int span_bits, backing_bits;
};
static RamWindow ram_window;
#ifdef _XBOX
// The XEX's static data is executable, as used by the existing PPC cores.
// XPhysicalAlloc and the CRT heap must not be used for generated instructions.
static __declspec(align(128)) UINT32 xbox_code[CACHE_BYTES / 4];
static __declspec(naked) void invalidate_line(int offset, const void *base)
{
 __asm { icbi r3, r4 }
 __asm { blr }
}
#endif

static void sync_code(UINT32 *begin, UINT32 *end)
{
#ifdef _XBOX
 char *p = (char*)((uintptr_t)begin & ~(uintptr_t)127);
 for (; p < (char*)end; p += 128) __dcbst(0, p);
 __sync();
 p = (char*)((uintptr_t)begin & ~(uintptr_t)127);
 for (; p < (char*)end; p += 128) invalidate_line(0, p);
 __sync();
 __emit(0x4c00012c); // isync
#else
 __builtin___clear_cache((char*)begin, (char*)end);
#endif
}

static int reg_offset(int guest)
{
 if (guest < 16) return SO(r) + guest * 4;
 switch (guest) {
  case G_SR: return SO(sr); case G_MACL: return SO(macl);
  case G_MACH: return SO(mach); case G_PR: return SO(pr);
  default: return SO(gbr);
 }
}

struct Slot { int guest, age; bool dirty, locked; };
struct BlockExit {
 UINT32 *branches[8];
 int count, cycles;
 UINT32 pc, target;
 bool delay_slot, conditional;
 unsigned service_opcode;
 Slot slots[HOST_REGS];
};
struct CodeWriteCheck { UINT32 *compare; unsigned size; };
// Compile-time accounting of emitted words per phase. The emitter runs only on
// compilation, so counting here costs nothing at frame time and is available on
// both host and console. Averages per compiled block say where the generated
// code actually goes.
enum { CAT_BODY=0, CAT_MEMADDR, CAT_MEMGUARD, CAT_MEMACCESS, CAT_COMPLETE, CAT_EXIT, CAT_COUNT };
static unsigned long long sh3_gen_words[CAT_COUNT];
static unsigned long long sh3_gen_blocks;

struct Compiler {
 UINT32 *start, *out;
 UINT32 pc, source_begin, block_pc;
 int cat;
 unsigned long long cat_words[CAT_COUNT];
 Slot slots[HOST_REGS];
 int clock, cycles, exit_count, write_check_count;
 // A watched read may need separate general, service and mirror guards.
 BlockExit exits[MAX_INSNS*3];
 CodeWriteCheck write_checks[MAX_INSNS];
 bool ended, folded_slot, in_delay_slot;
 // Every guard exit and the block's own completion end with the same
 // "publish pc/ppc, charge the guest cycles, return to the dispatcher"
 // sequence. Emit it once per block instead of once per exit: each caller
 // jumps to it with the pc value in r0, the cycles in r12 and the dispatcher
 // result code in r5, all of which are dead once the cached guest registers
 // have been written back. Exits are cold, so the extra branch costs nothing
 // on the hot path; what it removes is a large share of the emitted words.
 UINT32 *tail_pc_jump[MAX_INSNS*3+4], *tail_nopc_jump[MAX_INSNS*3+4];
 int tail_pc_jumps, tail_nopc_jumps;

 Compiler(UINT32 *dest, UINT32 address, const UINT16 *source)
  : start(dest), out(dest), pc(address), block_pc(address), clock(0), cycles(0), exit_count(0), write_check_count(0), ended(false), folded_slot(false), in_delay_slot(false), tail_pc_jumps(0), tail_nopc_jumps(0)
 {
  source_begin = (UINT32)(uintptr_t)source;
  cat = CAT_BODY;
  for (int i = 0; i < CAT_COUNT; i++) cat_words[i] = 0;
  for (int i = 0; i < HOST_REGS; i++) {
   slots[i].guest = -1; slots[i].age = 0;
   slots[i].dirty = slots[i].locked = false;
  }
  // Keep the registered RAM window base in r4 for the whole block. The
  // window cannot move while generated code lives (registration, mapping and
  // handler changes all revoke it), so this is a one-word prologue that
  // replaces a two-word host-pointer materialisation per translated access.
  if (ram_base_registered()) {
   cat = CAT_MEMADDR;
   load(RAM_BASE_REG, SO(ram_base));
   cat = CAT_BODY;
  }
 }
 void emit(UINT32 x) { *out++ = x; ++cat_words[cat]; }
 // Attribute every word emitted inside the scope to one phase.
 struct CatGuard {
  Compiler &c; int save;
  CatGuard(Compiler &owner, int k) : c(owner), save(owner.cat) { c.cat = k; }
  ~CatGuard() { c.cat = save; }
 };
 static UINT32 d(int op, int rt, int ra, int imm) {
  return ((UINT32)op << 26) | (rt << 21) | (ra << 16) | (imm & 0xffff);
 }
 static UINT32 x(int rt, int ra, int rb, int xo) {
  return 0x7c000000 | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1);
 }
 void load(int rt, int off) { emit(d(32, rt, 3, off)); }
 void store(int rt, int off) { emit(d(36, rt, 3, off)); }
 void imm(int rt, UINT32 value) {
  if ((INT32)value >= -32768 && (INT32)value <= 32767) emit(d(14, rt, 0, value));
  else {
   emit(d(15, rt, 0, value >> 16));
   if (value & 0xffff) emit(d(24, rt, rt, value));
  }
 }
 void move(int dst, int src) { if (dst != src) emit(x(src, dst, src, 444)); }
 void addi(int dst, int src, int value) { emit(d(14, dst, src, value)); }
 void add(int dst, int a, int b) { emit(x(dst, a, b, 266)); }
 void sub(int dst, int a, int b) { emit(x(dst, b, a, 40)); }
 void logic(int dst, int a, int b, int xo) { emit(x(a, dst, b, xo)); }
 void rotate(int dst, int src, int sh, int mb, int me, bool insert = false) {
  emit(((UINT32)(insert ? 20 : 21) << 26) | (src << 21) | (dst << 16) |
       (sh << 11) | (mb << 6) | (me << 1));
 }
 void cmp(int a, int b, bool uns = false) { emit(x(0, a, b, uns ? 32 : 0)); }
 void cmpi(int a, int value, bool uns = false) { emit(d(uns ? 10 : 11, 0, a, value)); }
 UINT32 *branch(int bit, bool set) {
  UINT32 *p = out; emit(0x40000000 | ((set ? 12 : 4) << 21) | (bit << 16)); return p;
 }
 UINT32 *jump() { UINT32 *p = out; emit(0x48000000); return p; }
 void patch(UINT32 *p) { *p |= ((UINT32)((char*)out - (char*)p) & ((*p >> 26) == 18 ? 0x03fffffc : 0xfffc)); }
 void constant(int off, UINT32 value) { imm(0, value); store(0, off); }

 void unlock() { for (int i = 0; i < HOST_REGS; i++) slots[i].locked = false; }
 int reg(int guest, bool read = true) {
  int found = -1, oldest = 0x7fffffff;
  for (int i = 0; i < HOST_REGS; i++) {
   if (slots[i].guest == guest) { found = i; break; }
   if (!slots[i].locked && (slots[i].guest < 0 || slots[i].age < oldest)) {
    oldest = slots[i].guest < 0 ? -1 : slots[i].age; found = i;
   }
  }
  Slot &s = slots[found];
  if (s.guest != guest) {
   if (s.dirty) store(found + FIRST_SLOT, reg_offset(s.guest));
   s.guest = guest; s.dirty = false;
   if (read) load(found + FIRST_SLOT, reg_offset(guest));
  }
  s.age = ++clock; s.locked = true;
  return found + FIRST_SLOT;
 }
 void dirty(int host) { slots[host - FIRST_SLOT].dirty = true; }
 void flush() {
  for (int i = 0; i < HOST_REGS; i++)
   if (slots[i].dirty) store(i + FIRST_SLOT, reg_offset(slots[i].guest));
 }
 void charge(int count) {
  load(11, SO(total)); addi(11, 11, count); store(11, SO(total));
  load(11, SO(icount)); addi(11, 11, -count); store(11, SO(icount));
 }
 void finish(UINT32 next, int cost, int result, bool set_pc = true) {
  CatGuard catScope(*this, cat == CAT_EXIT ? CAT_EXIT : CAT_COMPLETE);
  flush();
  imm(0,next);
  imm(12,cost);
  imm(5,result);
  tail_jump(set_pc); // the shared tail stores pc/ppc, charges and returns
 }
 void tail_jump(bool set_pc) {
  UINT32 *p = jump();
  if (set_pc) tail_pc_jump[tail_pc_jumps++] = p;
  else tail_nopc_jump[tail_nopc_jumps++] = p;
 }
 // One per block, shared by every guard exit and by the block's completion.
 // Emitted last and reached only by an unconditional branch, so it never
 // falls through from the exit it follows.
 void emit_tail() {
  if (!tail_pc_jumps && !tail_nopc_jumps) return;
  CatGuard catScope(*this, CAT_EXIT);
  if (tail_pc_jumps) {
   for (int i = 0; i < tail_pc_jumps; i++) patch(tail_pc_jump[i]);
   store(0, SO(pc));
  }
  for (int i = 0; i < tail_nopc_jumps; i++) patch(tail_nopc_jump[i]);
  store(0, SO(ppc));
  load(11, SO(total)); load(6, SO(icount));
  add(11, 11, 12); sub(6, 6, 12);
  store(11, SO(total)); store(6, SO(icount));
  move(3, 5);
  emit(0x4e800020); // blr
 }
 // Branch out of the straight-line fast path only when a guard fails.
 // All guards for one memory operation share an exit before that operation.
 void guard(int bit, bool set, unsigned service_opcode=0) {
  CatGuard catScope(*this, CAT_MEMGUARD);
  if (!exit_count || exits[exit_count-1].pc != pc || exits[exit_count-1].conditional || exits[exit_count-1].service_opcode!=service_opcode) {
   BlockExit &e = exits[exit_count++];
   e.count = 0; e.pc = pc; e.cycles = cycles;
   e.delay_slot = in_delay_slot;
   e.conditional = false; e.service_opcode=service_opcode;
   memcpy(e.slots, slots, sizeof(slots));
  }
  BlockExit &e = exits[exit_count-1];
  e.branches[e.count++] = branch(bit, !set);
 }
 // A distinct taken edge encodes the validated MOV.L in its return value.
 // Generated functions stay leaf functions; the dispatcher calls RL safely
 // through the normal C++ ABI, with all cached guest registers committed.
 void guard_watch(unsigned opcode) {
  guard(2,false,opcode); // equality with the watched longword
 }
 void emit_exits() {
  CatGuard catScope(*this, CAT_EXIT);
  for (int i = 0; i < exit_count; i++) {
   BlockExit &e = exits[i];
   for (int j = 0; j < e.count; j++) patch(e.branches[j]);
   memcpy(slots, e.slots, sizeof(slots));
   flush(); // this exit's own dirty set, before r5/r12 become arguments
   if (e.conditional) {
    // pc and ea carry the taken target, ppc keeps the fall-through address.
    // ppc is the only value the shared tail can publish here, because pc and
    // ea differ from it; r0 holds e.pc when the tail runs.
    imm(0,e.target); store(0,SO(pc)); store(0,SO(ea));
    imm(0,e.pc); store(0,SO(ppc));
    imm(12,e.cycles); imm(5,1);
    tail_jump(false);
    continue;
   }
   // The branch has already committed its target and PR. Publish the
   // pending slot only on this slow path, so the interpreter executes it
   // exactly once with the state it would see after interpreting the branch.
   if (e.delay_slot) constant(SO(delay),e.pc);
   else imm(0,e.pc);
   imm(12,e.cycles);
   imm(5,e.service_opcode ? (int)(2u|(e.service_opcode<<2)) : 0);
   tail_jump(!e.delay_slot);
  }
  emit_tail();
 }
 void protect_code(int words) {
  for (int i=0;i<write_check_count;i++) {
   CodeWriteCheck &w=write_checks[i];
   UINT32 begin=source_begin&~(w.size-1);
   UINT32 end=(source_begin+words*2+w.size-1)&~(w.size-1);
   *w.compare |= end-begin;
  }
 }
 void set_t(int value) {
  int sr = reg(G_SR);
  // T is the low bit of G_SR. From r0 (a 0/1 produced by compare_t or a shift)
  // one rlwimi replaces the clear-then-OR pair; setting T outright is a single
  // OR; only clearing needs the masked move.
  if (value < 0) rotate(sr, 0, 0, 31, 31, true); // rlwimi sr,r0,0,31,31
  else if (value) emit(d(24, sr, sr, 1));        // ori sr,sr,1
  else rotate(sr, sr, 0, 0, 30);                 // clear T, keep the rest
  dirty(sr);
 }
 void compare_t(int bit, bool set) {
  emit(0x7c000026); // mfcr r0
  rotate(0, 0, bit + 1, 31, 31);
  if (!set) emit(d(26, 0, 0, 1)); // xori
  set_t(-1);
 }

 // Effective guest address -> r11. Special base -1 = constant, -2 = G_GBR.
 void address(int base, int index, int disp, UINT32 absolute) {
  if (base == -1) imm(11, absolute);
  else {
   int b = reg(base == -2 ? G_GBR : base);
   if (index >= 0) { int ix = reg(index); add(11, b, ix); }
   else addi(11, b, disp);
  }
 }
 // True when r4 really holds the registered RAM window base. The driver field
 // is refreshed wherever the window is set; an out-of-sync pair falls back to
 // the immediate form rather than trusting the register.
 bool ram_base_registered() const {
  return ram_window.base && sh3_ppc_state.ram_base == ram_window.base;
 }
 // rD = ram_window.base + off, where off is an offset inside the window.
 void ram_address(int rd, UINT32 off) {
  if (ram_base_registered() && off <= 32767) addi(rd, RAM_BASE_REG, (int)off);
  else imm(rd, (UINT32)(uintptr_t)ram_window.base + off);
 }
 // rD += ram_window.base: the direct path already holds a window-relative
 // offset in rD, so one register add replaces lis/ori plus an add.
 void ram_bias(int rd) {
  if (ram_base_registered()) add(rd, rd, RAM_BASE_REG);
  else { imm(0, (UINT32)(uintptr_t)ram_window.base); add(rd, rd, 0); }
 }
 bool memory(bool write, int size, int value, int base, int index, int disp,
             UINT32 absolute, bool ea, bool pre, bool post, unsigned service_opcode=0) {
  CatGuard catScope(*this, CAT_MEMADDR);
  const RamWindow &ram=ram_window;
  const unsigned device_opcode=(sh3_device_reads[0].callback || sh3_device_reads[1].callback)?service_opcode:0;
  bool constant_ram=ram.base && base==-1 &&
   ((absolute&0x7fffffffu)&~(ram.span-1))==ram.start;
  bool direct=constant_ram || (ram.base && base!=-1 &&
   ((block_pc&0x7fffffffu)&~(ram.span-1))==ram.start);
  // A constant PC-relative operand needs no runtime address translation.
  // Keep real memory reads: literal data can change without changing code.
  if (constant_ram && ((absolute&(size-1)) ||
      (!write && size==4 && (absolute&AM)==ram.watch))) return false;
  // A load overwrites its destination. Its old value is needed only when
  // that same guest register supplies the address. Guard exits preserve an
  // uncached destination in architectural RAM; already dirty slots retain
  // their normal snapshots and flushes even when read is false.
  bool address_value=value==base || value==index || (base==-2 && value==G_GBR);
  int data = reg(value,write || address_value);
  if (constant_ram) {
   // A compile-time-bound RAM operand needs no runtime translation at all.
   ram_address(12,(absolute-ram.start)&ram.mask);
  } else if (direct) {
   address(base,index,disp,absolute);
   // Match every external alias accepted by the interpreter, including
   // the uncached acxxxxxx addresses used heavily by CV1000. Internal
   // addresses >= e0000000 must never alias ordinary RAM. Word compares
   // and rotates also discard Xenon's upper 32 general-register bits.
   //
   // Bits 29..31 of the effective address are its area select and bits 0..1
   // its alignment. Rotating left by three places both fields next to each
   // other, so one five-bit (three-bit + 2-byte alignment) or four-bit
   // (three-bit + halfword alignment) extract is "<= 6" exactly when the
   // address is outside the internal area and aligned. That folds the
   // alignment guard into the area guard, saving a rotate, a compare and a
   // branch on every translated word and long access.
   if (size > 1) {
    rotate(0,11,3,size==2?28:27,31);
    cmpi(0,6,true); guard(1,false,device_opcode);
   } else {
    rotate(0,11,3,29,31); cmpi(0,7,true); guard(0,true,device_opcode);
   }
   rotate(0,11,32-ram.span_bits,ram.span_bits+3,31);
   cmpi(0,ram.start>>ram.span_bits,true); guard(2,true,device_opcode);
   rotate(12,11,0,32-ram.span_bits,31);
   if(!write && size==4 && ram.watch!=0xffffffffu) {
    UINT32 watch_off=ram.watch-ram.start;
    if(watch_off<=0xffff) { cmpi(12,(int)watch_off,true); guard(2,false,service_opcode); }
    else { imm(0,watch_off); cmp(12,0); guard_watch(service_opcode); }
   }
   if(ram.backing_bits!=ram.span_bits)rotate(12,12,0,32-ram.backing_bits,31);
   ram_bias(12);
  } else {
   address(base, index, disp, absolute);
   // All address arithmetic is explicitly reduced to 32 bits for Xenon.
   rotate(11, 11, 0, 0, 31);
   imm(0, 0xe0000000); cmp(11, 0, true); guard(0, true,device_opcode);
   if (size > 1) { emit(d(28, 11, 0, size - 1)); guard(2, true,device_opcode); }
   rotate(11, 11, 0, 3, 31); // SH3 physical address mask (AM)
   rotate(0, 11, 18, 14, 29); // (address >> 16) * sizeof(pointer)
   load(12, write ? SO(write_map) : SO(read_map));
   emit(x(12, 12, 0, 23)); // lwzx map entry
   cmpi(12, SH3_MAXHANDLER, true);
   if (write) guard(0, false);
   else {
    UINT32 *mapped = branch(0, false);
    // CV1000 routes a busy RAM page through its idle-loop read handler.
    // The board can explicitly expose that RAM while keeping the watched
    // longword on its real handler. Explicitly registered device reads can
    // use the tagged service exit; other handlers still fall back normally.
    load(0, SO(mirror_handler)); cmp(12, 0); guard(2, true,device_opcode);
    rotate(0, 11, 0, 0, 15); load(12, SO(mirror_page)); cmp(0, 12); guard(2, true,device_opcode);
    if (size == 4) { load(12, SO(mirror_watch)); cmp(11, 12); guard_watch(service_opcode); }
    load(12, SO(read_mirror)); cmpi(12, SH3_MAXHANDLER, true); guard(0, false);
    patch(mapped);
   }
   rotate(11, 11, 0, 16, 31);
   add(12, 12, 11);
  }
  if (write) {
   // Code and ordinary data often share a page. Guard only stores that
   // overlap this block's validated instruction bytes, through any alias.
   // Aligned store widths also catch a longword straddling the first/last
   // instruction. The final source length is patched after decoding.
   // Keep the original direct-RAM effective address in r11 until every
   // guard has succeeded. r0 is scratch and is not a cached guest register.
   // r0 = store address - the block's own first byte, without rebuilding the
   // 32-bit source pointer when it is ram_window.base plus a small offset.
   UINT32 src=source_begin&~((UINT32)size-1);
   UINT32 src_off=src-(UINT32)(uintptr_t)ram.base;
   if(ram_base_registered() && src_off<=32767) addi(0,RAM_BASE_REG,(int)src_off);
   else imm(0,src);
   sub(0,12,0);
   CodeWriteCheck &w=write_checks[write_check_count++];
   w.compare=out; w.size=size;
   cmpi(0,0,true); guard(0,false);
  }
  {
  CatGuard accessScope(*this, CAT_MEMACCESS);
  if (ea) {
   // The direct path still has the complete guest alias in r11. Generic
   // translation masks it, and constant operands have not initialized it.
   if(!direct || constant_ram)address(base,index,disp,absolute);
   store(11, SO(ea));
  }
  if (write) emit(d(size == 1 ? 38 : size == 2 ? 44 : 36, data, 12, 0));
  else {
   // Every SH3 word load sign-extends, and lha is exactly that load, so the
   // halfword case needs one instruction instead of lhz plus extsh.
   if (size == 2) emit(d(42, data, 12, 0));
   else emit(d(size == 1 ? 34 : 32, data, 12, 0));
   if (size == 1) emit(x(data, data, 0, 954));
   dirty(data);
  }
  if (pre || (post && value != base)) {
   int b = reg(base); addi(b, b, pre ? -size : size); dirty(b);
  }
  }
  return true;
 }

 bool instruction(UINT16 op) {
  int n = (op >> 8) & 15, m = (op >> 4) & 15, lo = op & 15;
  int a, b, dst;
  unlock();
  // Transfers, with every alignment/MMIO/self-modification guard preceding
  // architectural changes (including predecrement, T and effective address).
  if ((op & 0xf000) == 0x1000) return memory(true,4,m,n,-1,lo*4,0,true,false,false);
  if ((op & 0xf000) == 0x5000) return memory(false,4,n,m,-1,lo*4,0,true,false,false);
  if ((op & 0xf000) == 0x9000) return memory(false,2,n,-1,-1,0,pc+4+(op&255)*2,true,false,false);
  if ((op & 0xf000) == 0xd000) return memory(false,4,n,-1,-1,0,((pc+4)&~3u)+(op&255)*4,true,false,false);
  if ((op & 0xf000) == 0x2000 && (lo <= 2 || (lo >= 4 && lo <= 6))) {
   int size = 1 << (lo & 3); bool pre = (lo & 4) != 0;
   return memory(true,size,m,n,-1,pre?-size:0,0,!pre,pre,false);
  }
  if ((op & 0xf000) == 0x6000 && (lo <= 2 || (lo >= 4 && lo <= 6))) {
   bool post = (lo & 4) != 0;
   return memory(false,1<<(lo&3),n,m,-1,0,0,!post,false,post,lo==2?op:0);
  }
  if ((op & 0xf000) == 0 && ((lo >= 4 && lo <= 6) || (lo >= 12 && lo <= 14))) {
   bool wr = lo < 8;
   return memory(wr,1<<(lo&3),wr?m:n,wr?n:m,0,0,0,true,false,false);
  }
  if ((op & 0xff00) == 0x8000 || (op & 0xff00) == 0x8100 ||
      (op & 0xff00) == 0x8400 || (op & 0xff00) == 0x8500) {
   int size = (op & 0x100) ? 2 : 1;
   return memory(!(op&0x400),size,0,m,-1,lo*size,0,true,false,false);
  }
  if ((op & 0xf000) == 0xc000 && (n <= 2 || (n >= 4 && n <= 6))) {
   int size = 1 << (n & 3);
   return memory(!(n&4),size,0,-2,-1,(op&255)*size,0,true,false,false);
  }
  if ((op & 0xf0ff) == 0x4002 || (op & 0xf0ff) == 0x4012 || (op & 0xf0ff) == 0x4022 ||
      (op & 0xf0ff) == 0x4006 || (op & 0xf0ff) == 0x4016 || (op & 0xf0ff) == 0x4026) {
   int special=(op&0xf0)==0?G_MACH:(op&0xf0)==0x10?G_MACL:G_PR;
   bool wr=(op&15)==2;
   return memory(wr,4,special,n,-1,wr?-4:0,0,true,wr,!wr);
  }
  if ((op & 0xf0ff) == 0x4013 || (op & 0xf0ff) == 0x4017) {
   // STC.L GBR,@-Rn / LDC.L @Rn+,GBR. Reuse the checked memory
   // transfer, including EA and stack updates only after its guards.
   // These control transfers cost 2 / 3 guest cycles respectively.
   bool wr=(op&15)==3;
   if(!memory(wr,4,G_GBR,n,-1,wr?-4:0,0,true,wr,!wr))return false;
   cycles+=wr?1:2;
   return true;
  }
  if ((op & 0xf000) == 0xe000) { dst=reg(n,false); imm(dst,(INT32)(INT8)op); dirty(dst); return true; }
  if ((op & 0xf000) == 0x7000) { dst=reg(n); addi(dst,dst,(INT8)op); dirty(dst); return true; }
  if ((op & 0xff00) == 0xc700) {
   UINT32 ea = ((pc+4)&~3u)+(op&255)*4;
   dst=reg(0,false); imm(dst,ea); store(dst,SO(ea)); dirty(dst); return true;
  }
  if ((op & 0xff00) >= 0xc800 && (op & 0xff00) <= 0xcb00) {
   dst=reg(0);
   switch (n) {
    case 8: emit(d(28,dst,0,op&255)); compare_t(2,true); break;
    case 9: emit(d(28,dst,dst,op&255)); dirty(dst); break;
    case 10: emit(d(26,dst,dst,op&255)); dirty(dst); break;
    case 11: emit(d(24,dst,dst,op&255)); dirty(dst); cycles+=2; break;
   }
   return true;
  }
  if ((op & 0xff00) == 0x8800) { a=reg(0); cmpi(a,(INT8)op); compare_t(2,true); return true; }

  switch (op & 0xf00f) {
   case 0x6003: a=reg(m); dst=reg(n,false); move(dst,a); dirty(dst); return true;
   case 0x6007: a=reg(m); dst=reg(n,false); logic(dst,a,a,124); dirty(dst); return true; // nor
   case 0x600b: a=reg(m); dst=reg(n,false); emit(x(dst,a,0,104)); dirty(dst); return true;
   case 0x600c: case 0x600d: case 0x600e: case 0x600f:
    a=reg(m); dst=reg(n,false);
    if (lo<14) rotate(dst,a,0,lo==12?24:16,31);
    else emit(x(a,dst,0,lo==14?954:922));
    dirty(dst); return true;
   case 0x6008: // SWAP.B preserves the upper half
    a=reg(m); dst=reg(n,false); rotate(0,a,24,24,31);
    move(dst,a); rotate(dst,a,8,16,23,true); rotate(dst,0,0,24,31,true);
    dirty(dst); return true;
   case 0x6009: a=reg(m); dst=reg(n,false); rotate(dst,a,16,0,31); dirty(dst); return true;
   case 0x3004: { // DIV1: use word comparisons, never Xenon's 64-bit CA flag.
    a=reg(n); b=reg(m); int sr=reg(G_SR);
    rotate(12,sr,24,31,31); rotate(0,sr,23,31,31); logic(12,12,0,316);
    cmpi(12,0); // old Q xor M selects addition vs subtraction
    rotate(11,a,1,31,31); // previous sign of Rn
    rotate(0,sr,0,31,31); rotate(a,a,1,0,30); logic(a,a,0,444);
    move(0,a); // shifted value, before the add/sub (also handles n == m)
    UINT32 *do_add=branch(2,false);
    sub(a,a,b); cmp(a,0,true); emit(0x7c000026); rotate(0,0,2,31,31); // borrow
    UINT32 *done=jump(); patch(do_add);
    add(a,a,b); cmp(a,0,true); emit(0x7c000026); rotate(0,0,1,31,31); // carry
    patch(done);
    logic(11,11,0,316); // sign xor carry/borrow
    rotate(0,sr,23,31,31); logic(0,0,11,316); rotate(sr,0,8,23,23,true); // Q
    emit(d(26,11,0,1)); rotate(sr,0,0,31,31,true); // T = Q == M
    dirty(a); dirty(sr); return true;
   }
   case 0x2007: { // DIV0S
    a=reg(n); b=reg(m); int sr=reg(G_SR);
    rotate(11,a,1,31,31); rotate(12,b,1,31,31);
    rotate(sr,11,8,23,23,true); rotate(sr,12,9,22,22,true);
    logic(0,11,12,316); rotate(sr,0,0,31,31,true); dirty(sr); return true;
   }
   case 0x300a: case 0x300e: { // SUBC / ADDC: explicit 32-bit borrow/carry
    if(lo==10 && n==m) {
     // Rn-Rn-T = -T; the outgoing borrow equals the incoming T. Preserve
     // SR and avoid reading the overwritten Rn, even in a dirty slot.
     int sr=reg(G_SR); dst=reg(n,false);
     rotate(0,sr,0,31,31); emit(x(dst,0,0,104)); dirty(dst);
     return true;
    }
    a=reg(n); b=reg(m); int sr=reg(G_SR);
    rotate(11,sr,0,31,31); move(12,a); // incoming T and original Rn
    if(lo==14)add(a,a,b);else sub(a,a,b);
    // cmplw compares only the low word, including when Xenon's add/sub
    // leaves a nonzero upper half. Do not use its 64-bit XER CA flag.
    cmp(a,12,true); emit(0x7c000026);
    rotate(0,0,lo==14?1:2,31,31); // first carry (LT) / borrow (GT)
    move(12,a); // intermediate word, before applying incoming T
    if(lo==14)add(a,a,11);else sub(a,a,11);
    cmp(a,12,true); emit(0x7c000026u|(11u<<21)); // mfcr r11
    rotate(11,11,lo==14?1:2,31,31);
    logic(0,0,11,444); rotate(sr,0,0,31,31,true);
    dirty(a); dirty(sr); return true;
   }
   case 0x300c: case 0x3008:
    a=reg(n); b=reg(m); if (lo==12) add(a,a,b); else sub(a,a,b); dirty(a); return true;
   case 0x2009: case 0x200a: case 0x200b:
    a=reg(n); b=reg(m); logic(a,a,b,lo==9?28:lo==10?316:444); dirty(a); return true;
   case 0x2008:
    a=reg(n); b=reg(m); emit(x(a,0,b,28)|1); compare_t(2,true); return true;
   case 0x3000: case 0x3002: case 0x3003: case 0x3006: case 0x3007:
    a=reg(n); b=reg(m); cmp(a,b,lo==2||lo==6);
    compare_t(lo==0?2:lo==2||lo==3?0:1,lo!=2&&lo!=3); return true;
   case 0x200d:
    a=reg(n); b=reg(m); rotate(0,b,16,0,15); rotate(a,a,16,16,31); logic(a,a,0,444); dirty(a); return true;
   case 0x3005: case 0x300d: { // DMULU.L / DMULS.L, exact 32x32 -> 64
    a=reg(n); b=reg(m);
    int low=reg(G_MACL,false), high=reg(G_MACH,false);
    // Word multiply instructions select the low 32 operand bits on Xenon;
    // no 64-bit carry flag or sign extension of cached GPRs is assumed.
    emit(x(high,a,b,lo==13?75:11)); // mulhw / mulhwu
    emit(x(low,a,b,235)); // mullw
    dirty(high); dirty(low); cycles++; // one extra cycle, plus base cycle
    return true;
   }
   case 0x0007: case 0x200e: case 0x200f:
    a=reg(n); b=reg(m); dst=reg(G_MACL,false);
    if (lo==7) { emit(x(dst,a,b,235)); cycles++; }
    else {
     if (lo==14) { rotate(11,a,0,16,31); rotate(12,b,0,16,31); }
     else { emit(x(a,11,0,922)); emit(x(b,12,0,922)); }
     emit(x(dst,11,12,235));
    }
    dirty(dst); return true;
   case 0x400c: case 0x400d: // SHAD / SHLD
    a=reg(n); b=reg(m); cmpi(b,0); {
     UINT32 *right=branch(0,true);
     rotate(0,b,0,27,31); emit(x(a,a,0,24));
     UINT32 *done=jump(); patch(right);
     // The SH3's negative multiples of 32 mean shift by 32, not by zero.
     logic(0,b,b,124); rotate(0,0,0,27,31);
     imm(12,1); add(0,0,12); // addi cannot use r0 as a source
     emit(x(a,a,0,lo==12?792:536)); patch(done);
    } dirty(a); return true;
  }
  switch (op & 0xf0ff) {
   case 0x0029: a=reg(G_SR); dst=reg(n,false); rotate(dst,a,0,31,31); dirty(dst); return true;
   case 0x000a: case 0x001a: case 0x002a:
    a=reg((op&0xf0)==0?G_MACH:(op&0xf0)==0x10?G_MACL:G_PR); dst=reg(n,false); move(dst,a); dirty(dst); return true;
   case 0x400a: case 0x401a: case 0x402a:
    a=reg(n); dst=reg((op&0xf0)==0?G_MACH:(op&0xf0)==0x10?G_MACL:G_PR,false); move(dst,a); dirty(dst); return true;
   case 0x0002: // STC G_SR,Rn
    a=reg(G_SR); dst=reg(n,false); move(dst,a); dirty(dst); return true;
   case 0x0012: // STC G_GBR,Rn
    a=reg(G_GBR); dst=reg(n,false); move(dst,a); dirty(dst); return true;
   case 0x401e: a=reg(n); dst=reg(G_GBR,false); move(dst,a); dirty(dst); return true;
   case 0x4010: a=reg(n); addi(a,a,-1); dirty(a); cmpi(a,0); compare_t(2,true); return true;
   case 0x4011: case 0x4015:
    a=reg(n); cmpi(a,0); compare_t((op&255)==0x11?0:1,(op&255)==0x15); return true;
   case 0x4008: case 0x4018: case 0x4028: case 0x4009: case 0x4019: case 0x4029:
    a=reg(n); { int count=(op&0xf0)==0?2:(op&0xf0)==0x10?8:16;
     if (lo==8) rotate(a,a,count,0,31-count); else rotate(a,a,32-count,count,31);
    } dirty(a); return true;
   case 0x4024: case 0x4025: { // ROTCL / ROTCR
    a=reg(n); int sr=reg(G_SR);
    if(lo==4) {
     rotate(0,a,1,31,31); rotate(11,sr,0,31,31);
     rotate(a,a,1,0,30); logic(a,a,11,444);
    } else {
     rotate(0,a,0,31,31); rotate(11,sr,31,0,0);
     rotate(a,a,31,1,31); logic(a,a,11,444);
    }
    rotate(sr,0,0,31,31,true); dirty(sr); dirty(a); return true;
   }
   case 0x4000: case 0x4020: case 0x4001: case 0x4021: case 0x4004: case 0x4005:
    a=reg(n); rotate(0,a,(lo==0||lo==4)?1:0,31,31); set_t(-1);
    if ((op&255)==0x21) emit(x(a,a,1,824)); // srawi
    else if (lo==0) rotate(a,a,1,0,30);
    else if (lo==1) rotate(a,a,31,1,31);
    else rotate(a,a,lo==4?1:31,0,31);
    dirty(a); return true;
  }
  switch (op) {
   case 0x0009: return true;
   case 0x0019: a=reg(G_SR); imm(0,~(UINT32)(M|Q|T)); logic(a,a,0,28); dirty(a); return true;
   case 0x0008: set_t(0); return true;
   case 0x0018: set_t(1); return true;
   case 0x0028: a=reg(G_MACH,false); imm(a,0); dirty(a); b=reg(G_MACL,false); imm(b,0); dirty(b); return true;
  }
  return false;
 }

 bool delay_instruction(int op) {
  if (op < 0) return false;
  // PC-relative operands use the branch target while in a delay slot.
  // DT also peeks at the next opcode for the interpreter's idle shortcut.
  // Keep both on the existing path until their distinct semantics are
  // explicitly implemented here. Control/system ops are not in instruction().
  if ((op&0xf000)==0x9000 || (op&0xf000)==0xd000 ||
      (op&0xff00)==0xc700 || (op&0xf0ff)==0x4010) return false;
  pc += 2;
  in_delay_slot = true;
  bool ok = instruction((UINT16)op);
  in_delay_slot = false;
  pc -= 2;
  if (ok) cycles++;
  return ok;
 }

 void finish_delay(bool loop) {
  CatGuard catScope(*this, cat == CAT_EXIT ? CAT_EXIT : CAT_COMPLETE);
  flush();
  if (!loop) {
   // A completed slot leaves PC at the target and PPC at that same value.
   // This is the shared tail's no-pc entry with the block pc as its value,
   // which is what load(0,SO(pc)) produced inline before.
   load(0,SO(pc));
   imm(12,cycles);
   imm(5,1);
   tail_jump(false);
   return;
  }
  charge(cycles);
  // charge() leaves the remaining budget in r11 for the loop-back test.
  cmpi(11,cycles);
  UINT32 *done=branch(0,true);
  emit(0x48000000 | ((UINT32)((char*)start-(char*)out)&0x03fffffc));
  patch(done);
  // A completed slot leaves PC at the target, and PPC at that same value.
  load(0,SO(pc)); store(0,SO(ppc));
  imm(3,1); emit(0x4e800020);
 }

 bool control(UINT16 op, int slot) {
  int n=(op>>8)&15;
  if ((op&0xff00)==0x8900 || (op&0xff00)==0x8b00 ||
      (op&0xff00)==0x8d00 || (op&0xff00)==0x8f00) {
   unlock(); int sr=reg(G_SR); emit(d(28,sr,0,1));
   bool delayed=(op&0x400)!=0, on_t=(op&0x200)==0;
   UINT32 *taken=branch(2,!on_t);
   UINT32 target=pc+4+(INT8)op*2;
   if (!delayed && target!=block_pc) {
    // Continue the not-taken path in this block. Only the taken edge needs
    // a register snapshot and an exit; no dispatcher round trip is needed
    // for the common fallthrough case. Both paths keep their exact costs.
    BlockExit &e=exits[exit_count++];
    e.branches[0]=taken; e.count=1; e.pc=pc+2; e.target=target;
    e.cycles=cycles+3; e.delay_slot=false; e.conditional=true; e.service_opcode=0;
    memcpy(e.slots,slots,sizeof(slots));
    cycles++;
    return true;
   }
   bool loop=target==block_pc && (!delayed || slot==0x0009);
   folded_slot=loop && delayed;
   finish(pc+(folded_slot?4:2),cycles+(folded_slot?2:1),1);
   patch(taken);
   if(loop) {
    // The original opcode bytes were checked on entry. No native store may
    // touch this block's code bytes, and devices/timers run outside it.
    // Stay in generated code for short loops instead of redispatching every
    // few SH3 instructions. A NOP delay slot can be folded without changing
    // the interpreter's PC-relative or interrupt semantics.
    int cost=cycles+3;
    CatGuard catScope(*this, CAT_COMPLETE);
    flush(); constant(SO(ea),target); charge(cost);
    // charge leaves the remaining budget in r11.
    cmpi(11,cost);
    UINT32 *done=branch(0,true);
    emit(0x48000000 | ((UINT32)((char*)start-(char*)out)&0x03fffffc));
    patch(done);
    constant(SO(pc),target);
    constant(SO(ppc),delayed?target:pc+2);
    imm(3,1); emit(0x4e800020);
    cycles=cost;
   } else {
    constant(SO(pc),target); constant(SO(ea),target);
    cycles+=delayed?2:3;
    folded_slot=delayed && delay_instruction(slot);
    if (folded_slot) finish_delay(target==block_pc);
    else {
     if (delayed) constant(SO(delay),pc+2);
     finish(pc+2,cycles,1,false);
    }
   }
   ended=true; return true;
  }
  bool relative=(op&0xf000)==0xa000||(op&0xf000)==0xb000;
  bool regrel=(op&0xf0ff)==0x0023||(op&0xf0ff)==0x0003;
  bool regabs=(op&0xf0ff)==0x402b||(op&0xf0ff)==0x400b;
  if (relative || regrel || regabs || op==0x000b) {
   // Preserve the interpreter's BRA/NOP idle-loop cycle shortcut.
   if ((op&0xffff)==0xaffe) return false;
   unlock();
   bool call=(op&0xf000)==0xb000||(op&0xf0ff)==0x0003||(op&0xf0ff)==0x400b;
   if (relative) { INT32 disp=(INT32)(op&0xfff); if(disp&0x800)disp-=0x1000; imm(12,pc+4+disp*2); }
   else if (op==0x000b) { int pr=reg(G_PR); move(12,pr); }
   else { int a=reg(n); if(regrel) { imm(12,pc+4); add(12,12,a); } else move(12,a); }
   store(12,SO(pc)); if(!regrel)store(12,SO(ea));
   if(call) { int pr=reg(G_PR,false); imm(pr,pc+4); dirty(pr); }
   cycles+=((op&0xf0ff)==0x402b)?1:2;
   folded_slot=delay_instruction(slot);
   if (folded_slot) finish_delay(false);
   else { constant(SO(delay),pc+2); finish(pc+2,cycles,1,false); }
   ended=true; return true;
  }
  return false;
 }
};

static bool allocate()
{
 if (failed) return false;
 if (blocks) return true;
 blocks=(Block*)calloc(TABLE_SIZE,sizeof(Block));
 lookup=(Lookup*)calloc(CACHE_SETS,sizeof(Lookup));
#ifdef _XBOX
 code=xbox_code;
#else
 code=(UINT32*)mmap(NULL,CACHE_BYTES,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(code==MAP_FAILED)code=NULL;
#endif
 if(!blocks || !lookup || !code) {
  free(blocks); blocks=NULL; free(lookup); lookup=NULL;
#ifndef _XBOX
  if(code)munmap(code,CACHE_BYTES);
#endif
  code=NULL; failed=true; return false;
 }
 used=0;
 sh3_ppc_state.read_map=MemMapR;
 sh3_ppc_state.write_map=MemMapW;
 return true;
}

static void compile(Block &block, UINT32 pc, const UINT16 *source)
{
 if(used+MAX_WORDS >= CACHE_BYTES/4) {
  ++sh3_drc_work.arena_recycles;
  memset(blocks,0,TABLE_SIZE*sizeof(Block));
  memset(lookup,0,CACHE_SETS*sizeof(Lookup)); used=0;
 }
 Compiler c(code+used,pc,source);
 int available=(int)((4096-((uintptr_t)source&4095))/2);
 int guest_available=(int)((SH3_PAGE_SIZE-(pc&SH3_PAGEM))/2);
 if(available>guest_available)available=guest_available;
 if(available>MAX_INSNS)available=MAX_INSNS;
 int count=0, validate=0; bool check_read=false;
 for(;count<available;count++) {
  if(c.exit_count>MAX_INSNS*3-3 || c.out-c.start+c.exit_count*32>MAX_WORDS-256)break; // reserve the largest instruction and exit
  UINT16 op=source[count];
  if((op&0xf0ff)==0x4010) {
   // DT reads the following opcode for the existing busy-loop hack. Do not
   // fold a handler read, or translate the hack itself. Include lookahead in
   // validation even when DT ends the block.
   if(count+1>=available || MemMapR[(pc&AM)>>SH3_SHIFT]!=MemMapF[(pc&AM)>>SH3_SHIFT] || source[count+1]==0x8bfd)break;
   validate=count+2; check_read=true;
  }
  c.pc=pc+count*2;
  if(c.control(op,count+1<available ? source[count+1] : -1)) {
   if(c.ended) { count+=c.folded_slot?2:1; break; }
   continue;
  }
  if(!c.instruction(op))break;
  c.cycles++;
 }
 // A taken conditional near the end can cost more than the fallthrough.
 // Reserve enough budget for every native path before entering the block.
 int max_cycles=c.cycles;
 for(int i=0;i<c.exit_count;i++)
  if(c.exits[i].cycles>max_cycles)max_cycles=c.exits[i].cycles;
 if(!c.ended)c.finish(pc+count*2,c.cycles,1);
 c.emit_exits();
 if(count) {
  ++sh3_gen_blocks;
  for(int i=0;i<CAT_COUNT;i++)sh3_gen_words[i]+=c.cat_words[i];
 }
 block.pc=pc; block.source=source;
 block.words=(UINT16)(validate>count?validate:count);
 // Keep unsupported first opcodes in the lookup table without generating
 // useless native calls. Validate the opcode so later code changes work.
 if(!block.words)block.words=1;
 c.protect_code(block.words);
 memcpy(block.original,source,block.words*2);
 block.cycles=(UINT16)max_cycles; block.check_read_map=check_read;
 block.entry=count?(int (*)(Sh3PpcState*))(code+used):NULL;
 if(count) {
  sync_code(code+used,c.out);
  used=(unsigned)(c.out-code+3)&~3u; // 16-byte block alignment
  if(used>sh3_drc_work.arena_peak)sh3_drc_work.arena_peak=used;
 }
}
#undef SO
} // namespace Sh3Ppc

static bool sh3_drc_enabled=true;
void Sh3SetDrc(INT32 enabled) { sh3_drc_enabled=enabled!=0; }
static void sh3_drc_invalidate_ram()
{
 if(Sh3Ppc::ram_window.base) {
  Sh3Ppc::ram_window.base=NULL;
  sh3_ppc_state.ram_base=NULL;
  sh3_drc_reset();
 }
}
static void sh3_drc_mapping_changed(UINT32 start, UINT32 end, INT32 type)
{
 const Sh3Ppc::RamWindow &ram=Sh3Ppc::ram_window;
 if(ram.base && (type&(MAP_READ|MAP_WRITE)) &&
    start<ram.start+ram.span && end>=ram.start)sh3_drc_invalidate_ram();
}
void Sh3SetDrcIdleWatch(const UINT32* idle_ram, const UINT32* idle_pc)
{
 sh3_idle_watch_ram=idle_ram; sh3_idle_watch_pc=idle_pc;
}
void Sh3SetDrcDeviceRead(INT32 slot, UINT32 address, INT32 handler)
{
 if(slot<0 || slot>=2)return;
 Sh3DrcDeviceRead &read=sh3_device_reads[slot];
 read.address=0; read.handler=0; read.callback=NULL;
 if(handler>=0 && handler<SH3_MAXHANDLER && address<=AM && !(address&3)) {
  read.address=address; read.handler=handler; read.callback=ReadLong[handler];
 }
 // Tagged exits depend on registration at compile time, not just execution.
 sh3_drc_reset();
}
void Sh3SetDrcReadMirror(UINT8* ram, UINT32 page, UINT32 watched, INT32 handler)
{
 Sh3SetDrcIdleWatch(NULL,NULL);
 sh3_drc_invalidate_ram();
 sh3_ppc_state.read_mirror=ram;
 sh3_ppc_state.mirror_page=page;
 sh3_ppc_state.mirror_watch=watched;
 sh3_ppc_state.mirror_handler=handler;
}
INT32 Sh3SetDrcRam(UINT8* ram, UINT32 start, UINT32 span, UINT32 backing_size)
{
 using namespace Sh3Ppc;
 sh3_drc_invalidate_ram();
 if(!ram)return 0;
 if(((uintptr_t)ram&3) || span<SH3_PAGE_SIZE || span>0x1000000 || (span&(span-1)) ||
    backing_size<SH3_PAGE_SIZE || backing_size>span || (backing_size&(backing_size-1)) ||
    (start&(span-1)) || start>0x20000000u-span)return 1;
 UINT32 watch=0xffffffffu;
 for(UINT32 off=0;off<span;off+=SH3_PAGE_SIZE) {
  UINT32 page=(start+off)>>SH3_SHIFT;
  UINT8 *expected=ram+(off&(backing_size-1));
  if(MemMapW[page]!=expected)return 1;
  if(MemMapR[page]!=expected) {
   if(MemMapR[page]!=(UINT8*)(uintptr_t)sh3_ppc_state.mirror_handler ||
      sh3_ppc_state.read_mirror!=expected || sh3_ppc_state.mirror_page!=start+off)return 1;
   if((sh3_ppc_state.mirror_watch&~SH3_PAGEM)==start+off)
    watch=sh3_ppc_state.mirror_watch;
  }
 }
 ram_window.base=ram; ram_window.start=start; ram_window.span=span;
 sh3_ppc_state.ram_base=ram;
 ram_window.mask=backing_size-1; ram_window.watch=watch;
 ram_window.span_bits=ram_window.backing_bits=0;
 while((1u<<ram_window.span_bits)<span)ram_window.span_bits++;
 while((1u<<ram_window.backing_bits)<backing_size)ram_window.backing_bits++;
 sh3_drc_reset();
 return 0;
}
static void sh3_drc_reset()
{
 if(Sh3Ppc::blocks)memset(Sh3Ppc::blocks,0,Sh3Ppc::TABLE_SIZE*sizeof(Sh3Ppc::Block));
 if(Sh3Ppc::lookup)memset(Sh3Ppc::lookup,0,Sh3Ppc::CACHE_SETS*sizeof(Sh3Ppc::Lookup));
 Sh3Ppc::used=0;
}
static void sh3_drc_exit()
{
 free(Sh3Ppc::blocks); Sh3Ppc::blocks=NULL;
 free(Sh3Ppc::lookup); Sh3Ppc::lookup=NULL;
#ifndef _XBOX
 if(Sh3Ppc::code)munmap(Sh3Ppc::code,Sh3Ppc::CACHE_BYTES);
#endif
 Sh3Ppc::code=NULL; Sh3Ppc::used=0; Sh3Ppc::failed=false;
 sh3_drc_enabled=true;
 memset(sh3_device_reads,0,sizeof(sh3_device_reads));
 Sh3SetDrcReadMirror(NULL,0,0,0);
}
#include "sh3_drc_dispatch.h"

// Preserve the one-block entry for the existing instruction differential tests.
static bool sh3_drc_run() { return sh3_drc_dispatch<false>(); }
#endif
