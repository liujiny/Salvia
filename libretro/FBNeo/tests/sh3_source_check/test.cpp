// Exact source validator vs independent libc equality. No ROMs or PPC needed.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef uint16_t UINT16;
#include "../../src/cpu/sh4/sh3_drc_source_check.h"

static unsigned rng=0x931ea50d, cases;
static unsigned random32() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static void check(const UINT16* a,const UINT16* b,unsigned words) {
 bool want=words==0 || memcmp(a,b,words*sizeof(UINT16))==0;
 if(sh3_drc_source_equal(a,b,words)!=want) {
  fprintf(stderr,"FAIL words=%u case=%u\n",words,cases); exit(1);
 }
 ++cases;
}
static void lengths_and_mutations() {
 for(unsigned n=0;n<=33;++n)for(unsigned oa=0;oa<4;++oa)for(unsigned ob=0;ob<4;++ob) {
  // Exact allocation ends let ASan detect any tail overread. Start offsets
  // exercise every halfword alignment within an eight-byte boundary.
  UINT16* rawA=(UINT16*)malloc((n+oa+1)*sizeof(UINT16));
  UINT16* rawB=(UINT16*)malloc((n+ob+1)*sizeof(UINT16));
  if(!rawA||!rawB)exit(2);
  UINT16 *a=rawA+oa+1,*b=rawB+ob+1;
  for(unsigned i=0;i<n;++i)a[i]=b[i]=(UINT16)random32();
  check(a,b,n);
  for(unsigned i=0;i<n;++i)for(unsigned bit=0;bit<16;++bit) {
   b[i]^=(UINT16)(1u<<bit); check(a,b,n); b[i]^=(UINT16)(1u<<bit);
  }
  check(a,b,n);
  free(rawA);free(rawB);
 }
 // Data beyond the validated range must not invalidate an otherwise equal block.
 UINT16 a[40],b[40];
 for(unsigned n=0;n<=33;++n) {
  memset(a,0,sizeof(a));memset(b,0,sizeof(b));
  for(unsigned i=n;i<40;++i)b[i]=0xffff;
  check(a,b,n);
 }
 check(NULL,NULL,0);
}
static void randomized() {
 UINT16 a[40],b[40];
 for(unsigned i=0;i<200000;++i) {
  unsigned n=random32()%34,oa=random32()%4,ob=random32()%4;
  for(unsigned j=0;j<40;++j)a[j]=b[j]=(UINT16)random32();
  for(unsigned j=0;j<n;++j)b[ob+j]=a[oa+j];
  if(n && (random32()&1))b[ob+random32()%n]^=(UINT16)(1u<<(random32()%16));
  check(a+oa,b+ob,n);
 }
}
// Two equal bit flips must not cancel in an equality predicate. Exercise
// every pair of positions and halfword alignment, including group boundaries.
static void paired_mutations() {
 UINT16 storageA[40],storageB[40];
 for(unsigned n=1;n<=33;++n)for(unsigned oa=0;oa<4;++oa)for(unsigned ob=0;ob<4;++ob) {
  UINT16 *a=storageA+oa,*b=storageB+ob;
  for(unsigned k=0;k<n;++k)a[k]=b[k]=(UINT16)random32();
  check(a,a,n);
  for(unsigned i=0;i<n;++i)for(unsigned j=i+1;j<n;++j) {
   UINT16 mask=(UINT16)(1u<<((i+j)&15));
   b[i]^=mask;b[j]^=mask;check(a,b,n);b[i]^=mask;b[j]^=mask;
  }
  check(a,b,n);
 }
 puts("PASS paired equal-bit mutations, group boundaries and same-pointer equality");
}
static void page_boundaries() {
#if defined(__unix__)
 long page=sysconf(_SC_PAGESIZE); if(page<=0)exit(3);
 unsigned char *pa=(unsigned char*)mmap(NULL,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 unsigned char *pb=(unsigned char*)mmap(NULL,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(pa==MAP_FAILED||pb==MAP_FAILED)exit(4);
 if(mprotect(pa+page,page,PROT_READ|PROT_WRITE)||mprotect(pb+page,page,PROT_READ|PROT_WRITE))exit(5);
 for(unsigned n=0;n<=33;++n)for(unsigned begin=0;begin<2;++begin) {
  UINT16* a=(UINT16*)(begin?pa+page:pa+2*page-2*n);
  UINT16* b=(UINT16*)(begin?pb+page:pb+2*page-2*n);
  for(unsigned i=0;i<n;++i)a[i]=b[i]=(UINT16)random32();
  check(a,b,n);
  for(unsigned i=0;i<n;++i) { b[i]^=0x0101;check(a,b,n);b[i]^=0x0101; }
 }
 munmap(pa,3*page);munmap(pb,3*page);
 puts("PASS guard pages: 0..33 opcodes at start/end of mapped page, no overread");
#endif
}
int main() {
 lengths_and_mutations();randomized();paired_mutations();page_boundaries();
 printf("PASS exact source equality: %u cases, all 0..33 lengths, every bit mutation, halfword alignments and tails\n",cases);
 puts("Scope: host helper correctness; not PPC execution or console FPS");
 return 0;
}
