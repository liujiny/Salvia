#define EPIC12_GPU_ALPHA_PROFILE
#include "../../src/burn/devices/epic12_gpu_alpha.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

struct Command { int sx,sy,x,y,w,h,flipx,flipy,transparent,blend,sa,da; uint32_t tint; };
struct Bounds {int min_x,max_x,min_y,max_y;};
static uint32_t source[12][128*128];
static Epic12GpuAlphaPage alpha[16];
static int slots[2048];
static uint32_t randomState=0x53d6a109;
static unsigned random_word(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}

static uint32_t pixel(int x,int y)
{
    int slot=slots[(y>>7)*64+(x>>7)];assert(slot>=0&&slot<12);
    return source[slot][(y&127)*128+(x&127)];
}

static int reference(Command& c)
{
    if(!c.transparent)return 1;
    int l=c.w,t=c.h,r=-1,b=-1;
    for(int y=0;y<c.h;++y)for(int x=0;x<c.w;++x)if(pixel(c.sx+x,c.sy+y)&0x20000000U){
        if(x<l)l=x;
        if(y<t)t=y;
        if(x>r)r=x;
        if(y>b)b=y;
    }
    if(r<0)return 0;
    c.x+=c.flipx?c.w-1-r:l;c.y+=c.flipy?c.h-1-b:t;
    c.sx+=l;c.sy+=t;c.w=r-l+1;c.h=b-t+1;return 1;
}

static void render(const Command& c,uint32_t* frame)
{
    for(int y=0;y<c.h;++y)for(int x=0;x<c.w;++x){
        uint32_t raw=pixel(c.sx+(c.flipx?c.w-1-x:x),c.sy+(c.flipy?c.h-1-y:y));
        if(!c.transparent||(raw&0x20000000U))frame[(c.y+y)*768+c.x+x]=raw;
    }
}

static void build(int pattern,int baseX=0,int baseY=0)
{
    for(int i=0;i<2048;++i)slots[i]=-1;
    for(int py=0;py<3;++py)for(int px=0;px<4;++px){
        int slot=(py*4+px)*5%12; // Atlas positions differ from source-page order.
        slots[(baseY/128+py)*64+baseX/128+px]=slot;
        for(int y=0;y<128;++y)for(int x=0;x<128;++x){
            int gx=px*128+x,gy=py*128+y;bool opaque=false;
            if(pattern==1)opaque=true;
            if(pattern==2)opaque=(random_word()&15)==0;
            if(pattern==3)opaque=((gx&31)>=7&&(gx&31)<=21&&(gy&31)>=5&&(gy&31)<=28);
            if(pattern==4)opaque=(x==0||x==31||x==32||x==63||x==64||x==127)&&(y==0||y==7||y==8||y==31||y==127);
            source[slot][y*128+x]=(random_word()&~0x20000000U)|(opaque?0x20000000U:0);
        }
        alpha[slot].build(source[slot],128);
    }
}

static void compare(Command c,bool pixels)
{
    Command expected=c,actual=c;
    int e=reference(expected),a=epic12_gpu_alpha_trim(actual,alpha,slots,16);
    assert(e==a);assert(memcmp(&expected,&actual,sizeof(c))==0);
    if(pixels){
        static uint32_t original[768*512],trimmed[768*512];
        for(unsigned i=0;i<768*512;++i)original[i]=trimmed[i]=0x53c0197aU;
        render(c,original);
        if(a)render(actual,trimmed);
        assert(memcmp(original,trimmed,sizeof(original))==0);
    }
}

static void build_rows_and_empty()
{
    static uint32_t padded[128*160];
    for(int y=0;y<128;++y)for(int x=0;x<160;++x)padded[y*160+x]=random_word();
    Epic12GpuAlphaPage a,b;
    a.build(padded,160);for(int y=0;y<128;++y)b.build_row(y,padded+y*160);
    assert(memcmp(a.rows,b.rows,sizeof(a.rows))==0);
    assert(memcmp(a.groups,b.groups,sizeof(a.groups))==0);
    memset(padded,0xdf,sizeof(padded));a.build(padded,160);
    int l=999,t=999,r=999,bot=999;
    assert(!a.bounds(0,0,127,127,l,t,r,bot));assert(l==999&&t==999&&r==999&&bot==999);
    assert(sizeof(Epic12GpuAlphaPage)==2304+128);
    puts("PASS row-by-row/strided build, overwrite invalidation, 2304-byte metadata plus 128-byte bbox cache");
}

