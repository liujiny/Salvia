// Exercise the production frontend, including its real queue and gzip helpers.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>
#include <libretro/libretro.h>
#define EMU_LIB_NAME "fbneo"
#define LOG_ERROR(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define STATE_IMG_EXT ".png"
static pthread_t mainThread;
typedef unsigned Uint32;
typedef pthread_mutex_t SDL_mutex;
typedef sem_t SDL_sem;
struct SDL_Thread { pthread_t id; int (*fn)(void*); void* arg; };
static SDL_mutex* SDL_CreateMutex() { SDL_mutex* p=new SDL_mutex; pthread_mutex_init(p,0);return p; }
static void SDL_DestroyMutex(SDL_mutex* p) {pthread_mutex_destroy(p);delete p;}
static void SDL_mutexP(SDL_mutex* p) {pthread_mutex_lock(p);}
static void SDL_mutexV(SDL_mutex* p) {pthread_mutex_unlock(p);}
static SDL_sem* SDL_CreateSemaphore(int n) {SDL_sem* p=new SDL_sem;sem_init(p,0,n);return p;}
static void SDL_DestroySemaphore(SDL_sem* p) {sem_destroy(p);delete p;}
static void SDL_SemWait(SDL_sem* p) {while(sem_wait(p)!=0) {}}
static void SDL_SemPost(SDL_sem* p) {sem_post(p);}
static void* trampoline(void* p) {SDL_Thread* t=(SDL_Thread*)p;t->fn(t->arg);return 0;}
static SDL_Thread* SDL_CreateThread(int(*fn)(void*),void* arg) {SDL_Thread*t=new SDL_Thread;t->fn=fn;t->arg=arg;pthread_create(&t->id,0,trampoline,t);return t;}
static void SDL_WaitThread(SDL_Thread* t,void*) {pthread_join(t->id,0);delete t;}
static void SDL_Delay(unsigned n) {usleep(n*1000);}
static Uint32 SDL_GetTicks() {return (Uint32)(clock()*1000/CLOCKS_PER_SEC);}
static const int SDL_AUDIO_PLAYING=1;
static int SDL_GetAudioStatus() {return SDL_AUDIO_PLAYING;}
static void SDL_PauseAudio(int) {}
static void SDL_LockAudio() {}
static void SDL_UnlockAudio() {}
namespace cfg {enum {enableAchievements,hardcoreRA};struct t_cfg_props {bool valueBool;};}
struct CfgLoader {cfg::t_cfg_props configMain[2];CfgLoader(){configMain[0].valueBool=configMain[1].valueBool=false;}};
static unsigned menuRefresh=0;
struct TestMenus {void poblarPartidasGuardadas(CfgLoader*,const std::string&){assert(pthread_equal(mainThread,pthread_self()));++menuRefresh;}};
struct TestAudio {void Clear(){}};
struct GameMenu {
 CfgLoader config;TestMenus menus;TestMenus* configMenus;TestAudio g_audioBuffer;
 std::vector<std::string> messages;
 GameMenu():configMenus(&menus){}
 CfgLoader* getCfgLoader(){return &config;}
 void showSystemMessage(const std::string&s,unsigned){assert(pthread_equal(mainThread,pthread_self()));messages.push_back(s);}
 void showLangSystemMessage(const char*s,unsigned n){showSystemMessage(s,n);}
};
struct t_rom_paths {std::string savestate,rompath;};
class LanguageManager {public:static LanguageManager*instance(){static LanguageManager m;return &m;}std::string get(const char*s){return s;}};
class Constant {public:
 static std::string checkPath(const std::string&s){return s;}
 template<class T>static std::string TipoToStr(T t){std::ostringstream s;s<<t;return s.str();}
 static std::string intToString(int t){return TipoToStr(t);}
};
class Fileio {public:static void commit(const char*){}};
struct rc_client_t {};
static unsigned char achievements[37];
class Achievements {public:static Achievements* instance(){static Achievements a;return &a;}rc_client_t*getClient(){static rc_client_t c;return &c;}};
static size_t rc_client_progress_size(rc_client_t*){return sizeof achievements;}
static int rc_client_serialize_progress(rc_client_t*,uint8_t*p){memcpy(p,achievements,sizeof achievements);return 0;}
static void rc_client_deserialize_progress(rc_client_t*,const uint8_t*p){if(p)memcpy(achievements,p,sizeof achievements);else memset(achievements,0,sizeof achievements);}
static const int LCT_RGB=2;
static bool failPreview=false;
namespace lodepng {static unsigned encode(const std::string&,const std::vector<unsigned char>&,int,int,int,int){if(failPreview)throw std::bad_alloc();return 0;}}
GameMenu* gameMenu;
t_rom_paths romPaths;
static std::vector<unsigned char> liveState;
static unsigned char sram[4096];
static bool failLoad=false, failSave=false;
extern "C" size_t retro_serialize_size(){return liveState.size();}
extern "C" bool retro_serialize(void*p,size_t n){if(failSave)return false;memcpy(p,&liveState[0],n);return true;}
extern "C" bool retro_unserialize(const void*p,size_t n){if(failLoad)return false;memcpy(&liveState[0],p,n);return true;}
extern "C" void* retro_get_memory_data(unsigned){return sram;}
extern "C" size_t retro_get_memory_size(unsigned){return sizeof sram;}
static bool rejectLargeAllocations=false;
static size_t largestStateAllocation=0;
static void* stateMalloc(size_t size) {
 if (size>largestStateAllocation) largestStateAllocation=size;
 if (rejectLargeAllocations && size>65536) return NULL;
 return malloc(size);
}
#define malloc stateMalloc
#include <io/statesram.h>
#undef malloc
void salvia_state_progress(bool) {assert(pthread_equal(mainThread,pthread_self()));}
static bool RETRO_CALLCONV stream(unsigned mode,size_t n,FbneoStateIo io,void* ctx) {
 if ((mode&&failLoad)||(!mode&&failSave)) return false;
 for(size_t at=0;at<n;at+=SalviaStateFile::CHUNK){size_t amount=n-at;if(amount>SalviaStateFile::CHUNK)amount=SalviaStateFile::CHUNK;if(!io(ctx,&liveState[at],amount))return false;}
 return true;
}
static retro_proc_address_t RETRO_CALLCONV proc(const char*name) {return strcmp(name,"fbneo_state_stream_v1")==0?(retro_proc_address_t)stream:0;}
static std::vector<unsigned char> fileBytes(const char*path) {
 FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(&b[0],1,n,f)==(size_t)n);fclose(f);return b;
}
static void writeBytes(const char*path,const std::vector<unsigned char>&b) {FILE*f=fopen(path,"wb");assert(f);assert(fwrite(&b[0],1,b.size(),f)==b.size());fclose(f);}
static void primeScreenshot(){clearStateScreenshot();action_postponed.screenshot=new uint8_t[32];action_postponed.width=4;action_postponed.height=4;action_postponed.bpp=16;memset(action_postponed.screenshot,0,32);}
static void resetAction(){action_postponed.action=SAVE_NONE;action_postponed.cycles=-1;}
static void validateFailures(const std::vector<unsigned char>&good) {
 std::vector<unsigned char> bad=good;bad[bad.size()-8]^=1;writeBytes("slot",bad);
 const unsigned char sentinel=liveState[100];unsigned oldRefresh=menuRefresh;
 assert(!loadState());assert(liveState[100]==sentinel&&menuRefresh==oldRefresh);
 bad=good;bad.resize(bad.size()-6);writeBytes("slot",bad);assert(!loadState());assert(liveState[100]==sentinel);
 bad=good;bad.resize(bad.size()/2);writeBytes("slot",bad);assert(!loadState());assert(liveState[100]==sentinel);
 writeBytes("slot",good);
}
int main(int argc,char**argv) {
 assert(argc==2);assert(chdir(argv[1])==0);mainThread=pthread_self();GameMenu menu;gameMenu=&menu;romPaths.savestate="slot";romPaths.rompath="test.zip";
 initSaveSystem();liveState.resize(3*1024*1024+11);
 for(size_t i=0;i<liveState.size();++i)liveState[i]=(unsigned char)((i*71)^(i>>11));
 memset(achievements,0x5a,sizeof achievements);
 std::vector<unsigned char> expected=liveState;
 // Stream success; old-format gzip bytes remain loadable.
 g_state_get_proc=proc;rejectLargeAllocations=true;primeScreenshot();assert(saveState());assert(menuRefresh==1);
 std::vector<unsigned char> good=fileBytes("slot");
 memset(&liveState[0],0,liveState.size());memset(achievements,0,sizeof achievements);
 assert(loadState());assert(liveState==expected&&achievements[0]==0x5a);
 validateFailures(good);
 // Failure leaves the previous file intact; failed restore is not called success.
 failSave=true;primeScreenshot();assert(!saveState());failSave=false;assert(fileBytes("slot")==good);
 failLoad=true;assert(!loadState());failLoad=false;
 // Deflate can accept input and fail only on close (full disk). Keep old slot.
 assert(symlink("/dev/full","slot.tmp")==0);primeScreenshot();assert(!saveState());
 assert(fileBytes("slot")==good);assert(access("slot.tmp",F_OK)!=0);
 // A completed legacy worker owns and frees its detached allocation exclusively.
 g_state_get_proc=0;rejectLargeAllocations=false;primeScreenshot();assert(saveState());
 assert(!requestSaveState(0));saveSram("sram"); // Cannot replace a queued/active state.
 waitSaveSystem();assert(!saveSystemBusy());assert(menuRefresh==2);
 memset(&liveState[0],0,liveState.size());assert(loadState());assert(liveState==expected);
 // Legacy core-only files (no RCHV) are accepted; RA is reset.
 gzFile f=gzopen("slot","wb1");assert(f);assert(SalviaStateFile::write(f,&expected[0],expected.size()));assert(gzclose(f)==Z_OK);
 memset(&liveState[0],0,liveState.size());assert(loadState());assert(liveState==expected&&achievements[0]==0);
 // FIFO ownership, repeated enqueue, completion on main, and draining shutdown.
 for(unsigned i=0;i<32;++i){memset(sram,i,sizeof sram);saveSram("sram");waitSaveSystem();}
 assert(fileBytes("sram")==std::vector<unsigned char>(sizeof sram,31));
 memset(sram,0xc3,sizeof sram);saveSram("sram");deinitSaveSystem();assert(fileBytes("sram")==std::vector<unsigned char>(sizeof sram,0xc3));
 // CV1000-sized stream: only live core memory plus bounded zlib/chunk storage.
 initSaveSystem();g_state_get_proc=proc;rejectLargeAllocations=true;largestStateAllocation=0;
 liveState.assign(151*1024*1024+123,0x69);failPreview=true;
 primeScreenshot();assert(saveState());memset(&liveState[0],0,liveState.size());assert(loadState());
 for(size_t i=0;i<liveState.size();++i)assert(liveState[i]==0x69);
 assert(largestStateAllocation<=65536);
 deinitSaveSystem();puts("frontend-state PASS: streaming, 151 MiB, gzip CRC/truncation, atomic failure, legacy, worker ownership/shutdown");return 0;
}
