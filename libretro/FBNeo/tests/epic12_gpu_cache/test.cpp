#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/burn/devices/epic12_gpu_cache.h"

static unsigned randomState=0x53d19a27;
static unsigned random_word()
{
	randomState^=randomState<<13; randomState^=randomState>>17;
	randomState^=randomState<<5; return randomState;
}

// The reference uses a nonwrapping 64-bit access serial and individual page
// membership for invalidation. It does not share the production clock logic.
struct Reference {
	int capacity,page[256];
	uint64_t age[256],serial;
	bool pending[256];
	Reference(int count) : capacity(count),serial(0) { clear(); }
	void clear() { for(int i=0;i<capacity;++i) {page[i]=-1;age[i]=0;pending[i]=false;} }
	unsigned begin(const bool *needed,int *slots)
	{
		++serial; unsigned hits=0;
		for(int i=0;i<2048;++i) slots[i]=-1;
		for(int i=0;i<capacity;++i) {
			pending[i]=page[i]>=0 && needed[page[i]];
			if(pending[i]) {slots[page[i]]=i;age[i]=serial;++hits;}
		}
		return hits;
	}
	int allocate(int p)
	{
		if(p<0 || p>=2048) return -1;
		for(int i=0;i<capacity;++i) if(page[i]==p) {pending[i]=true;age[i]=serial;return i;}
		int chosen=-1;
		for(int i=0;i<capacity;++i) if(!pending[i] && page[i]<0) {chosen=i;break;}
		if(chosen<0) for(int i=0;i<capacity;++i)
			if(!pending[i] && (chosen<0 || age[i]<age[chosen])) chosen=i;
		if(chosen>=0) {page[chosen]=p;pending[chosen]=true;age[chosen]=serial;}
		return chosen;
	}
	void invalidate(int x0,int y0,int x1,int y1)
	{
		if(x1<x0 || y1<y0 || x1<0 || y1<0 || x0>8191 || y0>4095) return;
		if(x0<0)x0=0; if(y0<0)y0=0; if(x1>8191)x1=8191; if(y1>4095)y1=4095;
		for(int y=y0/128;y<=y1/128;++y) for(int x=x0/128;x<=x1/128;++x)
			for(int i=0;i<capacity;++i) if(page[i]==y*64+x) {page[i]=-1;age[i]=0;pending[i]=false;}
	}
};

static bool needed[2048];
static int actualSlots[2048],expectedSlots[2048];
static unsigned batches,allocations,evictions,wraps;
static uint64_t fingerprint=1469598103934665603ULL;

static void same(const Epic12GpuPageCache &cache,const Reference &reference)
{
	for(int i=0;i<cache.capacity;++i) {
		assert(cache.pages[i]==reference.page[i]);
		assert(cache.pinned[i]==reference.pending[i]);
		for(int j=i+1;j<cache.capacity;++j)
			assert(cache.pages[i]<0 || cache.pages[i]!=cache.pages[j]);
	}
}

static void boundary_cases()
{
	Epic12GpuPageCache cache;
	cache.reset(3); memset(needed,0,sizeof(needed));
	cache.begin_batch(needed,actualSlots);
	assert(cache.allocate(0)==0); assert(cache.allocate(1)==1);
	// All hits must be pinned before an earlier-numbered missing page is allocated.
	needed[1]=true; cache.begin_batch(needed,actualSlots);
	assert(actualSlots[1]==1);
	assert(cache.allocate(2)==2); // empty slot wins over the older valid slot 0
	assert(cache.allocate(3)==0);
	assert(cache.allocate(4)==-1); // all three slots are now pending
	assert(cache.allocate(1)==1); // repeated allocation cannot duplicate a page
	assert(cache.allocate(-1)==-1 && cache.allocate(2048)==-1);
	cache.invalidate(127,0,127,0); // only page 0, absent
	assert(cache.pages[1]==1);
	cache.invalidate(128,0,128,0); // first pixel in page 1
	assert(cache.pages[1]==-1 && !cache.pinned[1]);
	assert(cache.allocate(63)==1);
	cache.invalidate(8192,0,8192,4095); // wholly outside VRAM
	assert(cache.pages[1]==63);
	cache.invalidate(8191,127,8191,127);
	assert(cache.pages[1]==-1);
	cache.clear(); assert(cache.capacity==3 && cache.clock==0);
	for(int i=0;i<256;++i) assert(cache.pages[i]==-1);
	cache.reset(256); cache.begin_batch(needed,actualSlots);
	assert(cache.allocate(2047)==0);
	cache.invalidate(8191,4095,8191,4095);
	assert(cache.pages[0]==-1);
	cache.reset(128); assert(cache.capacity==128);
	puts("PASS: empty-first, pending pins, full atlas, duplicates, page-edge invalidation, reset");
}

static void randomized(int capacity)
{
	Epic12GpuPageCache cache; cache.reset(capacity); Reference reference(capacity);
	for(int iteration=0;iteration<1500;++iteration) {
		if(iteration%113==0) {
			// Jump ahead in time; relative age ordering of untouched entries
			// is unchanged. Subsequent batches exercise actual wrap handling.
			cache.clock=UINT_MAX-2; ++wraps;
		}
		memset(needed,0,sizeof(needed));
		int count=(int)(random_word()%(capacity+3));
		for(int n=0;n<count;++n) {
			unsigned page=random_word()%2048;
			if((random_word()&3)!=0) {
				int existing=cache.pages[random_word()%capacity];
				if(existing>=0) page=(unsigned)existing;
			}
			needed[page]=true;
		}
		assert(cache.begin_batch(needed,actualSlots)==reference.begin(needed,expectedSlots));
		assert(memcmp(actualSlots,expectedSlots,sizeof(actualSlots))==0);
		same(cache,reference);
		for(int page=0;page<2048;++page) if(needed[page] && actualSlots[page]<0) {
			int slot=cache.allocate(page),expected=reference.allocate(page);
			assert(slot==expected);
			if(slot>=0) {actualSlots[page]=slot;++allocations;}
			fingerprint^=(unsigned)(slot+1); fingerprint*=1099511628211ULL;
		}
		same(cache,reference);
		if(iteration%5==0) {
			int x=(int)(random_word()%8448)-128,y=(int)(random_word()%4352)-128;
			int x1=x+(int)(random_word()%512),y1=y+(int)(random_word()%256);
			cache.invalidate(x,y,x1,y1); reference.invalidate(x,y,x1,y1);
			same(cache,reference); ++evictions;
		}
		if(iteration%197==0) {cache.clear();reference.clear();same(cache,reference);}
		++batches;
	}
}

int main()
{
	boundary_cases();
	const int capacities[]={1,2,3,4,7,16,128,256};
	for(unsigned c=0;c<sizeof(capacities)/sizeof(capacities[0]);++c) randomized(capacities[c]);
	printf("PASS: %u batches, %u allocations, %u invalidations, %u clock-wrap setups; fingerprint=%016llx\n",
		batches,allocations,evictions,wraps,(unsigned long long)fingerprint);
	return 0;
}