static void query_cache()
{
    static uint32_t data[128*128];
    memset(data,0,sizeof(data));data[40*128+20]=0x20000000U;
    Epic12GpuAlphaPage page;page.build(data,128);
    int l=999,t=999,r=999,b=999;
    memset(&epic12_gpu_alpha_stats,0,sizeof(epic12_gpu_alpha_stats));
    assert(page.bounds(0,40,127,40,l,t,r,b));assert(l==20&&t==40&&r==20&&b==40);
    assert(page.bounds(0,40,127,40,l,t,r,b));
    assert(epic12_gpu_alpha_stats.cacheMisses==1&&epic12_gpu_alpha_stats.cacheHits==1);
    // Any row write invalidates cached answers, including a nonzero row and a
    // row outside the queried rectangle. Query a single row while rebuilding:
    // the eight-row group summaries retain their original ordered-build API.
    data[40*128+20]=0;data[40*128+90]=0x20000000U;
    page.build_row(40,data+40*128);
    assert(page.bounds(0,40,127,40,l,t,r,b));assert(l==90&&r==90);
    assert(epic12_gpu_alpha_stats.cacheMisses==2);
    page.build_row(7,data+7*128);
    assert(page.bounds(0,40,127,40,l,t,r,b));assert(l==90&&r==90);
    assert(epic12_gpu_alpha_stats.cacheMisses==3);
    // Empty hits preserve every output, including after a nonempty query.
    l=123;t=456;r=789;b=999;
    assert(!page.bounds(0,0,31,7,l,t,r,b));
    assert(!page.bounds(0,0,31,7,l,t,r,b));
    assert(l==123&&t==456&&r==789&&b==999);
    uint64_t hits=epic12_gpu_alpha_stats.cacheHits;
    assert(hits==2);
    // A complete build replaces all source data and flushes old empty answers.
    for(int i=0;i<128*128;++i)data[i]=0x20000000U;
    page.build(data,128);
    assert(page.bounds(0,0,31,7,l,t,r,b));assert(l==0&&t==0&&r==31&&b==7);
    // Exercise FIFO eviction and collisions in a single set. The full 28-bit
    // point (127,127) key must also remain distinct from both invalid markers.
    unsigned points[24],count=0;
    for(unsigned y=0;y<128&&count<24;++y)for(unsigned x=0;x<128&&count<24;++x) {
        uint32_t key=x|(y<<7)|(x<<14)|(y<<21);
        if(Epic12GpuAlphaPage::cache_set(key)==0)points[count++]=x|(y<<7);
    }
    assert(count==24);
    for(unsigned i=0;i<480;++i) {
        unsigned point=points[i%24];int x=point&127,y=point>>7;
        assert(page.bounds(x,y,x,y,l,t,r,b));assert(l==x&&r==x&&t==y&&b==y);
        assert(page.bounds(x,y,x,y,l,t,r,b));assert(l==x&&r==x&&t==y&&b==y);
    }
    assert(page.bounds(127,127,127,127,l,t,r,b));
    assert(page.bounds(127,127,127,127,l,t,r,b));assert(l==127&&t==127&&r==127&&b==127);
    puts("PASS cache hits/empty outputs, any-row/full-build invalidation, FIFO/hash collisions and maximum key");
}

static void boundary_and_random()
{
    const int size[]={1,7,8,9,31,32,33,63,64,65,127,128,129,255,256,384};
    unsigned comparisons=0;
    for(int pattern=0;pattern<5;++pattern){
        int baseX=pattern==4?7680:0,baseY=pattern==4?3712:0;
        build(pattern,baseX,baseY);
        for(int i=0;i<2400;++i){
            Command c;memset(&c,0,sizeof(c));
            c.w=i<256?size[i&15]:1+random_word()%65;
            c.h=i<256?size[(i>>4)&15]:1+random_word()%65;
            c.sx=baseX+random_word()%(512-c.w+1);
            c.sy=baseY+random_word()%(384-c.h+1);
            c.x=17;c.y=23;c.flipx=i&1;c.flipy=(i>>1)&1;c.transparent=i%7!=0;
            c.blend=5;c.sa=19;c.da=27;c.tint=0x57314211;
            compare(c,(i%47)==0);++comparisons;
        }
    }
    printf("PASS %u exact-bbox comparisons; cross-page, all flips, clipped dimensions, VRAM edges and pixel equality\n",comparisons);
}

