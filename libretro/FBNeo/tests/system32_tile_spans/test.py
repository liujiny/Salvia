#!/usr/bin/env python3
"""Compare cached tile spans and complete row/zoom functions with the originals."""
from pathlib import Path
import subprocess
import tempfile

p = Path(__file__).resolve().parent
s = (p.parents[1] / 'src/burn/drv/sega/d_segas32.cpp').read_text()
a = s.index('template<INT32 Step, bool Opaque>')
b = s.index('static void update_tilemap_text(', a)
code = s[a:b]
preamble = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
typedef int32_t INT32;
struct clip_struct { INT32 nMinx,nMaxx,nMiny,nMaxy; };
struct extents_list { UINT8 scan_extent[256]; UINT16 extent[32][16]; };
struct cache_entry { int tmap,dirty; };
static cache_entry tmap_cache[4], *tilemap_cache;
static UINT16 maps[4][512*256], ram[0x10000];
static UINT16 *output;
static UINT8 transparent_check[32][256];
static extents_list clips;
static int ids[4], flip_x, flip_y, clip_start, opaquey_hack;
static bool swap_bytes;
static UINT16 endian(UINT16 x) { return swap_bytes ? UINT16((x<<8)|(x>>8)) : x; }
#define BURN_ENDIAN_SWAP_INT16(x) endian(x)
// Preserve the driver's sign extension without signed-left-shift UB in the stub.
static INT32 sign_extend(UINT32 x, unsigned bits) { return INT32(x<<(32-bits))>>(32-bits); }
static void get_tilemaps(INT32,INT32 *out) { memcpy(out,ids,sizeof(ids)); }
static void GenericTilemapDraw(INT32,INT32,INT32) {}
static UINT16 *BurnBitmapGetBitmap(INT32 id) { assert(id>=32 && id<36);return maps[id-32]; }
static UINT16 *BurnBitmapGetPosition(INT32,INT32 x,INT32 y) { assert(y>=0 && y<256);return output+y*512+x; }
static void compute_tilemap_flips(INT32,INT32 &x,INT32 &y) { x=flip_x;y=flip_y; }
static INT32 compute_clipping_extents(INT32,INT32,INT32,clip_struct,extents_list *out) { *out=clips;return clip_start; }
static UINT32 seed=0x5325a11;
static UINT32 rnd() { seed=seed*1664525+1013904223;return seed; }
'''
main = r'''
int main() {
 static UINT16 expected[512*256],actual[512*256];
 static UINT8 expected_flags[32][256];
 for(int m=0;m<4;m++) {
  tmap_cache[m].tmap=m;
  for(int i=0;i<512*256;i++) {
   UINT16 v=rnd()>>16;
   if(i%3==0)v&=0xfff0;
   maps[m][i]=v;
  }
 }
 // Independently exercise forward/backward page boundaries, multiple wraps,
 // unaligned source/destination starts, empty spans and opaque zero pens.
 const UINT16 *src[2]={maps[0],maps[1]};
 for(int trial=0;trial<4000;trial++) {
  swap_bytes=trial&1;
  UINT32 sx=rnd();int step=(trial&2)?-1:1,count=(rnd()>>16)%1800;
  int opaque=(trial&4)!=0, zeros=0;
  std::fill(expected,expected+2048,0xdead);std::fill(actual,actual+2048,0xdead);
  UINT32 x=sx;
  for(int i=0;i<count;i++,x+=step) {
   UINT16 pix=src[(x>>9)&1][x&511];
   if(!opaque && !(pix&15)){pix=0;zeros++;}
   expected[i+3]=endian(pix);
  }
  assert(system32_copy_tile_span(actual+3,src,sx,step,count,opaque)==zeros);
  assert(!memcmp(actual,expected,2048*sizeof(UINT16)));
 }
 unsigned unity=0,scaled=0,rows=0;
 const int zooms[]={0x200,0x200,0x200,0x1ff,0x201,0x80,0x400,0,0xfff};
 for(int trial=0;trial<1800;trial++) {
  // Exercise rows made entirely of transparent pen zero, including a
  // nonzero palette bank; then restore varied pixels for subsequent cases.
  if(trial%30<2)for(int m=0;m<4;m++)for(int i=0;i<512*256;i++) {
   UINT16 v=rnd()>>16;
   maps[m][i]=(trial%30==0 || i%3==0) ? (v&0xfff0) : v;
  }
  swap_bytes=trial&1;flip_x=(trial>>1)&1;flip_y=(trial>>2)&1;
  clip_start=(trial>>3)&1;opaquey_hack=(trial>>4)&1;
  for(int i=0;i<0x10000;i++)ram[i]=rnd()>>16;
  for(int i=0;i<4;i++)ids[i]=i;
  for(int i=3;i>0;i--)std::swap(ids[i],ids[(rnd()>>16)%(i+1)]);
  clip_struct rect;
  rect.nMinx=(rnd()>>16)%80;rect.nMaxx=rect.nMinx+(rnd()>>16)%(512-rect.nMinx);
  rect.nMiny=(rnd()>>16)%16;rect.nMaxy=rect.nMiny+(rnd()>>16)%24;
  for(int i=0;i<32;i++) {
   int parts=1+(rnd()>>16)%8;
   clips.extent[i][0]=rect.nMinx;
   for(int k=1;k<parts;k++)clips.extent[i][k]=rect.nMinx+(rnd()>>16)%(rect.nMaxx-rect.nMinx+2);
   clips.extent[i][parts]=rect.nMaxx+1;
   std::sort(clips.extent[i],clips.extent[i]+parts+1);
  }
  for(int y=0;y<256;y++)clips.scan_extent[y]=(rnd()>>16)%32;
  int bg=trial&1;
  int zoom=zooms[(trial/2)%9];
  ram[0x1ff50/2+2*bg]=endian(zoom);
  ram[0x1ff52/2+2*bg]=endian(zooms[(trial/18)%9]);
  if(zoom==0x200)unity++;else scaled++;
  for(int mode=0;mode<2;mode++) {
   std::fill(expected,expected+512*256,0xdead);std::fill(actual,actual+512*256,0xdead);
   memset(transparent_check,0xab,sizeof(transparent_check));
   output=expected;
   if(mode)reference_rowscroll(rect,ram,3+(trial&1),2+(trial&1));
   else reference_zoom(rect,ram,bg+1,bg);
   memcpy(expected_flags,transparent_check,sizeof(expected_flags));
   memset(transparent_check,0xab,sizeof(transparent_check));
   output=actual;
   if(mode){update_tilemap_rowscroll(rect,ram,3+(trial&1),2+(trial&1));rows++;}
   else update_tilemap_zoom(rect,ram,bg+1,bg);
   assert(!memcmp(actual,expected,sizeof(actual)));
   assert(!memcmp(transparent_check,expected_flags,sizeof(expected_flags)));
  }
 }
 printf("PASS tile spans: 4000 direct spans; %u unity / %u scaled zoom and %u rowscroll cases, clipping/flip/endian/opaque/wrap\n",unity,scaled,rows);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    (t / 'test.cpp').write_text(preamble + code + (p / 'reference.inc').read_text() + main)
    subprocess.run(['g++', '-O2', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(t / 'test.cpp'), '-o', str(t / 'test')], check=True)
    subprocess.run([str(t / 'test')], check=True)
