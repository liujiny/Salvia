// Big-endian vector semantics on hosts; real AltiVec instructions on PPC.
#ifndef EPIC12_ALPHA_TEST_INTRINSICS_H
#define EPIC12_ALPHA_TEST_INTRINSICS_H
#include <stdint.h>
#include <string.h>
#include <assert.h>
static bool alpha_test_corrupt_store;
static unsigned long long alpha_test_vector_loads,alpha_test_vector_stores;
#if defined(__ALTIVEC__)
#include <altivec.h>
#undef bool
#undef pixel
#undef vector
typedef __vector unsigned char __vector4;
static __vector4 __lvx(const void* base,int offset) {
 assert(!(((uintptr_t)base+offset)&15));++alpha_test_vector_loads;
 return vec_ld(offset,(const unsigned char*)base);
}
static __vector4 __vperm(__vector4 a,__vector4 b,__vector4 control) {return vec_perm(a,b,control);}
static __vector4 __vand(__vector4 a,__vector4 b) {return vec_and(a,b);}
static __vector4 __vor(__vector4 a,__vector4 b) {return vec_or(a,b);}
static __vector4 __vrlb(__vector4 a,__vector4 b) {return vec_rl(a,b);}
#define __vsldoi(a,b,n) vec_sld((a),(b),(n))
static void __stvewx(__vector4 value,void* base,int offset) {
 assert(!(((uintptr_t)base+offset)&3));++alpha_test_vector_stores;
 __vector unsigned int words=(__vector unsigned int)value;
 if(alpha_test_corrupt_store) {
  const __vector unsigned int fault={1,1,1,1};words=vec_xor(words,fault);
 }
 vec_ste(words,offset,(unsigned int*)base);
}
#else
struct __vector4 {uint32_t words[4];};
static unsigned alpha_test_byte(const __vector4& v,unsigned i) {return (v.words[i/4]>>(24-(i%4)*8))&255;}
static void alpha_test_put(__vector4& v,unsigned i,unsigned byte) {v.words[i/4]|=(byte&255)<<(24-(i%4)*8);}
static __vector4 __lvx(const void* base,int offset) {
 assert(!(((uintptr_t)base+offset)&15));++alpha_test_vector_loads;
 __vector4 v;memcpy(&v,(const unsigned char*)base+offset,16);return v;
}
static __vector4 __vperm(__vector4 a,__vector4 b,__vector4 control) {
 __vector4 v={{0,0,0,0}};
 for(unsigned i=0;i<16;++i) {unsigned at=alpha_test_byte(control,i)&31;alpha_test_put(v,i,alpha_test_byte(at<16?a:b,at&15));}
 return v;
}
static __vector4 __vand(__vector4 a,__vector4 b) {for(unsigned i=0;i<4;++i)a.words[i]&=b.words[i];return a;}
static __vector4 __vor(__vector4 a,__vector4 b) {for(unsigned i=0;i<4;++i)a.words[i]|=b.words[i];return a;}
static __vector4 __vrlb(__vector4 a,__vector4 b) {
 __vector4 v={{0,0,0,0}};
 for(unsigned i=0;i<16;++i) {unsigned byte=alpha_test_byte(a,i),n=alpha_test_byte(b,i)&7;alpha_test_put(v,i,(byte<<n)|(byte>>((8-n)&7)));}
 return v;
}
static __vector4 __vsldoi(__vector4 a,__vector4 b,unsigned n) {
 __vector4 v={{0,0,0,0}};
 for(unsigned i=0;i<16;++i)alpha_test_put(v,i,alpha_test_byte(i+n<16?a:b,(i+n)&15));
 return v;
}
static void __stvewx(__vector4 value,void* base,int offset) {
 uintptr_t at=(uintptr_t)base+offset;assert(!(at&3));++alpha_test_vector_stores;
 *(uint32_t*)at=value.words[(at&15)/4]^(alpha_test_corrupt_store?1:0);
}
#endif
#define EPIC12_GPU_ALPHA_VECTOR_TEST
#endif
