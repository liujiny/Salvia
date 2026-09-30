// Execute emitted PPC instructions and compare them with the SH3 interpreter.
// No ROMs are needed. Run with run.py under a 32-bit big-endian PowerPC ABI.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../src/cpu/sh4/sh4.cpp"
UINT8 serflash_io_read(){return 0;}
static INT32 quiet(INT32,TCHAR*,...){return 0;}
INT32 (__cdecl *bprintf)(INT32,TCHAR*,...)=quiet;
static INT32 scan_stub(BurnArea*) {return 0;}
INT32 (__cdecl *BurnAcb)(BurnArea*)=scan_stub;
void CpuCheatRegister(INT32,cpu_core_config*){}
static unsigned seed=0x62123456, calls;
static UINT32 rnd(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static UINT8 read8(UINT32 a){calls++;return a^0xa5;}
static UINT16 read16(UINT32 a){calls++;return a^0x5aa5;}
static UINT32 read32(UINT32 a){calls++;return a^0x84212345;}
static void write8(UINT32,UINT8){calls++;}
static void write16(UINT32,UINT16){calls++;}
static void write32(UINT32,UINT32){calls++;}
static UINT8 *ram,*saved,*actual;
static void step() {
 UINT16 op;
 if(m_delay){op=sh3_cpu_readop16(m_delay&AM);m_delay=0;m_ppc=m_pc;}
 else {op=sh3_cpu_readop16(m_pc&AM);m_pc+=2;m_ppc=m_pc;}
 execute_one(op);EAT(1);
}
static UINT8 mirror8(UINT32 a){return ram[a&65535];}
static UINT16 mirror16(UINT32 a){return *(UINT16*)(ram+(a&65534));}
static UINT32 mirror32(UINT32 a){if(a==0x0c004000){calls++;EAT(5);}return *(UINT32*)(ram+(a&65532));}
static unsigned cases,compiled,fallback;
static void check(bool verbose=false) {
 Sh3PpcState before=sh3_ppc_state;
 memcpy(saved,ram,0x10000);
 calls=0;
 bool ran=sh3_drc_run();
 if(ran)compiled++;else fallback++;
 // The public dispatch contract: a guarded partial block requests exactly
 // one interpreted instruction at its updated PC.
 if(!ran)step();
 Sh3PpcState got=sh3_ppc_state;
 unsigned actual_calls=calls;
 memcpy(actual,ram,0x10000);
 sh3_ppc_state=before;memcpy(ram,saved,0x10000);calls=0;
 unsigned watchdog=0;
 while(m_sh4_icount>got.icount && watchdog++<1000)step();
 bool eq=memcmp(&sh3_ppc_state,&got,sizeof(got))==0 && memcmp(ram,actual,0x10000)==0 && calls==actual_calls;
 if(!eq) {
  fprintf(stderr,"FAIL case %u op %04x start %08x consumed %d ran %d calls %u/%u\n",cases,*(UINT16*)(saved+(before.pc&65535)),before.pc,before.icount-got.icount,ran,calls,actual_calls);
  UINT32 *a=(UINT32*)&sh3_ppc_state,*b=(UINT32*)&got,*init=(UINT32*)&before;
  for(unsigned i=0;i<sizeof(got)/4;i++)if(a[i]!=b[i])fprintf(stderr,"word %u before %08x expected %08x got %08x\n",i,init[i],a[i],b[i]);
  for(unsigned i=0;i<0x10000;i++)if(ram[i]!=actual[i]){fprintf(stderr,"RAM %04x expected %02x got %02x\n",i,ram[i],actual[i]);break;}
  FILE *f=fopen("failed-code.bin","wb");fwrite(Sh3Ppc::code,4,Sh3Ppc::used,f);fclose(f);exit(1);
 }
 cases++;
}
static void state(int mode) {
 for(int i=0;i<16;i++)m_r[i]=rnd();
 m_pc=0x0c000100;m_ppc=rnd();m_sr=rnd();m_pr=rnd();m_gbr=0x0c005000;m_mach=rnd();m_macl=rnd();m_ea=rnd();m_delay=0;
 m_sh4_icount=500;sh3_total_cycles=rnd()&0x3fffffff;
 if(mode)for(int i=0;i<16;i++)m_r[i]=(mode==1?0x0c004200:mode==2?0x8c004200:mode==3?0x0c000200:0x10000400)+(rnd()&0xfc);
}
int main() {
 setbuf(stdout,NULL);
 ram=(UINT8*)calloc(1,0x10000);saved=(UINT8*)calloc(1,0x10000);actual=(UINT8*)calloc(1,0x10000);
 Sh3Init(0,102400000,0,0,0,0,0,1,0,1,0);
 // Random addresses must not write real timer/DMA/internal registers: their
 // device state is outside this instruction-level snapshot. Count callbacks
 // throughout the synthetic address space; the game test covers real devices.
 Sh3MapHandler(0,0,0xffffffffu,MAP_READ|MAP_WRITE);
 Sh3MapMemory(ram,0x0c000000,0x0c00ffff,MAP_RAM);
 Sh3MapMemory(ram,0,0xffff,MAP_ROM);
 Sh3SetReadByteHandler(0,read8);Sh3SetReadWordHandler(0,read16);Sh3SetReadLongHandler(0,read32);
 Sh3SetWriteByteHandler(0,write8);Sh3SetWriteWordHandler(0,write16);Sh3SetWriteLongHandler(0,write32);
 Sh3Reset();Sh3Ppc::allocate();
 for(int mode=0;mode<5;mode++) {
  for(unsigned op=0;op<65536;op++) {
   // Only invoke supported opcodes; an arbitrary unsupported instruction
   // may switch banks, trap, read unmapped internal memory or touch the FPU.
   state(mode);UINT16 *p=(UINT16*)(ram+0x100);p[0]=op;p[1]=0xffff;
   Sh3Ppc::Block probe;Sh3Ppc::compile(probe,m_pc,p);
   if(!probe.entry)continue;
   check();
  }
  printf("MODE %d cases %u compiled %u fallback %u\n",mode,cases,compiled,fallback);
 }
 const UINT16 ops[]={0xe010,0x7001,0x71ff,0x6013,0x6233,0x6323,0x301c,0x3218,0x2129,0x212a,0x212b,0x6018,0x6219,0x3210,0x3212,0x3213,0x3216,0x3217,0x4010,0x4211,0x4215,0x4200,0x4201,0x4208,0x4229,0x0207,0x212f,0x431c,0x431d,0x6432,0x2432,0x7434,0x2436,0x6346,0x0346,0x600c,0x642e,0x0009,0x0018,0x0008,0x6833,0x69a3,0x6bc3,0x6dc3,0x6ed3,0x3e8c,0x8bf0,0x8df0,0xaffe,0xa010,0x402b,0x000b};
 for(int i=0;i<40000;i++) {
  state(i%5);
  if(i%7==0){m_r[0]=0;m_r[1]=0;m_r[2]=0xffffffff;m_r[3]=0x80000000;}
  for(int j=0;j<128;j++)((UINT16*)(ram+0x100))[j]=0xffff;
  int len=1+rnd()%32;
  for(int j=0;j<len;j++)((UINT16*)(ram+0x100))[j]=ops[rnd()%(sizeof(ops)/2)];
  m_sh4_icount=1+rnd()%100;
  check();
 }



 const UINT32 limits[]={0,1,0x7fffffff,0x80000000,0xffffffff,0xffff,0x10000,0x80000001};
 for(int i=0;i<24000;i++){
  state(0);int n=rnd()%16,m=i%2?n:rnd()%16;
  m_r[n]=i%3?rnd():limits[rnd()%8];m_r[m]=i%3?rnd():limits[rnd()%8];
  m_sr=(rnd()&~0x301)|((i%8&3)<<8)|((i%8)>>2);
  UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x3004|(n<<8)|(m<<4);p[1]=0x4000|(n<<8)|(i%2?0x24:0x25);p[2]=0xffff;
  check();
 }
 for(int i=0;i<10000;i++) {
  state(1);m_r[1]=0x0c004000;m_r[2]=0x0c005000;m_r[3]=1+rnd()%70;
  UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=0x6014;p[1]=0x2200;p[2]=0x7201;p[3]=0x4310;p[4]=0x8ffa;p[5]=0x0009;p[6]=0xffff;
  m_sh4_icount=1+rnd()%400;
  check();
 }

 // Every supported ordinary opcode in a taken branch's delay slot. These
 // exercise PR dependencies, overwritten jump registers, T changes and
 // guards after the branch has committed (including MMIO and code writes).
 const UINT16 branches[]={0xa005,0xb005,0x400b,0x402b,0x0323,0x0303,0x000b,0x8d05,0x8f05};
 for(unsigned op=0;op<65536;op++) {
  state(op%5);UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=branches[op%(sizeof(branches)/2)];p[1]=op;p[2]=0xffff;
  if((p[0]&0xff00)==0x8d00)m_sr|=T;
  if((p[0]&0xff00)==0x8f00)m_sr&=~T;
  Sh3Ppc::Block probe;Sh3Ppc::compile(probe,m_pc,p);
  if(probe.words==2)check();
 }
 for(int i=0;i<20000;i++) {
  state(i%5);UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=0x7001;p[1]=branches[rnd()%(sizeof(branches)/2)];
  p[2]=ops[rnd()%(sizeof(ops)/2)];p[3]=0xffff;
  m_sh4_icount=1+rnd()%12;
  check();
 }
 // BF/S copy loops with a non-NOP slot stay native, but must leave exactly
 // the same registers, PC, pending delay slot and cycles at a short budget.
 for(int i=0;i<10000;i++) {
  state(1);m_r[1]=0x0c004000;m_r[2]=0x0c005000;m_r[3]=1+rnd()%70;
  UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=0x6014;p[1]=0x2200;p[2]=0x4310;p[3]=0x8ffb;p[4]=0x7201;p[5]=0xffff;
  m_sh4_icount=1+rnd()%300;
  check();
 }
 printf("EDGE compiled delay slots / branch dependencies / guarded slots PASS\n");
 // A conditional side exit and the following memory guard have adjacent
 // PCs but different snapshots and destinations. They must never share an
 // exit, for either branch polarity, taken state or guard failure reason.
 for(int mode=0;mode<5;mode++)for(int t=0;t<2;t++)for(int bf=0;bf<2;bf++) {
  state(mode);m_sr=(m_sr&~T)|t;
  UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=0x0009;p[1]=bf?0x8b04:0x8904;p[2]=0x2132;p[3]=0x6212;p[4]=0x7001;p[5]=0xffff;
  check();
 }
 // The last instruction's taken cost can exceed the fallthrough cost.
 // Test budgets on both sides of that boundary, including a full block.
 for(int len=1;len<=32;len++)for(int budget=1;budget<=36;budget++) {
  state(1);UINT16 *p=(UINT16*)(ram+0x100);
  for(int i=0;i<len-1;i++)p[i]=0x7001;
  p[len-1]=0x8905;p[len]=0xffff;m_sr=(m_sr&~T)|(budget&1);
  m_sh4_icount=budget;check();
 }
 printf("EDGE conditional fallthrough / adjacent guards / cycle budgets PASS\n");
 // Same-block code writes through both physical and cached RAM aliases.
 for(int alias=0;alias<2;alias++)for(int width=0;width<3;width++){
  state(1);UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=0xe205;p[1]=0x7001;p[2]=0x2130+width;p[3]=0xe409;p[4]=0xffff;
  m_r[1]=(alias?0x8c000106:0x0c000106);m_r[3]=0xe477e509;
  if(width==2)m_r[1]+=2;
  check();check();
 }
 // Store widths straddling the start/end of code, including a block that
 // starts halfway through a longword. Nearby data outside the source span
 // can stay native; an overlapping store must exit before modifying code.
 for(int align=0;align<2;align++)for(int alias=0;alias<2;alias++)
 for(int width=0;width<3;width++)for(int off=-8;off<=16;off++) {
  state(1);m_pc+=align*2;UINT16 *p=(UINT16*)(ram+(m_pc&65535));
  p[0]=0x2130+width;p[1]=0xe477;p[2]=0x7001;p[3]=0xffff;
  m_r[1]=((m_pc+off)&~((1u<<width)-1))|(alias?0x80000000:0);
  m_r[3]=0x12345678;check();
 }
 state(1);{
  UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x2132;p[1]=0x7001;p[2]=0xffff;
  m_r[1]=0x8c000800;m_r[3]=0x12345678;
  unsigned count=compiled;check();if(compiled!=count+1)return 10;
 }
 // Protect the DT lookahead even though it is not a compiled instruction.
 state(1);{
  UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x2131;p[1]=0x7001;p[2]=0x4410;p[3]=0xffff;
  m_r[1]=0x8c000106;m_r[3]=0x8bfd;m_r[4]=3;check();check();
 }
 printf("EDGE precise code-write ranges / aliases / DT lookahead PASS\n");

 // Repeat every translated opcode with the verified RAM window enabled.
 // Random, ordinary, cached-alias, code and MMIO addresses still use the
 // original interpreter as the oracle, including guarded partial blocks.
 if(Sh3SetDrcRam(ram,0x0c000000,0x10000,0x10000))return 11;
 for(int mode=0;mode<5;mode++) {
  for(unsigned op=0;op<65536;op++) {
   state(mode);UINT16 *p=(UINT16*)(ram+0x100);p[0]=op;p[1]=0xffff;
   Sh3Ppc::Block probe;Sh3Ppc::compile(probe,m_pc,p);
   if(probe.entry)check();
  }
 }
 // External aliases use the same RAM; the internal high-address area must
 // not pass the RAM tag check. Signed byte/word loads are included.
 const UINT32 aliases[]={0x0c000000,0x2c000000,0x4c000000,0x6c000000,
                          0x8c000000,0xac000000,0xcc000000,0xec000000};
 for(int a=0;a<8;a++)for(int width=0;width<3;width++)for(int wr=0;wr<2;wr++) {
  state(1);UINT16 *p=(UINT16*)(ram+0x100);p[0]=(wr?0x2130:0x6210)+width;p[1]=0xffff;
  m_r[1]=aliases[a]+0x5000;check();
 }
 // Cached PC-relative loads read literal data on every execution.
 for(int i=0;i<10;i++) {
  state(1);UINT16 *p=(UINT16*)(ram+0x100);p[0]=0xd27f;p[1]=0xffff;
  *(UINT32*)(ram+0x300)=rnd();check();
 }
 // Registration validates every read/write page. A mapping change must
 // revoke embedded host pointers before the next compiled block is used.
 UINT8 *other_ram=(UINT8*)calloc(1,0x10000);
 *(UINT32*)(other_ram+0x5000)=0xaabbccdd;
 Sh3MapMemory(other_ram,0x0c000000,0x0c00ffff,MAP_READ);
 if(Sh3Ppc::ram_window.base || Sh3Ppc::used)return 12;
 if(!Sh3SetDrcRam(ram,0x0c000000,0x10000,0x10000))return 13;
 state(1);{UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x6212;p[1]=0xffff;m_r[1]=0x0c005000;check();}
 Sh3MapMemory(ram,0x0c000000,0x0c00ffff,MAP_READ);free(other_ram);
 if(Sh3SetDrcRam(ram,0x0c000000,0x10000,0x10000))return 14;
 Sh3MapHandler(0,0x0c000000,0x0c00ffff,MAP_WRITE);
 if(Sh3Ppc::ram_window.base || Sh3Ppc::used)return 15;
 state(1);{UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x2132;p[1]=0xffff;m_r[1]=0x0c005000;check();}
 Sh3MapMemory(ram,0x0c000000,0x0c00ffff,MAP_WRITE);
 if(!Sh3SetDrcRam(ram,0x0c000001,0x10000,0x10000) ||
    !Sh3SetDrcRam(ram,0x0c000000,0x30000,0x10000) ||
    !Sh3SetDrcRam(ram,0x0c000000,0x10000,0x20000))return 16;
 printf("EDGE direct RAM / literal reads / aliases / mapping invalidation PASS\n");

 Sh3MapHandler(1,0x0c000000,0x0c00ffff,MAP_READ);
 Sh3SetReadByteHandler(1,mirror8);Sh3SetReadWordHandler(1,mirror16);Sh3SetReadLongHandler(1,mirror32);
 Sh3SetDrcReadMirror(ram,0x0c000000,0x0c004000,1);
 for(int i=0;i<10000;i++){
  state(1);UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x7001;p[1]=0x6210+(i%3);p[2]=0xe655;p[3]=0xffff;
  m_r[1]=(i%2?0x8c004000:0x0c004000)+(i%4==0?0:4);m_sh4_icount=4;
  check();
 }
 Sh3SetReadByteHandler(1,mirror8);if(sh3_ppc_state.read_mirror)return 8;

 // The smaller CV1000 board mirrors its backing RAM within a larger
 // physical span. Only the real watched longword uses the idle handler;
 // the same offset in the second mirror remains an ordinary RAM read.
 Sh3SetDrcReadMirror(ram,0x0c000000,0x0c004000,1);
 Sh3MapMemory(ram,0x0c010000,0x0c01ffff,MAP_RAM);
 if(Sh3SetDrcRam(ram,0x0c000000,0x20000,0x10000))return 17;
 for(int i=0;i<10000;i++) {
  state(1);UINT16 *p=(UINT16*)(ram+0x100);
  p[0]=0x7001;p[1]=0x6210+(i%3);p[2]=0xe655;p[3]=0xffff;
  m_r[1]=(i&1?0x8c000000:0x0c000000)+(i&2?0x10000:0)+(i&4?0x4000:0x5000);
  m_sh4_icount=4;check();
 }
 Sh3SetReadLongHandler(1,read32);
 if(Sh3Ppc::ram_window.base || sh3_ppc_state.read_mirror || Sh3Ppc::used)return 18;
 state(1);{UINT16 *p=(UINT16*)(ram+0x100);p[0]=0x6212;p[1]=0xffff;m_r[1]=0x0c005000;check();}
 Sh3SetReadLongHandler(1,mirror32);
 printf("EDGE mirrored direct RAM / watched reads / handler invalidation PASS\n");
 Sh3MapMemory(ram,0x0c000000,0x0c00ffff,MAP_READ);
 Sh3SetDrcReadMirror(NULL,0,0,0);
 if(Sh3SetDrcRam(ram,0x0c000000,0x10000,0x10000))return 19;
 // Source mutation from DMA/direct RAM writes and remapped instruction pages.
 state(1);((UINT16*)(ram+0x100))[0]=0xe155;((UINT16*)(ram+0x100))[1]=0xffff;check();
 state(1);((UINT16*)(ram+0x100))[0]=0xe177;check();
 UINT8 *alternate=(UINT8*)calloc(1,0x10000);memcpy(alternate,ram,0x10000);
 ((UINT16*)(alternate+0x100))[0]=0xe199;
 Sh3MapMemory(alternate,0x0c000000,0x0c00ffff,MAP_FETCHOP);
 state(1);check();Sh3MapMemory(ram,0x0c000000,0x0c00ffff,MAP_FETCHOP);free(alternate);
 // Guest page and unaligned host-page endings must not read a second page.
 for(int i=0;i<16;i++) {
  state(1);m_pc=0x0c00fffe - i*2;
  for(unsigned off=m_pc&65535;off<65536;off+=2)*(UINT16*)(ram+off)=0x7001;
  check();
 }
 // Lifecycle clears lookup entries, but keeps CPU registers and mapped RAM.
 sh3_drc_reset();if(Sh3Ppc::used) return 6;
 state(1);((UINT16*)(ram+0x100))[0]=0xe333;((UINT16*)(ram+0x100))[1]=0xffff;check();
 Sh3Scan(ACB_WRITE|ACB_DRIVER_DATA);if(Sh3Ppc::used)return 7;
 state(1);check();
 // Tiny time slices, pending interrupts and delayed slots take the original
 // dispatch path; the full game test also exercises these in normal runs.
 printf("EDGE lifecycle / self-modification / remap / page boundary PASS\n");
 printf("PASS %u cases compiled %u fallback %u\n",cases,compiled,fallback);
 Sh3Exit();return 0;
}
