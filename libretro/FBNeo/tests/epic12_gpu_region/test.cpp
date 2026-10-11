#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Execute the same vector branch on little-endian hosts and big-endian PPC.
// The partial stores model the SDK's store-unaligned pair, whose instructions
// are Xbox VMX128 extensions not provided by ordinary PowerPC emulators.
typedef unsigned int __vector4 __attribute__((vector_size(16)));
static unsigned long alignedStores,unalignedStores,prefetches;
static __vector4 __lvx(const void *p,int offset)
{
	const unsigned char *address=(const unsigned char*)p+offset;
	assert(((uintptr_t)address&15)==0);
	return *(const __vector4*)address;
}
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
	++unalignedStores;
}
// dcbt is only a hint. The stub records that the prefetch loop ran and that
// every address it asked for is inside the rectangle being copied.
static const unsigned char *liveBase;
static size_t liveBytes;
static unsigned long outOfRangePrefetches;
static void region_dcbt(int offset,const void *base)
{
	const unsigned char *address=(const unsigned char*)base+offset;
	if(address<liveBase || address>=liveBase+liveBytes) ++outOfRangePrefetches;
	++prefetches;
}
#define __dcbt region_dcbt
#define EPIC12_GPU_REGION_VECTOR_TEST
#include "../../src/burn/devices/epic12_gpu_region.h"
#undef __dcbt

static const unsigned char guard=0x5a;

// One flat pattern over the whole source allocation. Rows overlap when a
// caller asks for more bytes than its own source pitch holds, and a flat
// pattern is what a row copy reads in that case.
static unsigned char pattern(size_t i)
{
	return (unsigned char)(i*29+(i>>9)*11+3);
}

static unsigned long long bytesCopied;
static unsigned long cases;

// Byte-exact oracle: every byte of the destination allocation is compared
// against what the contract says, so a write outside [0,width) of a row, or a
// read of a source row that was never requested, is a failure rather than a
// rounding difference.
static void run(int width,int rows,size_t srcPitch,size_t dstPitch,int srcOffset,int dstOffset)
{
	// Overlapping destination rows have no well-defined result. The compositor
	// never asks for one; the row copy is only specified for a rectangle.
	if((size_t)width>dstPitch) return;
	const size_t dstRow=dstPitch;
	const size_t srcBytes=(size_t)rows*srcPitch+(size_t)width+64;
	const size_t dstBytes=(size_t)rows*dstRow+64;
	unsigned char *srcAllocation=(unsigned char*)malloc(srcBytes);
	unsigned char *dstAllocation=(unsigned char*)malloc(dstBytes);
	assert(srcAllocation && dstAllocation);
	for(size_t i=0;i<srcBytes;++i) srcAllocation[i]=pattern(i);
	memset(dstAllocation,guard,dstBytes);
	unsigned char *src=srcAllocation+srcOffset;
	unsigned char *dst=dstAllocation+dstOffset;
	liveBase=src;
	liveBytes=rows?(size_t)(rows-1)*srcPitch+(size_t)width:0;
	epic12_gpu_copy_rows(dst,dstPitch,src,srcPitch,(size_t)width,(size_t)rows);
	for(size_t i=0;i<dstBytes;++i) {
		const long relative=(long)i-(long)dstOffset;
		const size_t row=relative<0?0:(size_t)relative/dstPitch;
		const size_t x=relative<0?0:(size_t)relative%dstPitch;
		unsigned char expected=guard;
		if(relative>=0 && row<(size_t)rows && x<(size_t)width)
			expected=pattern((size_t)srcOffset+row*srcPitch+x);
		if(dstAllocation[i]!=expected) {
			fprintf(stderr,"FAIL case %lu %dx%d srcPitch %lu dstPitch %lu offsets %d/%d byte %lu got %02x expected %02x\n",
				cases,width,rows,(unsigned long)srcPitch,(unsigned long)dstPitch,srcOffset,dstOffset,
				(unsigned long)i,dstAllocation[i],expected);
			abort();
		}
	}
	for(size_t i=0;i<srcBytes;++i) {
		if(srcAllocation[i]!=pattern(i)) {
			fprintf(stderr,"FAIL case %lu source modified at %lu\n",cases,(unsigned long)i);
			abort();
		}
	}
	bytesCopied+=(unsigned long long)width*rows;
	free(srcAllocation); free(dstAllocation);
	++cases;
}

int main()
{
	assert(sizeof(unsigned int)==4);
	const int widths[]={0,1,2,3,4,5,6,7,8,12,15,16,17,20,31,32,33,63,64,65,127,128,129,
		255,256,257,511,512,513,1023,1024,2047,2048};
	const int rowsList[]={0,1,2,3,4,5,6,7,8,9,16,33};
	const size_t pitches[][2]={{32768,2048},{32768,2048},{2048,32768},{256,512},{48,16},
		{200,205},{2049,2048},{32768,240}};
	const int offsets[]={0,4,8,12};
	// Every width against every row count at the real transfer shape, both
	// when the two rows share an alignment and when they do not.
	for(unsigned w=0;w<sizeof(widths)/sizeof(widths[0]);++w)
		for(unsigned r=0;r<sizeof(rowsList)/sizeof(rowsList[0]);++r)
			for(unsigned o=0;o<sizeof(offsets)/sizeof(offsets[0]);++o)
				run(widths[w],rowsList[r],32768,2048,offsets[o],offsets[o]);
	for(unsigned w=0;w<sizeof(widths)/sizeof(widths[0]);++w)
		for(unsigned r=0;r<sizeof(rowsList)/sizeof(rowsList[0]);++r)
			for(unsigned o=0;o<sizeof(offsets)/sizeof(offsets[0]);++o)
				run(widths[w],rowsList[r],32768,2048,offsets[o],offsets[(o+1)&3]);
	// Every pitch pair, including the ones whose pitch is not a multiple of 16
	// and must take the scalar fallback.
	for(unsigned p=0;p<sizeof(pitches)/sizeof(pitches[0]);++p)
		for(unsigned w=0;w<sizeof(widths)/sizeof(widths[0]);++w)
			for(unsigned o=0;o<sizeof(offsets)/sizeof(offsets[0]);++o)
				run(widths[w],5,pitches[p][0],pitches[p][1],offsets[o],offsets[(o+3)&3]);
	// The two rectangles the compositor actually copies: the batch bounding
	// box into the 512x512 input texture, and a batch whose box is one screen.
	run(2048,512,32768,2048,0,0);
	run(2048,512,32768,2048,12,4);
	run(1024,240,32768,2048,4,0);
	run(4,512,32768,2048,0,4);
	if(!alignedStores || !unalignedStores) { fprintf(stderr,"FAIL: one store form never ran\n"); return 1; }
	if(!prefetches) { fprintf(stderr,"FAIL: no prefetch hints\n"); return 1; }
	if(outOfRangePrefetches) { fprintf(stderr,"FAIL: %lu prefetch hints outside the rectangle\n",outOfRangePrefetches); return 1; }
	const unsigned endianProbe=1;
	printf("PASS: %lu rectangles; %llu exact bytes; %lu aligned / %lu unaligned vector stores; "
		"%lu prefetch hints, all inside the rectangle; %s-endian\n",
		cases,bytesCopied,alignedStores,unalignedStores,prefetches,
		*(const unsigned char*)&endianProbe?"little":"big");
	return 0;
}
