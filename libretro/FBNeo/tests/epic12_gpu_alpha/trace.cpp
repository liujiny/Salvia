// Optional private gameplay trace replay. Traces are not distributed with ROMs.
#define EPIC12_GPU_ALPHA_PROFILE
#include "../../src/burn/devices/epic12_gpu_alpha.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>

struct Event {uint32_t key,slot;unsigned page;};
struct Metadata {uint32_t words[576];};
static std::vector<Event> events;
static std::vector<Metadata> metadata;
static Epic12GpuAlphaPage pages[256];
static uint32_t be32(const unsigned char* p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static bool read_trace(const char* path) {
    FILE* file=fopen(path,"rb");if(!file)return false;
    unsigned char header[8],data[2304];
    for(;;) {
        size_t got=fread(header,1,8,file);
        if(!got) {bool ok=feof(file)!=0;fclose(file);return ok;}
        if(got!=8) {fclose(file);return false;}
        Event e;e.key=be32(header);e.slot=be32(header+4);e.page=0;
        if(e.slot>=256) {fclose(file);return false;}
        if(e.key==~0U) {
            if(fread(data,1,sizeof(data),file)!=sizeof(data)) {fclose(file);return false;}
            Metadata m;for(int i=0;i<576;++i)m.words[i]=be32(data+4*i);
            e.page=(unsigned)metadata.size();metadata.push_back(m);
        } else if(e.key>>28 || (e.key&127)>((e.key>>14)&127) ||
                  ((e.key>>7)&127)>(e.key>>21)) {fclose(file);return false;}
        events.push_back(e);
    }
}

template<bool cached> static unsigned replay(std::vector<uint32_t>* reference,bool compare) {
    bool initialized[256]={false};unsigned checksum=0;size_t query=0;
    for(size_t i=0;i<events.size();++i) {
        const Event &e=events[i];Epic12GpuAlphaPage &page=pages[e.slot];
        if(e.key==~0U) {
            memcpy(page.rows,metadata[e.page].words,sizeof(page.rows));
            memcpy(page.groups,metadata[e.page].words+512,sizeof(page.groups));
            page.invalidate_cache();initialized[e.slot]=true;continue;
        }
        assert(initialized[e.slot]);
        int l=-1,t=-2,r=-3,b=-4;
        int x0=e.key&127,y0=(e.key>>7)&127,x1=(e.key>>14)&127,y1=(e.key>>21)&127;
        bool found=cached?page.bounds(x0,y0,x1,y1,l,t,r,b):page.scan_bounds(x0,y0,x1,y1,l,t,r,b);
        uint32_t result=found?((unsigned)l|((unsigned)t<<7)|((unsigned)r<<14)|((unsigned)b<<21)):~0U;
        if(!found)assert(l==-1&&t==-2&&r==-3&&b==-4);
        if(reference) {
            if(compare)assert(query<reference->size()&&(*reference)[query]==result);
            else reference->push_back(result);
        }
        checksum=checksum*33+result;++query;
    }
    if(compare&&reference)assert(query==reference->size());
    return checksum;
}

int main(int argc,char**argv) {
    if(argc!=2||!read_trace(argv[1])) {puts("Invalid trace (expected BE key/slot and 2304-byte metadata rebuild records)");return 1;}
    std::vector<uint32_t> reference;
    memset(&epic12_gpu_alpha_stats,0,sizeof(epic12_gpu_alpha_stats));
    unsigned sum=replay<false>(&reference,false);
    uint64_t oldWords=epic12_gpu_alpha_stats.summaryReads+epic12_gpu_alpha_stats.rowReads;
    memset(&epic12_gpu_alpha_stats,0,sizeof(epic12_gpu_alpha_stats));
    assert(replay<true>(&reference,true)==sum);
    const Epic12GpuAlphaStats stats=epic12_gpu_alpha_stats;
    printf("TRACE PASS pages=%lu queries=%lu hits=%llu misses=%llu old_words=%llu new_words=%llu checksum=%u\n",
        (unsigned long)metadata.size(),(unsigned long)reference.size(),
        (unsigned long long)stats.cacheHits,(unsigned long long)stats.cacheMisses,
        (unsigned long long)oldWords,(unsigned long long)(stats.summaryReads+stats.rowReads),sum);
    // Both benchmark paths execute identical trace decoding/checksum work.
    // Timings are host CPU measurements and cannot predict Xbox frame rate.
    const int repeats=8;unsigned check=0;clock_t begin=clock();
    for(int i=0;i<repeats;++i)check^=replay<false>(NULL,false);
    double oldMs=1000.0*(clock()-begin)/CLOCKS_PER_SEC;
    begin=clock();for(int i=0;i<repeats;++i)check^=replay<true>(NULL,false);
    double newMs=1000.0*(clock()-begin)/CLOCKS_PER_SEC;
    printf("TRACE CPU (host/QEMU only) repeats=%d uncached_ms=%.3f cached_ms=%.3f guard=%u\n",repeats,oldMs,newMs,check);
    return 0;
}
