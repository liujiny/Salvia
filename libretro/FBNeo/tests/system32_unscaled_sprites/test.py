#!/usr/bin/env python3
"""Compare the real sprite renderer with its previous implementation."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
source = (here.parents[1] / 'src/burn/drv/sega/d_segas32.cpp').read_text()
macros = source[source.index('#define sprite_draw_pixel_16('):
                source.index('static INT32 draw_one_sprite(')]
start = source.index('static INT32 draw_one_sprite(')
function = source[start:source.index('\n}\n', start) + 3]

preamble = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
typedef int16_t INT16; typedef int32_t INT32;
struct clip_struct {int nMinx,nMaxx,nMiny,nMaxy;};
static int endian;
static UINT16 swap16(UINT16 x) {return endian ? __builtin_bswap16(x) : x;}
static UINT32 swap32(UINT32 x) {return endian ? __builtin_bswap32(x) : x;}
#define BURN_ENDIAN_SWAP_INT16(x) swap16(x)
#define BURN_ENDIAN_SWAP_INT32(x) swap32(x)
#define MIXER_LAYER_SPRITES_2 0
#define MIXER_LAYER_MULTISPR_2 1
static UINT8 ram[0x20000], gfx[0x400000];
static UINT32 ram32[0x20000/4];
static UINT8 *DrvSprRAM=ram, *DrvGfxROM[2]={0,gfx};
static UINT32 *DrvSprRAM32=ram32;
static int is_multi32, graphics_length[2]={0,0x400000};
static UINT8 sprite_control_latched[8];
static UINT16 bitmap[2][512*512], before[2][512*512], reference[2][512*512];
static UINT16 *BurnBitmapGetPosition(int n,int,int) {assert(n==5||n==6);return bitmap[n-5];}
static UINT32 rng=271828;
static unsigned next() {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
'''

test = r'''
int main() {
 for(unsigned i=0;i<sizeof(gfx);i++)gfx[i]=next();
 for(unsigned i=0;i<sizeof(ram);i++)ram[i]=next();
 for(unsigned i=0;i<sizeof(ram32)/4;i++)ram32[i]=next();
 unsigned changed=0, unity=0, scaled=0;
 for(int trial=0;trial<3000;trial++) {
  endian=trial&1; is_multi32=(trial>>1)&1;
  UINT16 logical[24]={},data[24];
  logical[0]=next()&0x3fff;
  int bpp8=logical[0]&0x200, srcw=1+next()%16, srch=1+next()%40;
  int width=(trial%3)?srcw*(bpp8?4:8):(1+next()%180);
  unity+=width==srcw*(bpp8?4:8); scaled+=width!=srcw*(bpp8?4:8);
  logical[1]=(srch<<8)|(srcw<<(bpp8?0:1));
  logical[2]=(1+next()%80)|((next()&15)<<12);
  logical[3]=width|(next()&0xe800);
  logical[4]=((int)(next()%340)-80)&0xfff;
  logical[5]=((int)(next()%500)-100)&0xfff;
  logical[6]=next(); logical[7]=next()&0x1ffe;
  for(int i=8;i<24;i++)logical[i]=next();
  for(int i=0;i<24;i++)data[i]=swap16(logical[i]);
  for(int i=0;i<8;i++)sprite_control_latched[i]=next();
  clip_struct in={(int)(next()%100),200+(int)(next()%248),
                  (int)(next()%64),200+(int)(next()%70)};
  clip_struct out={(int)(next()%250),250+(int)(next()%250),
                  (int)(next()%150),150+(int)(next()%150)};
  int xoffs=(int)(next()%40)-20,yoffs=(int)(next()%40)-20;
  // Nonzero prior pixels exercise shadow writes and transparent preservation.
  memset(before,next()&255,sizeof(before));
  memcpy(bitmap,before,sizeof(bitmap));
  int a=draw_one_sprite_reference(data,xoffs,yoffs,in,out);
  memcpy(reference,bitmap,sizeof(bitmap));
  changed+=memcmp(reference,before,sizeof(reference))!=0;
  memcpy(bitmap,before,sizeof(bitmap));
  int b=draw_one_sprite(data,xoffs,yoffs,in,out);
  if(a!=b || memcmp(reference,bitmap,sizeof(bitmap))) {
   fprintf(stderr,"sprite mismatch trial=%d flags=%x width=%d endian=%d\n",
           trial,logical[0],width,endian);return 1;
  }
 }
 assert(changed>500 && unity>1500 && scaled>500);
 printf("PASS sprites: 3000 reference comparisons; %u unity, %u scaled, %u visible\n",
        unity,scaled,changed);
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(preamble + macros +
                                 (here / 'reference.inc').read_text() + function + test)
    subprocess.run(['g++', '-O2', '-std=c++98', '-UNDEBUG',
                    '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
