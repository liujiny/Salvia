// Compile the unmodified savestate section of retro_memory.cpp with a small
// synthetic driver. This compares the new transport with the actual legacy
// serializers instead of duplicating their layout in a reference encoder.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>
#define __cdecl
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef int32_t INT32;
#include "../../src/burn/state.h"
#include "../../src/burner/libretro/retro_memory.h"

static std::vector<std::vector<UINT8> > areas;
static unsigned contextValue, scanCalls, restoreHooks, paletteCalls;
static int lastAction, avEnable = 3;
static bool injectNull, injectHuge;
unsigned nBurnDrvActive = 0;
int nCurrentFrame = 123456, nDiagInputHoldCounter = 17;
int EnableHiscores = 1, kNetGame;
bool bLibretroSupportsSavestateContext = true;
INT32 (__cdecl *BurnAcb)(BurnArea*);

static bool RETRO_CALLCONV environment(unsigned cmd, void *value)
{
    if(cmd == RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT) {
        *(int*)value = contextValue; return true;
    }
    if(cmd == RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE) {
        *(int*)value = avEnable; return true;
    }
    return false;
}
retro_environment_t environ_cb = environment;

INT32 BurnAreaScan(INT32 action, INT32*)
{
    ++scanCalls; lastAction = action;
    for(size_t i=0; i<areas.size(); ++i) {
        BurnArea area = {&areas[i][0], (UINT32)(areas[i].size()-1), 0, (char*)"test"};
        if(injectNull && i==1) area.Data = NULL;
        if(injectHuge && i==1) area.nLen = ~0U;
        BurnAcb(&area); // Drivers may ignore callback errors; test the latch.
    }
    if(action & ACB_WRITE) ++restoreHooks;
    return 0;
}
static void BurnRecalcPal() { ++paletteCalls; }

#include "retro_state_implementation.inc"

struct Io {
    std::vector<UINT8> *data;
    size_t offset, calls, maximum, failAt;
    bool loading, reenter;
    Io(std::vector<UINT8>& bytes, bool load=false):data(&bytes), offset(0), calls(0),
        maximum(0), failAt((size_t)-1), loading(load), reenter(false) {}
};

static bool RETRO_CALLCONV transfer(void *context, void *data, size_t size)
{
    Io& io=*(Io*)context;
    assert(size>0 && size<=65536);
    if(io.calls++==io.failAt) return false;
    if(io.reenter) {
        io.reenter=false;
        assert(!fbneo_state_stream_v1(0, io.data->size(), transfer, &io));
    }
    assert(io.offset+size<=io.data->size());
    if(io.loading) memcpy(data,&(*io.data)[io.offset],size);
    else assert(memcmp(data,&(*io.data)[io.offset],size)==0);
    io.offset+=size;
    if(size>io.maximum) io.maximum=size;
    return true;
}

static void initialize()
{
    nCurrentFrame=123456; nDiagInputHoldCounter=17;
    for(size_t a=0;a<areas.size();++a)
        for(size_t i=0;i<areas[a].size();++i) areas[a][i]=(UINT8)(a*113+i*37+(i>>8));
}
static std::vector<UINT8> serialized()
{
    std::vector<UINT8> result(retro_serialize_size());
    assert(retro_serialize(&result[0],result.size()));
    return result;
}
static void clobber()
{
    nCurrentFrame=7; nDiagInputHoldCounter=9;
    for(size_t a=0;a<areas.size();++a) memset(&areas[a][0],0x5a,areas[a].size());
}

