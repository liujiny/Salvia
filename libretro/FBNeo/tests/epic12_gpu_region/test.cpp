#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Execute the same vector branch on little-endian hosts and big-endian PPC.
//
// The stubs do not only model the SDK intrinsics, they assert the rules those
// intrinsics depend on. The destination is a locked texture, which is
// write-combined memory on Xenon, and an unaligned vector store there is not a
// rounding difference but the crash that took the console to the dashboard on
// 2026-10-11 (see epic12_gpu_tile.h: "Never read it or use unaligned stores",
// and docs/optimization/2026-10-11-cv1k-blit-rowcopy.md). So the unaligned
// store stub fails the test if the copy ever reaches it, and every aligned
// store is checked to sit on a 16-byte boundary inside the destination
// rectangle.
typedef unsigned int __vector4 __attribute__((vector_size(16)));

static unsigned long cases,alignedStores,unalignedLoads,prefetches;
static unsigned long outOfRangeLoads,outOfRangeStores,outOfRangePrefetches;
// The rectangle being copied, so a stub can prove that a load or a store stays
// inside it. Source and destination are checked separately.
static const unsigned char *liveBase;
static size_t liveBytes;
static const unsigned char *liveOutBase;
static size_t liveOutBytes;

static __vector4 __loadunalignedvector(const unsigned int *p)
{
	const unsigned char *address=(const unsigned char*)p;
	// The unaligned pair is defined for ordinary cached memory, and its address
	// still has to be word aligned for the intrinsic.
	assert(((uintptr_t)address&3)==0);
	if(address<liveBase || address+16>liveBase+liveBytes) ++outOfRangeLoads;
	++unalignedLoads;
	__vector4 value;
	memcpy(&value,address,16);
	return value;
}
static void __stvx_volatile(__vector4 value,void *p,int offset)
{
	unsigned char *address=(unsigned char*)p+offset;
	assert(((uintptr_t)address&15)==0);
	if(address<liveOutBase || address+16>liveOutBase+liveOutBytes) ++outOfRangeStores;
	*(__vector4*)address=value;
	++alignedStores;
}
static void __storeunalignedvector(__vector4 value,void *p)
{
	(void)value; (void)p;
	fprintf(stderr,"FAIL case %lu: a vector store landed off a 16-byte boundary\n",cases);
	abort();
}
// dcbt is only a hint. The stub records that the prefetch loop ran and that
// every address it asked for is inside the rectangle being copied.
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

// malloc() is only guaranteed to be aligned for any fundamental type, so the
// allocation is aligned by hand: a destination the copy can treat as vector
// aligned is the case that matters.
static unsigned char *allocAligned(size_t bytes,unsigned char **rawOut)
{
	unsigned char *raw=(unsigned char*)malloc(bytes+32);
	assert(raw);
	*rawOut=raw;
	return raw+((16-((uintptr_t)raw&15))&15);
}

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
	unsigned char *srcRaw,*dstRaw;
	unsigned char *srcAllocation=allocAligned(srcBytes,&srcRaw);
	unsigned char *dstAllocation=allocAligned(dstBytes,&dstRaw);
	for(size_t i=0;i<srcBytes;++i) srcAllocation[i]=pattern(i);
	memset(dstAllocation,guard,dstBytes);
	unsigned char *src=srcAllocation+srcOffset;
	unsigned char *dst=dstAllocation+dstOffset;
	liveBase=src;
	liveBytes=rows?(size_t)(rows-1)*srcPitch+(size_t)width:0;
	liveOutBase=dst;
	liveOutBytes=rows?(size_t)(rows-1)*dstPitch+(size_t)width:0;
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
	free(srcRaw); free(dstRaw);
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
	// Every offset is a multiple of 4, which is what a VRAM word address can
	// be; 4, 8 and 12 are the alignments that force the two sides apart and
	// send the source through the unaligned load.
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
	if(!alignedStores) { fprintf(stderr,"FAIL: no aligned vector store ran\n"); return 1; }
	if(!unalignedLoads) { fprintf(stderr,"FAIL: no unaligned vector load ran\n"); return 1; }
	if(!prefetches) { fprintf(stderr,"FAIL: no prefetch hints\n"); return 1; }
	if(outOfRangeLoads) { fprintf(stderr,"FAIL: %lu vector loads outside the source rectangle\n",outOfRangeLoads); return 1; }
	if(outOfRangeStores) { fprintf(stderr,"FAIL: %lu vector stores outside the destination rectangle\n",outOfRangeStores); return 1; }
	if(outOfRangePrefetches) { fprintf(stderr,"FAIL: %lu prefetch hints outside the rectangle\n",outOfRangePrefetches); return 1; }
	const unsigned endianProbe=1;
	printf("PASS: %lu rectangles; %llu exact bytes; %lu vector stores, every one 16-byte aligned and inside its row; "
		"0 unaligned stores; %lu unaligned vector loads; %lu prefetch hints, all inside the rectangle; %s-endian\n",
		cases,bytesCopied,alignedStores,unalignedLoads,prefetches,
		*(const unsigned char*)&endianProbe?"little":"big");
	return 0;
}
