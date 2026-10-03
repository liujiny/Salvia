#!/usr/bin/env python3
"""Check idle-pair cycle accounting and every memory/IRQ guard without ROMs."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'src/cpu/v60/op12.c').read_text()
start=s.index('static void v60SkipReadOnlyWaitLoop()')
function=s[start:s.index('\n}\n',start)+3]
preamble=r"""
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32; typedef int16_t INT16;
#define page_size 0x800
#define page_mask 0x7ff
#define CLEAR_LINE 0
static UINT8 *pages[3][0x2000], **mem[3]={pages[0],pages[1],pages[2]};
static UINT32 address_mask=0xffffff, idle_loop_start=0x200000, idle_loop_end=0x20ffff;
static int v60_ICount;
static struct {UINT32 reg[32],pc;int end_run,irq_line;} v60;
#define PC v60.pc
"""
main=r"""
static UINT8 code[page_size], ram[page_size];
static void reset() {
 memset(pages,0,sizeof(pages));memset(code,0,sizeof(code));memset(&v60,0,sizeof(v60));
 const UINT8 ins[]={0x87,0x80,0xe0,0x39,0x08,0x2c,0x65,0xfa};memcpy(code+0x505,ins,8);
 PC=0x133505;v60.reg[25]=0x200000;address_mask=0xffffff;
 idle_loop_start=0x200000;idle_loop_end=0x20ffff;v60_ICount=1024;
 pages[0][PC/page_size]=pages[2][PC/page_size]=code;
 pages[0][0x202c08/page_size]=ram;
}
struct Result {int cycles;UINT32 pc,ppc;};
static Result finish(int cycles) {
 Result r={cycles,0x13350b,0x133505};
 while(r.cycles>0) {r.ppc=r.pc;r.pc=r.pc==0x13350b?0x133505:0x13350b;r.cycles-=8;}
 return r;
}
static void rejected() {int before=v60_ICount;v60SkipReadOnlyWaitLoop();assert(v60_ICount==before);}
int main() {
 for(int cycles=-8;cycles<4096;cycles++) {
  reset();v60_ICount=cycles;Result a=finish(cycles);v60SkipReadOnlyWaitLoop();Result b=finish(v60_ICount);
  assert(a.cycles==b.cycles&&a.pc==b.pc&&a.ppc==b.ppc);
  assert(v60_ICount==(cycles>=16?(cycles&15):cycles));
 }
 reset();v60.irq_line=1;rejected();reset();v60.end_run=1;rejected();
 reset();address_mask=0xfffffe;rejected();
 reset();pages[0][PC/page_size]=0;rejected();reset();pages[2][PC/page_size]=ram;rejected();
 reset();PC=(PC&~page_mask)+page_mask-6;rejected();
 for(unsigned i=0;i<8;i++)if(i!=4&&i!=5){reset();code[0x505+i]^=0x80;rejected();}
 reset();v60.reg[25]=0;rejected();reset();idle_loop_end=0x202c0a;rejected();
 reset();idle_loop_start=0x202c09;rejected();
 reset();pages[0][0x202c08/page_size]=0;rejected();
 reset();v60.reg[25]=0x2003f7;rejected(); // data begins at page offset 0x7ff
 for(unsigned bit=0;bit<16;bit++){reset();code[0x507]=0xe0|bit;v60SkipReadOnlyWaitLoop();assert(v60_ICount==0);}
 puts("PASS V60 wait loop: exact cycle/PC/PPC boundaries and IRQ/mapping/instruction guards");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(preamble+function+main)
 subprocess.run(['g++','-O2','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
