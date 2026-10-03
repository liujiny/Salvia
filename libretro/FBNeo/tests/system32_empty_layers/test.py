#!/usr/bin/env python3
"""Compare actual System 32 mixer code with its pre-compaction loop."""
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;r=p.parents[1]
s=(r/'src/burn/drv/sega/d_segas32.cpp').read_text()
start=s.index('struct mixer_layer_info');end=s.index('\nstatic void mix_all_layers',start)
code=s[start:end]
preamble=r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;typedef int32_t INT32;
#define MIXER_LAYER_TEXT 0
#define MIXER_LAYER_NBG0 1
#define MIXER_LAYER_NBG1 2
#define MIXER_LAYER_NBG2 3
#define MIXER_LAYER_NBG3 4
#define MIXER_LAYER_BITMAP 5
#define MIXER_LAYER_SPRITES 6
#define MIXER_LAYER_BACKGROUND 7
#define MIXER_LAYER_MULTISPR 9
struct clip_struct {INT32 nMinx,nMaxx,nMiny,nMaxy;};
static bool swap_bytes;
static UINT16 endian(UINT16 x){return swap_bytes?UINT16((x<<8)|(x>>8)):x;}
#define BURN_ENDIAN_SWAP_INT16(x) endian(x)
static const int W=32,H=12;
static int nScreenWidth=W;
static UINT16 pixels[8][H][W],solid_0000[512],solid_ffff[512],palette[0x4000];
static bool empty[8][H];
static UINT16 *pTransDraw;
static UINT16 *get_layer_scanline(int layer,int row){if(layer==9)layer=6;return empty[layer][row]?(layer==6?solid_ffff:solid_0000):pixels[layer][row];}
static UINT32 seed=0x532360;
static UINT32 rnd(){seed=seed*1664525+1013904223;return seed;}
"""
main=r"""
int main(){
 for(int i=0;i<512;i++)solid_ffff[i]=0xffff;
 UINT16 expected[W*H],actual[W*H];
 for(int trial=0;trial<2000;trial++){
  swap_bytes=trial&1;System32MixerContext &c=system32_mixer;memset(&c,0,sizeof(c));
  c.which=trial&1;c.cliprect.nMaxx=W-1;c.cliprect.nMaxy=H-1;c.blendfactor=rnd()&7;
  c.sprgroup_shift=10+(rnd()%5);c.sprgroup_mask=(1U<<(rnd()%5))-1;
  c.sprshadowmask=(rnd()&1)?0x8000:0;c.sprpixmask=((1<<c.sprgroup_shift)-1)&0x3fff;c.sprshadow=0x7ffe&c.sprpixmask;
  c.sprdx=(trial&2)?-1:1;c.sprx_start=c.sprdx<0?W-1:0;c.sprdy=(trial&4)?-1:1;c.spry=c.sprdy<0?H-1:0;
  c.m_paletteram=palette;
  for(int i=0;i<0x4000;i++)palette[i]=rnd()>>16;
  for(int i=0;i<3;i++)for(int j=0;j<3;j++)c.rgboffs[i][j]=trial%3?int(rnd()%64)-32:0;
  for(int g=0;g<=c.sprgroup_mask;g++){
   int ids[8]={0,1,2,3,4,5,6,7};for(int i=6;i>0;i--)std::swap(ids[i],ids[rnd()%(i+1)]);
   if(trial%7==0){for(int i=0;i<7;i++)if(ids[i]==6)std::swap(ids[i],ids[7]);}
   for(int i=0;i<8;i++){mixer_layer_info &v=c.layerorder[g][i];v.index=ids[i];v.palbase=rnd()&0x3fff;v.mixshift=rnd()&3;v.coloroffs=rnd()%3;v.blendmask=ids[i]>=6?0:rnd()&0xff;v.sprblendmask=rnd()&0xffff;}
  }
  for(int layer=0;layer<8;layer++)for(int y=0;y<H;y++){
   empty[layer][y]=(trial%4==0)||((rnd()>>16)%3==0);
   for(int x=0;x<W;x++){UINT16 v=rnd()>>16;if(x%5==0)v=layer==6?0xffff:0;if(layer==6&&x%7==0)v=c.sprshadow;pixels[layer][y][x]=v;}
  }
  pTransDraw=expected;reference_mix_rows(0,H,0);
  pTransDraw=actual;system32_mix_rows(0,H,0);assert(!memcmp(expected,actual,sizeof(actual)));
  memset(actual,0,sizeof(actual));system32_mix_rows(0,H/3,0);system32_mix_rows(H/3,2*H/3,1);system32_mix_rows(2*H/3,H,2);assert(!memcmp(expected,actual,sizeof(actual)));
 }
 puts("PASS System 32 empty-layer compaction: 2000 blend/shadow/flip/endian/group cases, whole and split rows");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);(t/'test.cpp').write_text(preamble+code+(p/'reference.inc').read_text()+main)
 subprocess.run(['g++','-O2','-fsanitize=address,undefined',str(t/'test.cpp'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
