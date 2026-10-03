#!/usr/bin/env python3
"""Compare Xbox mapped opcode reads with the original V60/V70 bus readers."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
cpp = (root / "src/cpu/v60/v60.cpp").read_text()
mem = (root / "src/cpu/v60/v60mem.c").read_text()
def extract(source, name):
    start = source.rindex("static ", 0, source.index(name + "("))
    end = source.index("\n}\n", start) + 3
    return source[start:end]
functions = [extract(cpp, n) for n in ["program_read_byte_16le", "program_read_word_16le", "program_read_byte_32le", "program_read_word_32le", "program_read_dword_32le"]]
functions += [extract(mem, n) for n in ["MemRead16_16", "MemRead32_16", "MemRead16_32", "MemRead32_32", "MappedOpRead8", "MappedOpRead16", "MappedOpRead32"]]
preamble = r"""
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <vector>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32; typedef UINT32 offs_t;
#define FBNEO_V60_BYTE_READ_TEST
#define BURN_ENDIAN_SWAP_INT16(x) (x)
#define BURN_ENDIAN_SWAP_INT32(x) (x)
#define page_size 0x800
#define page_mask 0x7ff
static UINT8 *pages[3][4]; static UINT8 **mem[3]={pages[0],pages[1],pages[2]};
static UINT32 address_mask=0x1fff;
static std::vector<UINT32> events;
static UINT8 read8(UINT32 a) { events.push_back(0x10000000|a);return a^0x93; }
static UINT16 read16(UINT32 a) { events.push_back(0x20000000|a);return a^0x9517; }
static UINT32 read32(UINT32 a) { events.push_back(0x40000000|a);return a^0x19639517; }
static UINT8 (*v60_read8)(UINT32)=read8;
static UINT16 (*v60_read16)(UINT32)=read16;
static UINT32 (*v60_read32)(UINT32)=read32;
struct {struct {UINT8 (*mr8)(UINT32);UINT16 (*mr16)(UINT32);UINT32 (*mr32)(UINT32);} info;} v60;
"""
main = r"""
int main() {
 alignas(4) UINT8 data[4][page_size]; alignas(4) UINT8 fetch[4][page_size];
 unsigned seed=0x196032;
 for(unsigned p=0;p<4;p++)for(unsigned i=0;i<page_size;i++) {seed=seed*1664525+1013904223;data[p][i]=seed>>24;fetch[p][i]=~data[p][i];}
 for(unsigned bus=0;bus<2;bus++) {
  v60.info.mr8=bus ? program_read_byte_32le : program_read_byte_16le;
  v60.info.mr16=bus ? MemRead16_32 : MemRead16_16;
  v60.info.mr32=bus ? MemRead32_32 : MemRead32_16;
  for(unsigned masktest=0;masktest<3;masktest++) {
   address_mask=masktest==0?0x1fff:masktest==1?0xfff:0x3ff;
   for(unsigned mapped=0;mapped<16;mapped++) {
    for(unsigned p=0;p<4;p++) {pages[0][p]=(mapped&(1<<p))?data[3-p]:0;pages[2][p]=fetch[p];}
    for(unsigned a=0;a<8192;a++) {
     events.clear();UINT32 e8=v60.info.mr8(a);std::vector<UINT32> c8=events;
     events.clear();assert(MappedOpRead8(a)==e8);assert(events==c8);
     events.clear();UINT32 e16=v60.info.mr16(a);std::vector<UINT32> c16=events;
     events.clear();assert(MappedOpRead16(a)==e16);assert(events==c16);
     events.clear();UINT32 e32=v60.info.mr32(a);std::vector<UINT32> c32=events;
     events.clear();assert(MappedOpRead32(a)==e32);assert(events==c32);
    }
   }
  }
 }
 puts("PASS V60/V70 mapped fetch: all alignments, split pages, unmapped handlers, READ/FETCH differences, wrapping and subpage masks");
}
"""
with tempfile.TemporaryDirectory() as temporary:
    p = Path(temporary); (p/"test.cpp").write_text(preamble + "\n".join(functions) + main)
    subprocess.run(["g++", "-std=c++11", "-O2", "-fsanitize=address,undefined", str(p/"test.cpp"), "-o", str(p/"test")], check=True)
    subprocess.run([str(p/"test")], check=True)
