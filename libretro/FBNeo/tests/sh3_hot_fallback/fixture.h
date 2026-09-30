// State/callback fixture for the actual 16 production instruction helpers.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UINT8; typedef int8_t INT8;
typedef uint16_t UINT16; typedef int16_t INT16;
typedef uint32_t UINT32; typedef int32_t INT32;
struct TestState {
 UINT32 r[16], ea, sr, pc, reads, trace, other;
 INT32 cycles;
};
static TestState state;
static unsigned generic_calls;
#define m_r state.r
#define m_ea state.ea
#define m_sr state.sr
#define Rm ((opcode >> 4) & 15)
#define Rn ((opcode >> 8) & 15)
#define T 1u
static UINT32 read_memory(UINT32 address,unsigned width) {
 ++state.reads;
 state.trace=(state.trace*131u)^address^(width<<24)^state.pc;
 // Model the existing watched-longword idle handler, not a new native shortcut.
 if(width==4 && (address&0x1fffffffu)==0x0c000040u &&
    (state.pc==0x0c001000u || state.pc==0x0c001002u)) state.cycles-=1024;
 return ((address*0x9e3779b9u)^0x8000ff81u) + state.reads;
}
#define RB(a) ((UINT8)read_memory((a),1))
#define RW(a) ((UINT16)read_memory((a),2))
#define RL(a) read_memory((a),4)
static void other_handler(unsigned id,UINT16 opcode) {
 state.other=id; state.trace=state.trace*131u+opcode;
}
static UINT32 random_state=0x92ba9813u;
static UINT32 random32() {random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static TestState initial_state(unsigned variant,unsigned opcode) {
 TestState s; memset(&s,0,sizeof(s));
 static const UINT32 pool[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,
  0x0c000040u,0x8c000040u,0xac000040u,0xe0000040u,0xf0000000u,0x0c000041u,0x0c000043u};
 for(unsigned i=0;i<16;++i)s.r[i]=variant<6?pool[(opcode+variant+i)%12]:random32();
 // Exercise watched reads at every source register, including n == m == 0.
 if(variant==0 || variant==1)s.r[(opcode>>4)&15]=variant?0xac000040u:0x0c000040u;
 s.sr=variant&1?0xffffffffu:0;s.ea=0xfeed1234u;
 s.pc=0x0c001000u+(variant%4)*2;s.cycles=(int)variant-2;
 return s;
}