static void errors_and_batches()
{
    build(3);
    Command commands[32],out[32],expected[32];int kept=0;
    Bounds b={-111,-112,-113,-114},ref={8192,-1,4096,-1};
    for(int i=0;i<32;++i){
        Command c;memset(&c,0,sizeof(c));c.sx=i*11;c.sy=i*7;c.w=17;c.h=19;c.x=i*13;c.y=i*9;c.transparent=1;c.flipx=i&1;c.flipy=i&2;c.sa=i;
        commands[i]=c;
        if(reference(c)){
            expected[kept++]=c;
            if(c.x<ref.min_x)ref.min_x=c.x;
            if(c.y<ref.min_y)ref.min_y=c.y;
            if(c.x+c.w-1>ref.max_x)ref.max_x=c.x+c.w-1;
            if(c.y+c.h-1>ref.max_y)ref.max_y=c.y+c.h-1;
        }
    }
    assert(epic12_gpu_alpha_crop_batch(commands,32,out,alpha,slots,16,b)==kept);
    assert(memcmp(&b,&ref,sizeof(b))==0&&memcmp(out,expected,kept*sizeof(Command))==0);
    assert(epic12_gpu_alpha_crop_batch(commands,32,commands,alpha,slots,16,b)==kept);
    assert(memcmp(commands,expected,kept*sizeof(Command))==0);
    Command c=expected[0],original=c;c.transparent=0;c.sx=INT_MAX;
    original=c;assert(epic12_gpu_alpha_trim(c,NULL,NULL,0)==1&&memcmp(&c,&original,sizeof(c))==0);
    c=expected[0];int sourcePage=(c.sy>>7)*64+(c.sx>>7),saved=slots[sourcePage];
    const int invalidSlots[]={-1,16,INT_MAX};
    for(unsigned i=0;i<3;++i){slots[sourcePage]=invalidSlots[i];original=c;assert(epic12_gpu_alpha_trim(c,alpha,slots,16)==-1);assert(memcmp(&c,&original,sizeof(c))==0);}
    slots[sourcePage]=saved;
    for(int i=0;i<8;++i){
        c=expected[0];
        if(i==0)c.sx=-1;
        if(i==1)c.sy=4096;
        if(i==2)c.w=INT_MAX;
        if(i==3)c.h=0;
        if(i==4)c.x=INT_MAX;
        if(i==5)c.y=-1;
        if(i==6)c.sx=8192;
        if(i==7)c.h=-1;
        original=c;assert(epic12_gpu_alpha_trim(c,alpha,slots,16)==-1);assert(memcmp(&c,&original,sizeof(c))==0);
    }
    build(0);c=expected[0];b=ref;
    assert(epic12_gpu_alpha_crop_batch(&c,1,out,alpha,slots,16,b)==0&&memcmp(&b,&ref,sizeof(b))==0);
    assert(epic12_gpu_alpha_crop_batch(&c,0,out,alpha,slots,16,b)==0);
    assert(epic12_gpu_alpha_crop_batch(&c,-1,out,alpha,slots,16,b)==-1);
    puts("PASS empty batch, bounds union, order/in-place compaction, missing slots and overflow guards");
}

static void query_cost()
{
    build(3);memset(&epic12_gpu_alpha_stats,0,sizeof(epic12_gpu_alpha_stats));
    const unsigned count=300000;uint64_t pixels=0;unsigned checksum=0;
    clock_t start=clock();
    for(unsigned i=0;i<count;++i){
        Command c;memset(&c,0,sizeof(c));c.sx=(i*7)%400;c.sy=(i*13)%256;c.w=16+(i&3)*8;c.h=16+((i>>2)&3)*8;c.x=c.y=0;c.transparent=1;c.flipx=i&1;c.flipy=i&2;
        pixels+=(uint64_t)c.w*c.h;
        assert(epic12_gpu_alpha_trim(c,alpha,slots,16)>=0);checksum+=c.w+c.h;
    }
    double ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
    printf("QUERY COST commands=%u source_pixels=%llu summary_words=%llu row_words=%llu page_queries=%llu cache_hits=%llu cache_misses=%llu metadata_words_per_command=%.2f cpu_ms=%.3f checksum=%u\n",
        count,(unsigned long long)pixels,(unsigned long long)epic12_gpu_alpha_stats.summaryReads,
        (unsigned long long)epic12_gpu_alpha_stats.rowReads,(unsigned long long)epic12_gpu_alpha_stats.queryPages,
        (unsigned long long)epic12_gpu_alpha_stats.cacheHits,(unsigned long long)epic12_gpu_alpha_stats.cacheMisses,
        (double)(epic12_gpu_alpha_stats.summaryReads+epic12_gpu_alpha_stats.rowReads)/count,ms,checksum);
    assert(epic12_gpu_alpha_stats.rowReads+epic12_gpu_alpha_stats.summaryReads<pixels/8);
}

int main(){build_rows_and_empty();query_cache();boundary_and_random();errors_and_batches();query_cost();puts("PASS EPIC12 alpha metadata tests");return 0;}
