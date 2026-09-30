#include <audio/audiotempo.h>
#include <vector>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <chrono>
#include <algorithm>

struct Ring {
    int16_t data[16384];
    size_t head, tail, count;
    unsigned drops;
    Ring():head(0),tail(0),count(0),drops(0) {}
    size_t available() const { return count; }
    void copy(size_t offset, int16_t* dst, size_t frames) {
        assert(offset + frames <= count);
        for(size_t i=0;i<frames;i++) {
            size_t p=(tail+offset+i)&8191;
            dst[i*2]=data[p*2];dst[i*2+1]=data[p*2+1];
        }
    }
    void consume(size_t frames) {
        assert(frames<=count);tail=(tail+frames)&8191;count-=frames;
    }
    void put(int16_t l,int16_t r) {
        if(count==8191){drops++;return;}
        data[head*2]=l;data[head*2+1]=r;head=(head+1)&8191;count++;
    }
};

static void unity() {
    AudioTempo t;Ring r;
    std::vector<int16_t> input;
    unsigned seed=1;
    for(unsigned i=0;i<500000;i++){
        seed=1664525*seed+1013904223;
        input.push_back((int16_t)(seed>>16));
    }
    size_t written=0,read=0;
    int16_t out[2048];
    while(read<400000){
        while(r.available()<7000){r.put(input[written],input[written+1]);written+=2;}
        size_t n=t.read(r,out,1024);
        assert(n==1024);
        for(size_t i=0;i<n*2;i++)assert(out[i]==input[read+i]);
        read+=n*2;
    }
    assert(t.getStats().stretchedFrames==0);
    puts("PASS unity bit-exact stereo, including ring wraps");
}

static void simulation(const char* name,double fps,bool transition) {
    AudioTempo tempo;Ring ring;
    std::vector<int16_t> output;
    double producer=0,consumer=0;
    unsigned sourceFrames=0,shortage=0,steadyShortage=0;
    long long elapsedNs=0,maxNs=0;
    unsigned callbacks=0;
    int16_t block[2048];
    while(consumer<30.0){
        if(producer<=consumer){
            for(unsigned i=0;i<800;i++){
                int16_t sample=(int16_t)(12000.0*sin(6.283185307179586*440.0*sourceFrames/48000.0));
                ring.put(sample,(int16_t)-sample);sourceFrames++;
            }
            double rate=transition?(producer<8.0||producer>=20.0?60.0:fps):fps;
            producer+=1.0/rate;
            continue;
        }
        memset(block,0,sizeof(block));
        auto start=std::chrono::steady_clock::now();
        size_t got=tempo.read(ring,block,1024);
        auto end=std::chrono::steady_clock::now();
        long long ns=std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        elapsedNs+=ns;maxNs=std::max(maxNs,ns);callbacks++;
        if(got<1024){shortage++;if(consumer>2&&(!transition||(consumer>9&&consumer<19)))steadyShortage++;}
        for(unsigned i=0;i<got;i++)assert(abs((int)block[i*2]+block[i*2+1])<=1);
        output.insert(output.end(),block,block+2048);
        consumer+=1024.0/48000.0;
    }
    unsigned crossings=0;double begin=transition?11.0:5.0,end=transition?18.0:25.0;
    for(size_t i=(size_t)(begin*48000)+1;i<(size_t)(end*48000);i++)
        if(output[(i-1)*2]<=0&&output[i*2]>0)crossings++;
    double frequency=crossings/(end-begin);
    AudioTempo::Stats stats=tempo.getStats();
    printf("%s: steady underrun=%u startup/transition=%u drops=%u tone=%.2f Hz min_tempo=%.3f final=%.3f host_callback_mean=%.3f ms max=%.3f ms\n",
        name,steadyShortage,shortage-steadyShortage,ring.drops,frequency,stats.minTempo/65536.0,stats.tempo/65536.0,
        elapsedNs/1000000.0/callbacks,maxNs/1000000.0);
    assert(steadyShortage==0);
    assert(ring.drops==0);
    assert(fabs(frequency-440.0)<2.0);
    if(!transition&&fps==60)assert(stats.stretchedFrames==0);
    if(transition)assert(stats.tempo==AudioTempo::ONE);
}

int main(){unity();simulation("60 FPS",60,false);simulation("40 FPS",40,false);simulation("30 FPS",30,false);simulation("20 FPS",20,false);simulation("60->30->60 FPS",30,true);puts("PASS audio tempo tests");}
