#!/usr/bin/env python3
"""Exercise the actual V60 Xbox byte-read branches on a host, without ROMs."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
source = (root / "src/cpu/v60/v60.cpp").read_text()
functions = []
for name, width in [("cpu_readop16", 16), ("cpu_readop32", 32), ("program_read_word_16le", 16)]:
    first = source.index("static UINT" + str(width) + " " + name + "(")
    last = source.index("\n}\n", first) + 3
    functions.append(source[first:last])
preamble = r"""
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
#define FBNEO_V60_BYTE_READ_TEST
#define page_size 0x800
#define page_mask 0x7ff
#define BURN_ENDIAN_SWAP_INT16(x) (x)
#define BURN_ENDIAN_SWAP_INT32(x) (x)
static UINT8 *pages[3][4]; static UINT8 **mem[3]={pages[0],pages[1],pages[2]};
static UINT32 address_mask=0x1fff;
static unsigned calls;
static UINT16 read16(UINT32 a) { calls++; return UINT16(a ^ 0xa591); }
static UINT32 read32(UINT32 a) { calls++; return a ^ 0x8192a591; }
static UINT16 (*v60_read16)(UINT32)=read16;
static UINT32 (*v60_read32)(UINT32)=read32;
"""
main = r"""
int main() {
 UINT8 data[8196]; unsigned seed=0x195032;
 for (unsigned pass=0;pass<32;pass++) {
  for(unsigned i=0;i<sizeof(data);i++) {seed=seed*1664525+1013904223;data[i]=seed>>24;}
  for(unsigned bank=0;bank<3;bank++)for(unsigned page=0;page<4;page++)pages[bank][page]=data+page*page_size;
  for(unsigned a=0;a<8192;a++) {
   UINT16 w=data[a]|(UINT16(data[a+1])<<8);
   UINT32 d=UINT32(w)|(UINT32(data[a+2])<<16)|(UINT32(data[a+3])<<24);
   assert(cpu_readop16(a)==w); assert(cpu_readop32(a)==d);assert(program_read_word_16le(a)==w);
   assert(cpu_readop16(a+8192)==w);
  }
 }
 for(unsigned bank=0;bank<3;bank++)for(unsigned page=0;page<4;page++)pages[bank][page]=0;
 calls=0;assert(cpu_readop16(8195)==UINT16(3^0xa591));assert(cpu_readop32(8195)==(3^0x8192a591U));assert(program_read_word_16le(8195)==UINT16(3^0xa591));assert(calls==3);
 v60_read16=0;v60_read32=0;assert(cpu_readop16(3)==0);assert(cpu_readop32(3)==0);assert(program_read_word_16le(3)==0);
 puts("PASS V60 byte reads: all alignments, contiguous page ends, address mask and handler fallback");
}
"""
with tempfile.TemporaryDirectory() as temporary:
    p = Path(temporary); (p/"test.cpp").write_text(preamble + "\n".join(functions) + main)
    subprocess.run(["g++", "-O2", "-fsanitize=address,undefined", str(p/"test.cpp"), "-o", str(p/"test")], check=True)
    subprocess.run([str(p/"test")], check=True)
