#include "../../src/burn/devices/epic12_staging_columns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned seed=0x98bd1342;
static unsigned random_word(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void command(uint16_t *p,int x,int y,int sx,int sy,int h) {
 memset(p,0,20);p[0]=0x1000;p[2]=sx;p[3]=sy;p[4]=x;p[5]=y;p[6]=1;p[7]=h-1;p[8]=0x80;p[9]=0x8080;
}
int main() {
 uint16_t ram[2048];Epic12StagingColumns g;
 const unsigned mask=sizeof(ram)-1,words=8192*512;
 uint32_t *ref=(uint32_t*)malloc(words*4),*got=(uint32_t*)malloc(words*4);
 CHECK(ref && got);
 for(unsigned i=0;i<words;++i) ref[i]=random_word();
 memcpy(got,ref,words*4);
 unsigned cases=0;
 for(int test=0;test<512;++test) {
  memset(ram,0,sizeof(ram));unsigned count=4+random_word()%125,h=8+random_word()%128,x=768+random_word()%128,y=1+random_word()%16;
  unsigned next=x;
  for(unsigned i=0;i<count;++i){command(ram+i*10,next,y,1536+(random_word()%64),random_word()%128,h);++next;}
  CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==count);
  unsigned width=ram[(count-1)*10+4]-x+2;
  for(unsigned row=y-1;row<=y+h;++row)for(unsigned col=x-1;col<=x+width;++col)ref[row*8192+col]=got[row*8192+col]=random_word();
  for(unsigned i=0;i<count;++i)for(unsigned row=0;row<h;++row){unsigned src=(ram[i*10+3]+row)*8192+ram[i*10+2],dst=(y+row)*8192+ram[i*10+4];ref[dst]=ref[src];ref[dst+1]=ref[src+1];}
  epic12_staging_draw_columns(got,y*8192+x,g);
  for(unsigned row=y-1;row<=y+h;++row)for(unsigned col=x-1;col<=x+width;++col)CHECK(ref[row*8192+col]==got[row*8192+col]);
  ++cases;
 }
 // Admission barriers: flags, tint, clipping, source/destination overlap,
 // row wrapping, command boundaries and the bounded 128-command split.
 for(int i=0;i<129;++i)command(ram+i*10,768+i,10,1536+i,200,16);
 memset(ram+1290,0,20);
 CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==128);
 CHECK(epic12_staging_collect_columns(ram,mask,1,0,0,8191,4095,g)==0);
 CHECK(epic12_staging_collect_columns(ram,~0u,0,0,0,8191,4095,g)==0);
 CHECK(epic12_staging_collect_columns(ram,mask,0,769,0,8191,4095,g)==0);
 CHECK(epic12_staging_collect_columns(ram,mask,0,0,11,8191,4095,g)==0);
 uint16_t original[10];memcpy(original,ram,20);
 const unsigned flags[]={0x100,0x200,0x400,0x800};
 for(unsigned i=0;i<4;++i){ram[0]=original[0]|flags[i];CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==0);}
 memcpy(ram,original,20);ram[8]=0xfc;CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==0);
 memcpy(ram,original,20);ram[2]=8191;CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==0);
 memcpy(ram,original,20);ram[3]=4090;CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==0);
 memcpy(ram,original,20);
 for(int i=0;i<4;++i)command(ram+i*10,768+i,10,768+i,10,16);
 ram[40]=0;CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==0);
 memset(ram,0,sizeof(ram));for(int i=0;i<3;++i)command(ram+i*10,768+i,10,1536+i,200,16);
 CHECK(epic12_staging_collect_columns(ram,mask,0,0,0,8191,4095,g)==0);
 // A command ending at the RAM boundary is legal; a partial one is not.
 memset(ram,0,sizeof(ram));for(int i=0;i<4;++i)command(ram+512-40+i*10,768+i,10,1536+i,200,16);
 CHECK(epic12_staging_collect_columns(ram,1023,1024-80,0,0,8191,4095,g)==4);
 CHECK(epic12_staging_collect_columns(ram,1023,1024-18,0,0,8191,4095,g)==0);
 free(ref);free(got);printf("PASS %u randomized column/row-major pixel and guard comparisons; admission, aliases, overlap writes, RAM bounds and capacity\n",cases);return 0;
}
