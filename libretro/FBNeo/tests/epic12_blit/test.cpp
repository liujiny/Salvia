// Compare the fast fixed-alpha renderer with the original EPIC12 routines.
#define EPIC12_BLIT_TEST
#include "../../src/burn/devices/epic12.cpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned seed=0x26581234;
static unsigned rnd(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static UINT32 pixel(){return rnd()&0x20f8f8f8;}
static unsigned clamp(unsigned x){return x>31?31:x;}
int main(){
 setbuf(stdout,NULL);
 for(int a=0;a<32;a++)for(int b=0;b<64;b++)epic12_device_colrtable[a][b]=clamp(a*b/31);
 for(int a=0;a<32;a++)for(int b=0;b<32;b++)epic12_device_colrtable_add[a][b]=clamp(a+b);
 epic12_init_blend_tables();
 for(int t=0;t<64;t++)for(int a=0;a<32;a++)for(int c=0;c<32;c++)
  if(epic12_source_scale[t][a][c]!=clamp(clamp(c*t/31)*a/31))return 1;
 for(int i=0;i<1000000;i++){
  UINT32 a=pixel()&0x00f8f8f8,b=pixel()&0x00f8f8f8,expected=0;
  for(int sh=3;sh<=19;sh+=8)expected|=clamp(((a>>sh)&31)+((b>>sh)&31))<<sh;
  if(epic12_packed_add(a,b)!=expected)return 2;
 }
 puts("PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums");
 const unsigned words=0x2000*0x1000;
 UINT32 *ref=(UINT32*)calloc(words,4),*got=(UINT32*)calloc(words,4);
 if(!ref||!got)return 3;
 epic12_device_blitfunction funcs[2][2][2]={
  {{draw_sprite_f0_ti0_tr0_s0_d0,draw_sprite_f0_ti1_tr0_s0_d0},
   {draw_sprite_f0_ti0_tr1_s0_d0,draw_sprite_f0_ti1_tr1_s0_d0}},
  {{draw_sprite_f1_ti0_tr0_s0_d0,draw_sprite_f1_ti1_tr0_s0_d0},
   {draw_sprite_f1_ti0_tr1_s0_d0,draw_sprite_f1_ti1_tr1_s0_d0}}
 };
 for(int i=0;i<5000;i++){
  int w=1+rnd()%128,h=1+rnd()%32,dx=64+rnd()%128,dy=64+rnd()%32;
  int sx=256+rnd()%128,sy=32+rnd()%128,fx=i&1,fy=(i>>1)&1,tr=(i>>2)&1,ti=(i>>3)&1;
  int sa=rnd()%32,da=i%3?31:rnd()%32;
  if(i%5==0){sx=dx+(int)(rnd()%17)-8;sy=dy+(int)(rnd()%9)-4;}
  if(i%7==0)sy=4090+rnd()%6;
  if(i%11==0)sx=8180+rnd()%12;
  if(i%13==0){dx-=128;dy-=96;}
  rectangle clip;clip.set(32,256,16,128);
  clr_t tint;tint.r=ti?rnd()%64:32;tint.g=ti?rnd()%64:32;tint.b=ti?rnd()%64:32;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   unsigned pos=((sy+y)&4095)*8192+((sx+x)&8191);ref[pos]=got[pos]=pixel();
  }
  int y0=dy<clip.min_y?clip.min_y:dy,y1=dy+h-1>clip.max_y?clip.max_y:dy+h-1;
  for(int y=y0-1;y<=y1+1;y++)if(y>=0&&y<4096)
   for(int x=16;x<=272;x++){unsigned pos=y*8192+x;ref[pos]=got[pos]=pixel();}
  m_bitmaps=ref;epic12_device_blit_delay=0;
  funcs[fx][tr][ti](&clip,ref,sx,sy,dx,dy,w,h,fy,sa,da,&tint);
  UINT64 delay=epic12_device_blit_delay;
  m_bitmaps=got;epic12_device_blit_delay=0;
  epic12_draw_fixed(fx,tr,&clip,got,sx,sy,dx,dy,w,h,fy,sa,da,&tint);
  if(epic12_device_blit_delay!=delay){printf("FAIL delay case %d\n",i);return 4;}
  for(int y=y0-1;y<=y1+1;y++)if(y>=0&&y<4096)
   if(memcmp(ref+y*8192,got+y*8192,8192*4)){
    for(int x=0;x<8192;x++)if(ref[y*8192+x]!=got[y*8192+x]){
     printf("FAIL image case %d xy %d,%d ref %08x got %08x flip %d,%d trans %d tint %d,%d,%d alpha %d,%d\n",i,x,y,ref[y*8192+x],got[y*8192+x],fx,fy,tr,tint.r,tint.g,tint.b,sa,da);break;
    }
    return 5;
   }
 }
 puts("PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay");
 free(ref);free(got);return 0;
}
