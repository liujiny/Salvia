/* Host regression harness: uses the actual ported COP/crypto functions.
   The bus stub models MAME's CPU1 RAM alias; it is not a V30 emulator. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef unsigned char UINT8;
typedef signed char INT8;
typedef unsigned short UINT16;
typedef short INT16;
typedef unsigned int UINT32;
typedef int INT32;
#define M_PI 3.14159265358979323846
#define READ_HANDLER(n) int n(int offset)
#define WRITE_HANDLER(n) void n(int offset,int data)
#define r2_debug_access(a,v,w) ((void)0)
#define r2_debug_bank() ((void)0)
static UINT8 bus[0x200000], saved[0x40000], *banks[5], *r2_bank_rom=saved;
static UINT16 r2_prg_bank,r2_layer_enable;
static UINT8 r2_scroll[12];
static int bg_bank,mid_bank,fg_bank;
static void *background_layer,*midground_layer,*foreground_layer;
static void tilemap_mark_all_tiles_dirty(void *p) { (void)p; }
static int cpu_getactivecpu(void) { return 0; }
static void cpu_setbank(int n,UINT8 *p) { banks[n]=p; }
static int r2_cop_r(int offset);
static void r2_cop_w(int offset,int data);
static int cpu_readmem20(unsigned a) {
 a&=0xfffff;
 if(a>=0x400 && a<0x700) return r2_cop_r(a-0x400);
 if(a>=0x20000 && a<0x30000) return banks[3][a-0x20000];
 if(a>=0x30000 && a<0x40000) return banks[4][a-0x30000];
 return bus[a];
}
static void cpu_writemem20(unsigned a,int v) {
 a&=0xfffff;
 if(a>=0x400 && a<0x700) { r2_cop_w(a-0x400,v);return; }
 if(a<0x20000) bus[a]=(UINT8)v;
}
#include "../libretro/mame-2003-plus/src/drivers/raiden2_cop.inc"
#include "../libretro/mame-2003-plus/src/drivers/raiden2_r2crypt.inc"
static unsigned sound_reg, sound_value, sound_mask, sound_writes;
static UINT16 seibu_main_word_r(unsigned reg,unsigned mask)
{
 (void)mask; sound_reg=reg; return (UINT16)(0x50+reg);
}
static void seibu_main_word_w(unsigned reg,unsigned value,unsigned mask)
{
 sound_reg=reg;sound_value=value;sound_mask=mask;sound_writes++;
}
#include "../libretro/mame-2003-plus/src/drivers/raiden2_sound.inc"
#include "../libretro/mame-2003-plus/src/mame2003/video_rotate565.h"
static void verify_rotate565(void)
{
 static UINT16 input[336*256], actual[336*256], expected[336*256];
 static UINT32 palette[2048];
 int i, fx, fy, x, y, w, h, mode;
 UINT32 c;
 for(i=0;i<336*256;i++) input[i]=(UINT16)((i*37+i/336)%2048);
 for(i=0;i<2048;i++) palette[i]=(i*7919U)&0xffffff;
 for(mode=0;mode<2;mode++)
 for(fx=0;fx<2;fx++) for(fy=0;fy<2;fy++)
 {
  w=mode?317:320;h=mode?237:240;
  memset(actual,0xa5,sizeof(actual));memset(expected,0xa5,sizeof(expected));
  x360_rotate565(input,actual,336,256,3,5,w,h,palette,fx,fy);
  /* Independent scalar reference: source column becomes output row. */
  for(x=0;x<w;x++) for(y=0;y<h;y++)
  {
   c=palette[input[(5+y)*336+3+x]];
   expected[(fy?w-1-x:x)*256+(fx?h-1-y:y)]=(UINT16)
    (((c>>16)&0xf8)<<8 | ((c>>8)&0xfc)<<3 | (c&0xf8)>>3);
  }
  assert(memcmp(actual,expected,sizeof(actual))==0);
 }
 puts("PASS: tiled RGB565 rotation, four flips, cropped origin, partial blocks and pitch padding");
}
int main(void)
{
 unsigned i;
 {
  UINT8 src[64*9], dst[80*9], expected[80*9];
  unsigned y, x;
  for(i=0;i<sizeof(src);i++) src[i]=(UINT8)(i*17);
  memset(dst,0xa5,sizeof(dst));memset(expected,0xa5,sizeof(expected));
  for(y=0;y<9;y++) for(x=0;x<62;x++) expected[y*80+x]=src[y*64+x];
  x360_copy565_rows(dst,80,src,64,62,9);
  assert(!memcmp(dst,expected,sizeof(dst)));
  memset(dst,0xa5,sizeof(dst));
  x360_copy565_rows(dst,64,src,64,64,9);
  assert(!memcmp(dst,src,sizeof(src)));
  for(i=sizeof(src);i<sizeof(dst);i++) assert(dst[i]==0xa5);
  puts("PASS: staged texture copy, unequal pitches, padding preservation and contiguous copy");
 }
 verify_rotate565();
 /* Board address contract: 0708 reads reply0, 0710 asserts IRQ,
    0714 reads pending, 0718 publishes commands. Upper lane is disconnected. */
 assert(r2_sound_main_r(8)==0x52 && sound_reg==2);
 assert(r2_sound_main_r(12)==0x53 && sound_reg==3);
 assert(r2_sound_main_r(20)==0x55 && sound_reg==5);
 assert(r2_sound_main_r(9)==0 && sound_reg==2);
 r2_sound_main_w(0,0x12);assert(sound_reg==0 && sound_value==0x12);
 r2_sound_main_w(4,0x34);assert(sound_reg==1 && sound_value==0x34);
 r2_sound_main_w(16,0);assert(sound_reg==4 && sound_mask==0xff00);
 r2_sound_main_w(24,0);assert(sound_reg==6);
 i=sound_writes;r2_sound_main_w(17,0xff);assert(sound_writes==i);
 assert(r2_sound_main_r(10)==0x52); /* bit1 mirror */
 puts("PASS: Raiden II Seibu 4-byte spacing, command/reply/IRQ/pending, byte lanes and mirror");
 for(i=0;i<sizeof(saved);i++) bus[i]=(UINT8)((i*13)^(i>>12)^0x5a);
 memcpy(saved,bus,sizeof(saved));
 memset(bus,0,0x20000); /* Stage2 destroys the lower ROM page here. */
 assert(memcmp(saved,bus,0x20000)!=0);
 r2_main_bankswitch(0x8000);
 for(i=0;i<0x20000;i++) assert(cpu_readmem20(0x20000+i)==saved[i]);
 r2_main_bankswitch(0);
 for(i=0;i<0x20000;i++) assert(cpu_readmem20(0x20000+i)==saved[0x20000+i]);
 r2_reset_cop();
 r2_mem_w32(0x1001,0x89abcdefU);
 assert(bus[0x1001]==0xef && bus[0x1004]==0x89);
 assert(r2_mem_r16(0x1002)==0xabcd && r2_mem_r32(0x1001)==0x89abcdefU);
 /* Actual byte-dispatch register assembly, CRTC and command trigger. */
 r2_mem_w16(0x4a0,0);r2_mem_w16(0x4c0,0x2000);
 assert(r2_cop_regs[0]==0x2000);
 r2_mem_w32(0x2004,0xffff8000U);r2_mem_w32(0x2010,0x00010000);
 r2_mem_w16(0x500,0x0205);
 assert(r2_mem_r32(0x2004)==0x00008000 && r2_mem_r16(0x201e)==1);
 r2_mem_w16(0x61c,0x001f);r2_mem_w16(0x620,0x1234);
 assert(r2_layer_enable==31 && r2_scroll[0]==0x34 && r2_scroll[1]==0x12);
 r2_mem_w16(0x47e,9);r2_mem_w16(0x478,0x80);
 r2_mem_w16(0x47c,0x100);r2_mem_w16(0x47a,0x200);
 r2_mem_w16(0x6fc,0);
 assert(r2_mem_r32(0x4004)==0x00008000);
 /* Equal keys retain original order after the lower key moves forward. */
 r2_cop_sort_lookup=0x5000;r2_cop_sort_ram_addr=0x6000;r2_cop_sort_param=1;
 r2_mem_w16(0x5000,0);r2_mem_w16(0x5002,4);r2_mem_w16(0x5004,8);
 r2_mem_w32(0x6000,2);r2_mem_w32(0x6004,2);r2_mem_w32(0x6008,1);
 r2_cop_sort_trigger(3);
 assert(r2_mem_r16(0x5000)==8 && r2_mem_r16(0x5002)==0 && r2_mem_r16(0x5004)==4);
 r2_cop_itoa_digit_count=0xffff;r2_cop_itoa=123;r2_itoa_compute();
 assert(r2_cop_itoa_digits[0]=='3' && r2_cop_itoa_digits[2]=='1');
 assert(r2_yrot(0x12345678,0)==0x12345678 && r2_yrot(0x12345678,4)==0x23456781);
 puts("PASS: ROM/RAM isolation, both bank pages, unaligned LE access, COP byte dispatch/movement, CRTC, DMA copy, stable sort, bounded ITOA, rotate-zero");
 return 0;
}
