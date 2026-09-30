// Work sampling is disjoint from frame timing. No target CPU or ROM required.
#include <stdio.h>
#include <string.h>
#include "../../src/burn/devices/cv1k_review_profile.h"
#include "../../src/cpu/sh4/sh3_drc_work_profile.h"
int main() {
 unsigned timing=0,work=0;
 for(unsigned x=0;x<65536;++x) {
  bool timed=(x&63u)==0;
  bool counted=salvia_review_work_sample(x,timed);
  if(timed && counted)return 1;
  if(timed)++timing;
  if(counted)++work;
  if(salvia_review_work_sample(x,true))return 2;
 }
 if(timing!=1024 || work!=256)return 3;
 SalviaReviewProfile<4> p={};
 unsigned sampled=0,collisions=0;
 for(unsigned i=0;i<1000000;++i) {
  bool timed=p.begin();
  bool counted=salvia_review_work_sample(p.rng,timed);
  sampled+=counted;collisions+=timed&&counted;
  if(i%2309==0)p.clear_window();
 }
 if(collisions || sampled<3000 || sampled>5000)return 4;
 memset(&sh3_drc_work,0xff,sizeof(sh3_drc_work));
 sh3_drc_work.clear();
 unsigned char *b=(unsigned char*)&sh3_drc_work;
 for(unsigned i=0;i<sizeof(sh3_drc_work);++i)if(b[i])return 5;
 sh3_drc_work.native_cycles=(Sh3WorkCount)0xffffffffu+1;
 if(sh3_drc_work.native_cycles!=((Sh3WorkCount)1<<32))return 6;
 printf("PASS disjoint selector: %u of 65536 workload values; million-frame samples=%u collisions=0\n",work,sampled);
 printf("PASS 64-bit counters, whole-profile reset and bounded static storage: %u bytes\n",(unsigned)sizeof(sh3_drc_work));
 puts("Scope: host sampling/counter semantics, not measured instrumentation overhead or FPS");
 return 0;
}
