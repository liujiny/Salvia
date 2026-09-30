#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Execute the same vector branch on little-endian hosts and big-endian PPC.
// The partial stores model the SDK's store-unaligned pair, whose instructions
// are Xbox VMX128 extensions not provided by ordinary PowerPC emulators.
typedef unsigned int __vector4 __attribute__((vector_size(16)));
static unsigned long alignedStores,partialStores;
static __vector4 __lvx(const void *p,int offset)
{
	const unsigned char *address=(const unsigned char*)p+offset;
	assert(((uintptr_t)address&15)==0);
	return *(const __vector4*)address;
}
static __vector4 __vand(__vector4 a,__vector4 b) { return a&b; }
static void __stvx(__vector4 value,void *p,int offset)
{
	unsigned char *address=(unsigned char*)p+offset;
	assert(((uintptr_t)address&15)==0);
	*(__vector4*)address=value;
	++alignedStores;
}
static void __storeunalignedvector(__vector4 value,void *p)
{
	unsigned char *address=(unsigned char*)p;
	const unsigned char *bytes=(const unsigned char*)&value;
	unsigned left=16-((uintptr_t)address&15);
	memcpy(address,bytes,left);
	memcpy(address+left,bytes+left,16-left);
	++partialStores;
}
#define EPIC12_GPU_UNTILE_VECTOR_TEST
#include "../../src/burn/devices/epic12_gpu_untile.h"

// Independent full-surface reference, in bytes. This keeps macro addressing
// together with the bank permutation instead of using the helper's tables.
static unsigned reference(unsigned x,unsigned y)
{
	const unsigned macro=((x/32)+(y/32)*16)*512;
	const unsigned micro=((x%8)+(y&6)*4)*4;
	const unsigned offset=macro+(micro/16)*32+(micro%16)+(y&8)*32+(y&1)*16;
	return (offset/512)*4096+((offset&448)*4)+(offset&63)+(y&16)*128+
		((((y&8)/4)+(x/8))%4)*64;
}
static unsigned pixel(unsigned x,unsigned y)
{
	return (x*0x1f3d5b79U)^(y*0x9e3779b9U)^0xabcdef01U;
}
static unsigned *source;
static unsigned cases;
static unsigned long long pixels;

static void run(int width,int height,int dx,int dy,int pitch,int allocationOffset)
{
	const unsigned guard=0xc35a7d91;
	int words=pitch*(height+dy+1)+16+allocationOffset;
	unsigned *base=(unsigned*)malloc(words*4);
	assert(base);
	for(int i=0;i<words;++i) base[i]=guard;
	unsigned *destination=base+4+allocationOffset;
	epic12_gpu_untile_mask(source,destination,dx,dy,width,height,pitch);
	for(int i=0;i<words;++i) {
		const int relative=i-4-allocationOffset;
		const int y=relative/pitch-dy,x=relative%pitch-dx;
		unsigned expected=guard;
		if(relative>=0 && x>=0 && x<width && y>=0 && y<height)
			expected=pixel(x,y)&0x20f8f8f8;
		if(base[i]!=expected) {
			fprintf(stderr,"FAIL case %u %dx%d dst %d,%d pitch %d allocationOffset %d index %d got %08x expected %08x\n",
				cases,width,height,dx,dy,pitch,allocationOffset,i,base[i],expected);
			abort();
		}
	}
	free(base); ++cases; pixels+=(unsigned long long)width*height;
}

int main()
{
	assert(sizeof(unsigned int)==4);
	unsigned char *allocation=(unsigned char*)malloc(512*512*4+15);
	assert(allocation);
	source=(unsigned*)(((uintptr_t)allocation+15)&~(uintptr_t)15);
	epic12_gpu_untile_init();
	for(unsigned y=0;y<512;++y) for(unsigned x=0;x<512;++x) {
		unsigned byteOffset=reference(x,y);
		assert(byteOffset<512*512*4);
		assert(byteOffset==epic12_untile_blocks[y/32][x/32]+epic12_untile_offsets[y%32][(x%32)/4]+(x%4)*4);
		source[byteOffset/4]=pixel(x,y);
	}
	puts("PASS: 262144 source addresses, all macro blocks and four-pixel contiguous groups");
	for(int i=0;i<=512;++i) for(int a=0;a<4;++a) {
		run(i,(i*7)%35,a,1,520,a);
		run((i*13)%35,i,a,2,519,a);
	}
	const int edge[]={1,3,4,5,7,8,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,319,320,321,511,512};
	for(unsigned h=0;h<sizeof(edge)/sizeof(edge[0]);++h)
		for(unsigned w=0;w<sizeof(edge)/sizeof(edge[0]);++w)
			for(int a=0;a<4;++a) run(edge[w],edge[h],a,a&1,520+(a&1),a);
	// Exercise the real VRAM pitch, clipping near its right boundary, and
	// nonzero row origins without allocating the complete 128MB VRAM image.
	for(int a=0;a<4;++a) {
		run(512,512,a,1,8192,0);
		run(511,33,8192-511-a,3,8192,a);
		run(1,1,8191,31,8192,a);
	}
	const unsigned endianProbe=1;
	printf("PASS: %u rectangles; %llu exact pixels; %lu aligned / %lu unaligned vector stores; %s-endian\n",
		cases,pixels,alignedStores,partialStores,*(const unsigned char*)&endianProbe?"little":"big");
	free(allocation);
	return 0;
}
