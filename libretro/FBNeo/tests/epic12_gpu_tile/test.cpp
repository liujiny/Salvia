#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int __vector4 __attribute__((vector_size(16)));
static unsigned char *atlasStart;
static size_t atlasBytes;
static int expectedSlot;
static unsigned vectorWrites,alignedLoads,unalignedLoads;

static __vector4 __lvx(const void *base,int offset)
{
	const unsigned char *p=(const unsigned char*)base+offset;
	assert(((uintptr_t)p&15)==0); ++alignedLoads;
	return *(const __vector4*)p;
}
static __vector4 __loadunalignedvector(const void *source)
{
	__vector4 value; memcpy(&value,source,16); ++unalignedLoads; return value;
}
static void __stvx_volatile(__vector4 value,volatile void *base,int offset)
{
	volatile unsigned char *p=(volatile unsigned char*)base+offset;
	assert(((uintptr_t)p&15)==0);
	assert((uintptr_t)p>=(uintptr_t)atlasStart);
	size_t address=(uintptr_t)p-(uintptr_t)atlasStart;
	assert(address+16<=atlasBytes);
	unsigned tile=(unsigned)(address/4096);
	unsigned owner=(tile/64/4)*16+(tile%64/4);
	assert(owner==(unsigned)expectedSlot); // no write outside the assigned page
	assert(address%4096==(vectorWrites%256)*16); // sequential, complete blocks
	*(volatile __vector4*)p=value; ++vectorWrites;
}
#define EPIC12_GPU_TILE_VECTOR_TEST
#include "../../src/burn/devices/epic12_gpu_tile.h"

// Independent full-surface address expression. The production table is an
// inverse 32x32 permutation; this reference computes each texel directly.
static unsigned reference(unsigned x,unsigned y,unsigned width)
{
	unsigned macro=((x/32)+(y/32)*(width/32))*512;
	unsigned micro=((x%8)+(y&6)*4)*4;
	unsigned offset=macro+(micro/16)*32+(micro%16)+(y&8)*32+(y&1)*16;
	return (offset/512)*4096+(offset&448)*4+(offset&63)+(y&16)*128+
		((((y&8)/4)+(x/8))%4)*64;
}
static unsigned pattern(unsigned x,unsigned y,unsigned slot,unsigned pass)
{
	return (x*0x1f3d5b79U)^(y*0x9e3779b9U)^(slot*0x71da89c3U)^(pass*0xa73ec451U)^0xabcdef01U;
}

int main()
{
	const unsigned char guard=0x5a;
	const int pitches[]={8192,132,133,8195};
	unsigned pages=0; unsigned long long pixels=0;
	for(int height=1024;height<=2048;height*=2) for(unsigned pass=0;pass<4;++pass) {
		atlasBytes=(size_t)2048*height*4;
		unsigned char *atlasAllocation=(unsigned char*)malloc(atlasBytes+256+127);
		assert(atlasAllocation);
		unsigned char *aligned=(unsigned char*)(((uintptr_t)atlasAllocation+127)&~(uintptr_t)127);
		atlasStart=aligned+128;
		memset(aligned,guard,atlasBytes+256);
		int pitch=pitches[pass];
		unsigned char *sourceAllocation=(unsigned char*)malloc((size_t)pitch*128*4+256+127);
		assert(sourceAllocation);
		unsigned *source=(unsigned*)((((uintptr_t)sourceAllocation+127)&~(uintptr_t)127)+128)+pass;
		for(int slot=0;slot<height/8;++slot) {
			for(unsigned y=0;y<128;++y) for(unsigned x=0;x<128;++x)
				source[y*pitch+x]=pattern(x,y,slot,pass);
			expectedSlot=slot;
			unsigned before=vectorWrites;
			epic12_gpu_tile_page(atlasStart,source,slot,pitch);
			assert(vectorWrites-before==4096);
			unsigned ax=(slot%16)*128,ay=(slot/16)*128;
			for(unsigned y=0;y<128;++y) for(unsigned x=0;x<128;++x) {
				unsigned offset=reference(ax+x,ay+y,2048);
				unsigned actual=*(unsigned*)(atlasStart+offset);
				assert(actual==pattern(x,y,slot,pass));
				assert(source[y*pitch+x]==pattern(x,y,slot,pass));
			}
			for(int i=0;i<128;++i)
				assert(aligned[i]==guard && atlasStart[atlasBytes+i]==guard);
			++pages; pixels+=128*128;
		}
		// Later pages must also leave every previously uploaded page intact.
		for(unsigned y=0;y<(unsigned)height;++y) for(unsigned x=0;x<2048;++x) {
			unsigned slot=(y/128)*16+x/128;
			assert(*(unsigned*)(atlasStart+reference(x,y,2048))==pattern(x%128,y%128,slot,pass));
		}
		free(sourceAllocation); free(atlasAllocation);
	}
	unsigned endian=1;
	printf("PASS: %u pages, %llu pixels; all 128/256 slots, guards, cached source alignments/pitches; %s-endian\n",
		pages,pixels,*(unsigned char*)&endian?"little":"big");
	printf("PASS: %u sequential aligned vector stores; %u aligned/%u unaligned source loads\n",
		vectorWrites,alignedLoads,unalignedLoads);
	return 0;
}
