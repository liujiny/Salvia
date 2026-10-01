#include "intrinsics.h"
#include "../../src/burn/devices/epic12_gpu_alpha.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned seed=0x71da89c3;
static unsigned random_word(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned cases;
static void check_row(const uint32_t* source) {
 Epic12GpuAlphaVector vector;uint32_t output[8];
 for(unsigned offset=0;offset<4;++offset) {
  for(unsigned i=0;i<8;++i)output[i]=0xc35a7d91;
  unsigned long long before=alpha_test_vector_loads;
  vector.row(source,output+offset);
  assert(alpha_test_vector_loads-before==32); // exactly 512 input bytes
  for(unsigned w=0;w<4;++w) {
   uint32_t expected=0;
   for(unsigned x=0;x<32;++x)if(source[w*32+x]&0x20000000u)expected|=1u<<(31-x);
   assert(output[offset+w]==expected);
  }
  for(unsigned i=0;i<8;++i)if(i<offset||i>=offset+4)assert(output[i]==0xc35a7d91);
  ++cases;
 }
}
static void check_page(const uint32_t* source,int pitch) {
 Epic12GpuAlphaPage page;
 // Prime a cached query; rebuilding must invalidate both empty/nonempty hits.
 page.build(source,pitch);int l=0,t=0,r=0,b=0;page.bounds(0,0,127,127,l,t,r,b);
 page.build(source,pitch);assert(page.cacheKeys[0]==~0u);
 for(unsigned y=0;y<128;++y)for(unsigned w=0;w<4;++w) {
  uint32_t expected=0;
  for(unsigned x=0;x<32;++x)if(source[y*pitch+w*32+x]&0x20000000u)expected|=1u<<(31-x);
  assert(page.rows[y][w]==expected);
 }
 for(unsigned g=0;g<16;++g)for(unsigned w=0;w<4;++w) {
  uint32_t expected=0;for(unsigned y=g*8;y<g*8+8;++y)expected|=page.rows[y][w];
  assert(page.groups[g][w]==expected);
 }
 // Individual row updates retain query invalidation and group summaries.
 for(int y=0;y<128;++y)page.build_row(y,source+y*pitch);
 assert(page.cacheKeys[0]==~0u);
 for(unsigned g=0;g<16;++g)for(unsigned w=0;w<4;++w) {
  uint32_t expected=0;for(unsigned y=g*8;y<g*8+8;++y)expected|=page.rows[y][w];
  assert(page.groups[g][w]==expected);
 }
 ++cases;
}
int main() {
 assert(epic12_gpu_alpha_vector_selftest() && epic12_alpha_vector_enabled);
 size_t pageSize=(size_t)sysconf(_SC_PAGESIZE);
 unsigned char* guard=(unsigned char*)mmap(NULL,pageSize*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(guard!=MAP_FAILED && !mprotect(guard+pageSize,pageSize,PROT_NONE));
 uint32_t* row=(uint32_t*)(guard+pageSize-512);
 for(unsigned bit=0;bit<32;++bit) {
  for(unsigned x=0;x<128;++x)row[x]=1u<<bit;
  check_row(row);
 }
 for(unsigned hot=0;hot<128;++hot) {
  for(unsigned x=0;x<128;++x)row[x]=(random_word()&~0x20000000u)|(x==hot?0x20000000u:0);
  check_row(row);
 }
 for(unsigned pass=0;pass<12000;++pass) {
  for(unsigned x=0;x<128;++x)row[x]=random_word();
  check_row(row);
 }
 munmap(guard,pageSize*2);
 const int pitches[]={128,132,133,8192,8195};
 for(unsigned p=0;p<5;++p)for(unsigned offset=0;offset<4;++offset) {
  int pitch=pitches[p];unsigned char* allocation=(unsigned char*)malloc((size_t)pitch*128*4+63);assert(allocation);
  uint32_t* source=(uint32_t*)(((uintptr_t)allocation+15)&~(uintptr_t)15)+offset;
  for(int y=0;y<128;++y)for(int x=0;x<pitch;++x)source[y*pitch+x]=random_word();
  check_page(source,pitch);
  // Failure of the real startup test must disable the vector builder, and
  // scalar metadata must stay correct even with faulty vector-store shims.
  if(p==3 && offset==0) {
   alpha_test_corrupt_store=true;
   assert(!epic12_gpu_alpha_vector_selftest() && !epic12_alpha_vector_enabled);
   unsigned long long before=alpha_test_vector_stores;check_page(source,pitch);
   assert(alpha_test_vector_stores==before);
   alpha_test_corrupt_store=false;assert(epic12_gpu_alpha_vector_selftest());
  }
  free(allocation);
 }
#if defined(__ALTIVEC__)
 puts("Backend: real big-endian PPC AltiVec gather/rotate/word-store instructions");
#else
 puts("Backend: numeric big-endian intrinsic model on host with sanitizer");
#endif
 printf("PASS %u row/page cases: all alpha positions, unrelated bits, random masks, destination lanes, guard pages, VRAM pitch, unaligned scalar fallback and failed-selftest recovery\n",cases);
 return 0;
}
