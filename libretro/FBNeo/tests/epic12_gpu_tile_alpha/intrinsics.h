#ifndef EPIC12_TILE_ALPHA_TEST_INTRINSICS_H
#define EPIC12_TILE_ALPHA_TEST_INTRINSICS_H
#define __lvx epic12_test_lvx_base
#include "../epic12_gpu_alpha_vmx/intrinsics.h"
#undef __lvx
static unsigned char* tile_test_atlas;
static size_t tile_test_bytes;
static int tile_test_slot;
static unsigned tile_test_writes;
static bool tile_test_track;
static __vector4 __lvx(const void* base,int offset) {
 uintptr_t p=(uintptr_t)base+offset;
 assert(!tile_test_track || p<(uintptr_t)tile_test_atlas || p>=(uintptr_t)tile_test_atlas+tile_test_bytes);
 return epic12_test_lvx_base(base,offset);
}
static __vector4 __loadunalignedvector(const void* source) {
 __vector4 v;memcpy(&v,source,16);return v;
}
static void __stvx_volatile(__vector4 v,volatile void* base,int offset) {
 uintptr_t p=(uintptr_t)base+offset;assert(!(p&15));
 if(tile_test_track) {
  assert(p>=(uintptr_t)tile_test_atlas && p+16<=(uintptr_t)tile_test_atlas+tile_test_bytes);
  unsigned address=(unsigned)(p-(uintptr_t)tile_test_atlas),tile=address/4096;
  assert((tile/64/4)*16+(tile%64/4)==(unsigned)tile_test_slot);
  assert(address%4096==(tile_test_writes%256)*16);
 }
 ++tile_test_writes;
#if defined(__ALTIVEC__)
 vec_st(v,0,(unsigned char*)p);
#else
 memcpy((void*)p,&v,16);
#endif
}
#define EPIC12_GPU_TILE_VECTOR_TEST
#endif
