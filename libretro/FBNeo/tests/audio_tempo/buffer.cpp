#include <audio/audiorate.h>
#include <vector>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <time.h>

static long long cpuNanos() { struct timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return (long long)t.tv_sec*1000000000+t.tv_nsec; }

static void boundaries() {
    AudioBuffer b;b.ConfigureTempo(true);
    std::vector<int16_t> data(BUFF_SIZE*2);
    for(size_t i=0;i<data.size()/2;i++){data[i*2]=10000;data[i*2+1]=-10000;}
    assert(b.Write(&data[0],BUFF_SIZE)==BUFF_SIZE-2);
    assert(b.getUsed()==BUFF_SIZE-2);
    assert(b.Write(&data[0],2)==0);
    int16_t out[2048];
    for(unsigned turn=0;turn<200;turn++) {
        b.Read(out,2048);
        for(unsigned i=256;i<1024;i++)assert(out[i*2]==10000&&out[i*2+1]==-10000);
        assert(b.Write(&data[0],2048)==2048);
        assert((b.getUsed()&1)==0);
    }
    b.SetTempoPlayback(true);assert(b.getUsed()==0);
    b.Write(&data[0],6000);b.SetTempoPlayback(false);assert(b.getUsed()==0);
    b.SetTempoPlayback(true);assert(b.getUsed()==0);
    b.Clear();assert(b.getUsed()==0&&b.getDropsTotal()==0&&b.getTempoStats().callbacks==0);
    puts("PASS stereo overflow, ring wrap, pause/fast-forward reset, Clear");
}

static void blocking() {
    AudioBuffer b;b.ConfigureTempo(true);
    std::vector<int16_t> data(48000*4);
    for(unsigned i=0;i<data.size()/2;i++) {
        data[i*2]=(int16_t)(i%20000);data[i*2+1]=(int16_t)-(int)(i%20000);
    }
    std::atomic<bool> done(false);
    std::thread producer([&](){b.WriteBlocking(&data[0],data.size());done=true;});
    size_t consumed=0;int16_t output[2048];
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(consumed<data.size()) {
        size_t remaining=data.size()-consumed,n=remaining<2048?remaining:2048;
        if(b.getUsed()<n){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();continue;}
        b.Read(output,n);
        for(size_t i=0;i<n;i++)if(consumed+i>=512)assert(output[i]==data[consumed+i]);
        consumed+=n;
    }
    producer.join();assert(done&&b.getDropsTotal()==0);
    b.Clear();b.SetTempoPlayback(true);done=false;
    std::thread stretchProducer([&](){b.WriteBlocking(&data[0],data.size());done=true;});
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!done) {
        b.Read(output,2048);
        assert(std::chrono::steady_clock::now()<deadline);
        std::this_thread::yield();
    }
    stretchProducer.join();assert(b.getDropsTotal()==0&&b.getTempoStats().generatedFrames>0);
    puts("PASS oversized blocking writes with normal and WSOLA concurrent consumers");
}

static void integrated(const char* name,double fps,unsigned callbackFrames,bool stall,unsigned sourceRate=48000) {
    AudioBuffer b;b.ConfigureTempo(true);b.SetTempoPlayback(true);
    AudioRateControl rate;rate.init(BUFF_SIZE);rate.setRates(sourceRate,48000);
    double nextProducer=0,nextConsumer=0;
    unsigned source=0;
    long steady=0,oldUnder=0;
    const unsigned packetFrames=sourceRate/60;
    int16_t input[1600];std::vector<int16_t> output(callbackFrames*2);
    long long timeNs=0,maxNs=0;unsigned callbacks=0;
    while(nextConsumer<30.0){
        if(nextProducer<=nextConsumer) {
            if(stall&&nextProducer>=10&&nextProducer<10.2){nextProducer=10.2;continue;}
            for(unsigned i=0;i<packetFrames;i++,source++){
                // Two independent musical frequencies plus percussive decay.
                double t=(double)source/sourceRate;
                double drum=0.15*exp(-fmod(t,0.25)*90.0)*sin(t*6.28318530718*73);
                input[i*2]=(int16_t)(12000*(sin(t*6.28318530718*220)+0.25*sin(t*6.28318530718*880)+drum));
                input[i*2+1]=(int16_t)(10000*(sin(t*6.28318530718*660)+drum));
            }
            rate.processAndWrite(b,input,packetFrames,false);
            nextProducer+=1.0/fps;continue;
        }
        long long start=cpuNanos();
        b.Read(&output[0],callbackFrames*2);
        long long ns=cpuNanos()-start;
        timeNs+=ns;maxNs=std::max(maxNs,ns);callbacks++;
        long under=b.getUnderrunsTotal();
        if(nextConsumer>2&&(!stall||nextConsumer<9.9||nextConsumer>11))steady+=under-oldUnder;
        oldUnder=under;
        nextConsumer+=callbackFrames/48000.0;
    }
    printf("%s: callback=%u frames steady_underruns=%ld total_underruns=%ld drops=%ld tempo=%.3f host_thread_cpu_mean=%.3f ms host_thread_cpu_max=%.3f ms\n",
        name,callbackFrames,steady,b.getUnderrunsTotal(),b.getDropsTotal(),b.getTempoStats().tempo/65536.0,
        timeNs/1e6/callbacks,maxNs/1e6);
    assert(steady==0);assert(b.getDropsTotal()==0);
}

int main(){boundaries();blocking();integrated("60 FPS mixed",60,1024,false);integrated("40 FPS mixed",40,1024,false);integrated("30 FPS mixed",30,1024,false);integrated("20 FPS mixed",20,1024,false);integrated("24 FPS mixed",24,1024,false);integrated("30 FPS 44.1 kHz",30,1024,false,44100);integrated("30 FPS large callback",30,2048,false);integrated("30 FPS with 200 ms stall",30,1024,true);puts("PASS audio integration tests");}
