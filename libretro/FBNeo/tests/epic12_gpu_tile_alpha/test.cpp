#include "intrinsics.h"
#include "../../src/burn/devices/epic12_gpu_tile_alpha.h"
#include <stdio.h>

// Independent full-surface address formula, not the production block inverse.
static unsigned reference(unsigned x,unsigned y,unsigned width) {
 unsigned macro=((x/32)+(y/32)*(width/32))*512;
 unsigned micro=((x%8)+(y&6)*4)*4;
 unsigned offset=macro+(micro/16)*32+(micro%16)+(y&8)*32+(y&1)*16;
 return (offset/512)*4096+(offset&448)*4+(offset&63)+(y&16)*128+
  ((((y&8)/4)+(x/8))%4)*64;
}
static unsigned pattern(unsigned x,unsigned y,unsigned slot,unsigned pass) {
 unsigned v=(x*0x1f3d5b79U)^(y*0x9e3779b9U)^(slot*0x71da89c3U)^(pass*0xa73ec451U);
 if(slot%4==0)v&=~0x20000000U;
 if(slot%4==1)v|=0x20000000U;
 if(slot%4==2)v=(v&~0x20000000U)|((x==(slot+pass)%128 && y==(slot*7+pass)%128)?0x20000000U:0);
 return v;
}
int main() {
 assert(epic12_gpu_alpha_vector_selftest());assert(epic12_gpu_tile_alpha_selftest());
 alpha_test_corrupt_store=true;assert(!epic12_gpu_tile_alpha_selftest());
 assert(!epic12_tile_alpha_enabled);alpha_test_corrupt_store=false;
 assert(epic12_gpu_tile_alpha_selftest());
 unsigned pages=0;unsigned long long pixels=0;
 const int pitches[]={8192,132,128};
 for(int height=1024;height<=2048;height*=2)for(unsigned pass=0;pass<3;++pass) {
  tile_test_bytes=(size_t)2048*height*4;
  unsigned char* allocation=(unsigned char*)malloc(tile_test_bytes+287);
  assert(allocation);unsigned char* start=(unsigned char*)(((uintptr_t)allocation+127)&~(uintptr_t)127);
  tile_test_atlas=start+128;memset(start,0x5a,tile_test_bytes+256);
  int pitch=pitches[pass];void* sourceAllocation=malloc(pitch*128*4+31);assert(sourceAllocation);
  uint32_t* source=(uint32_t*)(((uintptr_t)sourceAllocation+15)&~(uintptr_t)15);
  for(int slot=0;slot<height/8;++slot) {
   for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)source[y*pitch+x]=pattern(x,y,slot,pass);
   Epic12GpuAlphaPage alpha,expected;
   // Populate a stale bbox entry; uploading must invalidate it.
   alpha.cacheKeys[0]=0;alpha.cacheBounds[0]=0x80000000U;
   tile_test_slot=slot;tile_test_writes=0;tile_test_track=true;
   unsigned long long before=alpha_test_vector_loads;
   assert(epic12_gpu_tile_alpha_page(tile_test_atlas,source,slot,alpha,pitch));
   assert(alpha_test_vector_loads-before==4096+5); // exactly one 64 KiB source pass
   assert(tile_test_writes==4096);tile_test_track=false;
   epic12_alpha_vector_enabled=false;expected.build(source,pitch);epic12_alpha_vector_enabled=true;
   assert(!memcmp(alpha.rows,expected.rows,sizeof(alpha.rows)));
   assert(!memcmp(alpha.groups,expected.groups,sizeof(alpha.groups)));
   assert(alpha.cacheKeys[0]==~0U);
   for(int q=0;q<16;++q) {
    int x0=(q*7)%128,y0=(q*5)%128,x1=127-(q%7),y1=127-(q%5);
    int a=-1,b=-2,c=-3,d=-4,e=a,f=b,g=c,h=d;
    bool got=alpha.bounds(x0,y0,x1,y1,a,b,c,d),want=expected.bounds(x0,y0,x1,y1,e,f,g,h);
    assert(got==want && a==e && b==f && c==g && d==h);
   }
   unsigned ax=(slot%16)*128,ay=(slot/16)*128;
   for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x) {
    unsigned expectedPixel=pattern(x,y,slot,pass);
    assert(*(unsigned*)(tile_test_atlas+reference(ax+x,ay+y,2048))==expectedPixel);
    assert(source[y*pitch+x]==expectedPixel);
   }
   // Disabled and unaligned cases must return before any stores/cache changes.
   Epic12GpuAlphaPage saved=alpha;unsigned writes=tile_test_writes;
   assert(!epic12_gpu_tile_alpha_page(tile_test_atlas,source+1,slot,alpha,pitch));
   assert(!epic12_gpu_tile_alpha_page(tile_test_atlas,source,slot,alpha,pitch+1));
   epic12_tile_alpha_enabled=false;assert(!epic12_gpu_tile_alpha_page(tile_test_atlas,source,slot,alpha,pitch));
   epic12_tile_alpha_enabled=true;epic12_alpha_vector_enabled=false;
   assert(!epic12_gpu_tile_alpha_page(tile_test_atlas,source,slot,alpha,pitch));epic12_alpha_vector_enabled=true;
   assert(writes==tile_test_writes && !memcmp(&saved,&alpha,sizeof(alpha)));
   for(int i=0;i<128;++i)assert(start[i]==0x5a && tile_test_atlas[tile_test_bytes+i]==0x5a);
   ++pages;pixels+=16384;
  }
  for(unsigned y=0;y<(unsigned)height;++y)for(unsigned x=0;x<2048;++x) {
   unsigned slot=(y/128)*16+x/128;
   assert(*(unsigned*)(tile_test_atlas+reference(x,y,2048))==pattern(x%128,y%128,slot,pass));
  }
  free(sourceAllocation);free(allocation);tile_test_atlas=NULL;
 }
 printf("PASS fused tile/alpha: %u pages, %llu pixels; all slots, masks, groups, bbox cache, guards, source immutability, fallback and injected self-test failure\n",pages,pixels);
 puts("PASS exactly 4096 source vector loads/page plus five constants; 4096 sequential aligned vector stores/page; no atlas vector reads");
 return 0;
}