int main()
{
    const size_t sizes[]={0,1,65535,65536,65537,131089};
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i)
        areas.push_back(std::vector<UINT8>(sizes[i]+1));
    assert(fbneo_get_proc_address(NULL)==NULL);
    assert(fbneo_get_proc_address("retro_serialize")==NULL);
    assert(fbneo_get_proc_address("fbneo_state_stream_v1")==(retro_proc_address_t)fbneo_state_stream_v1);
    size_t comparisons=0;
    for(contextValue=0;contextValue<5;++contextValue) {
        initialize();
        std::vector<UINT8> legacy=serialized();
        int expectedFlags=lastAction;
        Io save(legacy); save.reenter=true;
        INT32 (__cdecl *previous)(BurnArea*)=BurnAcb;
        assert(fbneo_state_stream_v1(0,legacy.size(),transfer,&save));
        assert(BurnAcb==previous && lastAction==expectedFlags);
        assert(save.offset==legacy.size() && save.maximum==65536);
        comparisons+=save.offset;
        clobber(); Io load(legacy,true);
        unsigned oldPalette=paletteCalls,oldHooks=restoreHooks;
        assert(fbneo_state_stream_v1(1,legacy.size(),transfer,&load));
        assert((lastAction & ACB_ACCESSMASK)==ACB_WRITE);
        assert(paletteCalls==oldPalette+1 && restoreHooks==oldHooks+1);
        assert(serialized()==legacy);
        // Legacy readers must also accept the identical byte stream.
        clobber(); assert(retro_unserialize(&legacy[0],legacy.size()));
        assert(serialized()==legacy);
    }
    contextValue=RETRO_SAVESTATE_CONTEXT_NORMAL;
    initialize(); std::vector<UINT8> legacy=serialized();
    Io base(legacy); assert(fbneo_state_stream_v1(0,legacy.size(),transfer,&base));
    for(size_t failed=0;failed<base.calls;++failed) {
        Io write(legacy); write.failAt=failed;
        assert(!fbneo_state_stream_v1(0,legacy.size(),transfer,&write));
        assert(write.calls==failed+1);
        Io read(legacy,true); read.failAt=failed;
        unsigned oldPalette=paletteCalls;
        assert(!fbneo_state_stream_v1(1,legacy.size(),transfer,&read));
        assert(read.calls==failed+1 && paletteCalls==oldPalette);
    }
    Io small(legacy);
    assert(!fbneo_state_stream_v1(0,3,transfer,&small) && small.calls==0);
    Io shortEnd(legacy);
    assert(!fbneo_state_stream_v1(0,legacy.size()-1,transfer,&shortEnd));
    Io oversized(legacy);
    assert(!fbneo_state_stream_v1(0,legacy.size()+1,transfer,&oversized));
    assert(oversized.offset==legacy.size());
    Io hugeSize(legacy);
    assert(!fbneo_state_stream_v1(0,(size_t)-1,transfer,&hugeSize));
    assert(hugeSize.offset==legacy.size());
    unsigned previousScans=scanCalls;
    assert(!fbneo_state_stream_v1(2,legacy.size(),transfer,&base));
    assert(!fbneo_state_stream_v1(0,0,transfer,&base));
    assert(!fbneo_state_stream_v1(0,legacy.size(),NULL,&base));
    nBurnDrvActive=~0U;
    assert(!fbneo_state_stream_v1(0,legacy.size(),transfer,&base));
    nBurnDrvActive=0;
    assert(scanCalls==previousScans);
    Io nullArea(legacy); injectNull=true;
    assert(!fbneo_state_stream_v1(0,legacy.size(),transfer,&nullArea));
    assert(nullArea.calls==1); injectNull=false;
    Io hugeArea(legacy); injectHuge=true;
    assert(!fbneo_state_stream_v1(0,legacy.size(),transfer,&hugeArea));
    assert(hugeArea.calls==1); injectHuge=false;
    // Failed operations must release the callback/context and permit retry.
    Io retry(legacy); assert(fbneo_state_stream_v1(0,legacy.size(),transfer,&retry));
    bLibretroSupportsSavestateContext=false;
    for(avEnable=3;avEnable<=7;avEnable+=4) {
        legacy=serialized(); int flags=lastAction;
        Io io(legacy); assert(fbneo_state_stream_v1(0,legacy.size(),transfer,&io));
        assert(lastAction==flags && kNetGame==((avEnable&4)?1:0));
    }
    printf("PASS: %lu legacy-byte comparisons; all 5 contexts, 64 KiB boundaries, roundtrip, errors, retries and callback restoration\n",(unsigned long)comparisons);
    return 0;
}
