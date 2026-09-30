#pragma once

#include <vector>
#include <string>
#include <new>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_thread.h>
#include "image/lodepng.h"
#include <utils/langmanager.h>
#include <libretro/libretro.h>
#if defined(_XBOX) || defined(_XBOX360)
#include <xtl.h>
#include <io.h>
#endif
#include <io/statefile.h>

const Uint32 INTERVAL_SRAM_SAVE = 60000;
Uint32 lastSramSaved = 0;
void* g_sram_data_last = NULL;
std::size_t g_sram_size_last = 0;
int g_currentSlot = 0;
typedef enum {SAVE_STATE, SAVE_SRAM, SAVE_NONE} ThreadAction;
extern GameMenu *gameMenu;
extern t_rom_paths romPaths;
void salvia_state_progress(bool loading);

/* Optional FBNeo extension, obtained through environment 33. Other cores keep
 * their standard libretro serializer and never enter the streaming path. */
typedef bool (RETRO_CALLCONV *FbneoStateIo)(void*, void*, size_t);
typedef bool (RETRO_CALLCONV *FbneoStateStream)(unsigned, size_t, FbneoStateIo, void*);
static retro_get_proc_address_t g_state_get_proc = NULL;

static bool isFbneoStateCore() {
    return strcmp(EMU_LIB_NAME, "fbneo") == 0;
}
static FbneoStateStream getFbneoStateStream() {
    if (!isFbneoStateCore() || !g_state_get_proc) return NULL;
    return (FbneoStateStream)g_state_get_proc("fbneo_state_stream_v1");
}
static void stateLog(const char* stage, size_t size, bool ok) {
    if (!isFbneoStateCore()) return;
#if defined(_XBOX) || defined(_XBOX360)
    FILE* f = fopen("game:\\fbneo-state.log", "a");
#else
    FILE* f = fopen("fbneo-state.log", "a");
#endif
    if (!f) return;
    fprintf(f, "ticks=%lu stage=%s bytes=%lu ok=%d slot=%d\n",
            (unsigned long)SDL_GetTicks(), stage, (unsigned long)size, ok ? 1 : 0,
            g_currentSlot);
#if defined(_XBOX) || defined(_XBOX360)
    MEMORYSTATUS memory; memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    fprintf(f, "memory_available=%lu\n", (unsigned long)memory.dwAvailPhys);
#endif
    fclose(f);
}
static void stateError(const char* reason) {
    LOG_ERROR("Save/load state: %s", reason);
    gameMenu->showSystemMessage(std::string("Save/load state: ") + reason, 5000);
}

struct delayed_action {
    int cycles;
    ThreadAction action;
    uint8_t* screenshot;
    unsigned width, height;
    int bpp;
    delayed_action() : cycles(-1), action(SAVE_NONE), screenshot(NULL),
        width(0), height(0), bpp(16) {}
} action_postponed;

/* There is one pending job and at most one worker-owned job. The producer
 * never frees worker memory. Completion UI is consumed only on the main thread. */
struct SaveData {
#if defined(_XBOX) || defined(_XBOX360)
    HANDLE thread;
#else
    SDL_Thread* thread;
#endif
    SDL_sem* semaphore;
    SDL_mutex* saveMutex;
    bool running, busy;
    ThreadAction action;
    std::string targetPath, romPath;
    void *buffer;
    uint8_t *screenshot;
    unsigned width, height;
    int bpp, slot;
    std::size_t bufferSize;
    bool resultPending, resultSuccess, resultImage;
    ThreadAction resultAction;
    std::string resultPath, resultRom;
    int resultSlot;
    SaveData() : thread(NULL), semaphore(NULL), saveMutex(NULL), running(false),
        busy(false), action(SAVE_NONE), buffer(NULL), screenshot(NULL), width(0),
        height(0), bpp(16), slot(0), bufferSize(0), resultPending(false),
        resultSuccess(false), resultImage(false), resultAction(SAVE_NONE),
        resultSlot(0) {}
} g_saveQueue;

static bool saveSystemBusy() {
    if (!g_saveQueue.saveMutex || !g_saveQueue.running) return true;
    SDL_mutexP(g_saveQueue.saveMutex);
    bool busy = g_saveQueue.busy || g_saveQueue.action != SAVE_NONE ||
                g_saveQueue.resultPending;
    SDL_mutexV(g_saveQueue.saveMutex);
    return busy;
}
static bool requestSaveState(int slot) {
    if (action_postponed.action != SAVE_NONE || saveSystemBusy()) {
        stateError("A save is still in progress. Please wait.");
        return false;
    }
    g_currentSlot = slot;
    action_postponed.cycles = 1;
    action_postponed.action = SAVE_STATE;
    return true;
}
static void clearStateScreenshot() {
    delete[] action_postponed.screenshot;
    action_postponed.screenshot = NULL;
}

bool GuardarCapturaPNG(const std::string& ruta, uint8_t* buffer, int w, int h, int bpp = 16) {
    if (!buffer) return false;
    try {

    const int total_pixels = w * h;

    // 1. Crear un vector para los datos RGB (3 bytes por p?xel)
    std::vector<unsigned char> rgb_buffer;
    rgb_buffer.resize(total_pixels * 3);

    if (bpp == 32) {
        // Convertir de XRGB8888 a RGB888
        const uint32_t* src = (const uint32_t*)buffer;
        for (int i = 0; i < total_pixels; ++i) {
            uint32_t pixel = src[i];
            rgb_buffer[i * 3 + 0] = (uint8_t)((pixel >> 16) & 0xFF); // R
            rgb_buffer[i * 3 + 1] = (uint8_t)((pixel >>  8) & 0xFF); // G
            rgb_buffer[i * 3 + 2] = (uint8_t)( pixel        & 0xFF); // B
        }
    } else {
        // Convertir de RGB565 a RGB888
        const uint16_t* src = (const uint16_t*)buffer;
        for (int i = 0; i < total_pixels; ++i) {
            uint16_t pixel = src[i];

            // Extraer componentes y expandir de 5/6 bits a 8 bits
            // Usamos desplazamiento y bitwise OR para mantener el brillo correcto
            uint8_t r = ((pixel >> 11) & 0x1F);
            uint8_t g = ((pixel >> 5) & 0x3F);
            uint8_t b = (pixel & 0x1F);

            rgb_buffer[i * 3 + 0] = (r << 3) | (r >> 2);
            rgb_buffer[i * 3 + 1] = (g << 2) | (g >> 4);
            rgb_buffer[i * 3 + 2] = (b << 3) | (b >> 2);
        }
    }

    // 3. Codificar y guardar el archivo en el HDD/USB de la Xbox
    // lodepng::encode devuelve 0 si tiene exito
    unsigned error = lodepng::encode(ruta, rgb_buffer, w, h, LCT_RGB, 8);
	Fileio::commit(ruta.c_str());

    if (error) {
        // En caso de error, puedes depurar con: lodepng_error_text(error)
        return false;
    }

    return true;
    } catch (const std::bad_alloc&) {
        // Preview failure must not turn a successfully written state into a fatal exit.
        return false;
    }
}


bool guardar_comprimido_zlib(const char* path, void *buffer, std::size_t size) {
    if (!path || !buffer || !size) return false;
    std::string temporary = std::string(path) + ".tmp";
    gzFile file = gzopen(temporary.c_str(), "wb1");
    if (!file) return false;
    bool ok = SalviaStateFile::write(file, buffer, size);
    ok = SalviaStateFile::finish(file, ok, temporary, path);
    if (ok) Fileio::commit(path);
    return ok;
}
std::string getSlotPath(const std::string& base, int slot) {
    return slot == 0 ? base : base + Constant::intToString(slot);
}

void loadSram(const char* sram_path) {
    std::size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void* data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);

    if (size > 0 && data) {
		std::string path = Constant::checkPath(sram_path);
		gzFile fp = gzopen(path.c_str(), "rb");
        if (fp) {
			gzread(fp, data, size);
            gzclose(fp);
        }
    }
}


void saveSram(const char* path) {
    void* data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!data || !size || saveSystemBusy()) return;
    if (g_sram_data_last && g_sram_size_last == size &&
        memcmp(data, g_sram_data_last, size) == 0) return;
    void* copy = malloc(size);
    if (!copy) return;
    memcpy(copy, data, size);
    if (g_sram_size_last != size) {
        free(g_sram_data_last);
        g_sram_data_last = malloc(size);
        g_sram_size_last = size;
    }
    if (g_sram_data_last) memcpy(g_sram_data_last, data, size);
    SDL_mutexP(g_saveQueue.saveMutex);
    g_saveQueue.buffer = copy;
    g_saveQueue.bufferSize = size;
    g_saveQueue.targetPath = path;
    g_saveQueue.romPath = romPaths.rompath;
    g_saveQueue.action = SAVE_SRAM;
    SDL_mutexV(g_saveQueue.saveMutex);
    SDL_SemPost(g_saveQueue.semaphore);
}

static void reportStateSaved(const std::string& path, int slot, bool imageOK,
                             const std::string& savedRom) {
    if (savedRom == romPaths.rompath)
        gameMenu->configMenus->poblarPartidasGuardadas(gameMenu->getCfgLoader(), savedRom);
    gameMenu->showSystemMessage(LanguageManager::instance()->get("msg.state.save") +
                               Constant::TipoToStr(slot), 3000);
    if (!imageOK)
        gameMenu->showSystemMessage(LanguageManager::instance()->get("msg.error.savestate.image") +
                                   path + STATE_IMG_EXT, 3000);
}
static void pollSaveResult() {
    if (!g_saveQueue.saveMutex) return;
    SDL_mutexP(g_saveQueue.saveMutex);
    bool pending = g_saveQueue.resultPending;
    bool ok = g_saveQueue.resultSuccess, imageOK = g_saveQueue.resultImage;
    ThreadAction action = g_saveQueue.resultAction;
    std::string path = g_saveQueue.resultPath, rom = g_saveQueue.resultRom;
    int slot = g_saveQueue.resultSlot;
    g_saveQueue.resultPending = false;
    SDL_mutexV(g_saveQueue.saveMutex);
    if (!pending) return;
    if (action == SAVE_STATE && ok) reportStateSaved(path, slot, imageOK, rom);
    else if (!ok) {
        if (action == SAVE_SRAM) {
            free(g_sram_data_last); g_sram_data_last = NULL; g_sram_size_last = 0;
        }
        gameMenu->showSystemMessage(LanguageManager::instance()->get(
            action == SAVE_STATE ? "msg.error.savestate" : "msg.error.sram") + path, 5000);
    }
}

static void waitSaveSystem() {
    if (!g_saveQueue.running) return;
    for (;;) {
        pollSaveResult();
        if (!saveSystemBusy()) return;
        SDL_Delay(1);
    }
}

/* Suspend callbacks only during synchronous FBNeo state I/O. Never hold the
 * SDL audio lock while compressing, accessing storage, or scanning the core. */
class StateAudioPause {
    bool resume;
public:
    StateAudioPause() : resume(SDL_GetAudioStatus() == SDL_AUDIO_PLAYING) {
        if (resume) SDL_PauseAudio(1);
    }
    ~StateAudioPause() {
        if (resume) {
            SDL_LockAudio();
            gameMenu->g_audioBuffer.Clear();
            SDL_UnlockAudio();
            SDL_PauseAudio(0);
        }
    }
};
static bool RETRO_CALLCONV streamStateWrite(void* context, void* data, size_t size) {
    return SalviaStateFile::write((gzFile)context, data, size);
}
static bool RETRO_CALLCONV streamStateRead(void* context, void* data, size_t size) {
    return SalviaStateFile::read((gzFile)context, data, size);
}

bool saveState() {
    pollSaveResult();
    if (saveSystemBusy()) {
        clearStateScreenshot(); stateError("A save is still in progress. Please retry.");
        return false;
    }
    size_t coreSize = retro_serialize_size();
    size_t raSize = 0;
    rc_client_t* client = Achievements::instance()->getClient();
    if (client) raSize = rc_client_progress_size(client);
    stateLog("save-start", coreSize, true);
    if (!coreSize || raSize > SalviaStateFile::MAX_RA_BYTES ||
        coreSize > (size_t)-1 - 8 - raSize) {
        clearStateScreenshot(); stateError("Unsupported state size.");
        stateLog("save-size", coreSize, false); return false;
    }
    const uint32_t raLength = (uint32_t)raSize;
    std::string target = getSlotPath(romPaths.savestate, g_currentSlot);
    FbneoStateStream stream = getFbneoStateStream();
    if (stream) {
        StateAudioPause audioPause;
        salvia_state_progress(false);
        std::string path = Constant::checkPath(target), temporary = path + ".tmp";
        // RA is small, but allocate it before starting so OOM preserves the old slot.
        void* raBuffer = raSize ? malloc(raSize) : NULL;
        if (raSize && !raBuffer) {
            clearStateScreenshot(); stateError("Not enough memory for achievements.");
            stateLog("save-ra-memory", raSize, false); return false;
        }
        if (raSize && rc_client_serialize_progress(client, (uint8_t*)raBuffer) != 0) {
            free(raBuffer); clearStateScreenshot();
            stateError("Could not serialize achievement progress.");
            stateLog("save-ra-serialize", raSize, false); return false;
        }
        gzFile file = gzopen(temporary.c_str(), "wb1");
        bool ok = file != NULL;
        stateLog("save-open", coreSize, ok);
        if (file) {
            ok = stream(0, coreSize, streamStateWrite, file);
            stateLog("save-stream", coreSize, ok);
            if (ok) ok = SalviaStateFile::write(file, "RCHV", 4) &&
                         SalviaStateFile::write(file, &raLength, 4) &&
                         SalviaStateFile::write(file, raBuffer, raSize);
            ok = SalviaStateFile::finish(file, ok, temporary, path);
            if (ok) Fileio::commit(path.c_str());
        }
        free(raBuffer);
        stateLog("save-commit", coreSize, ok);
        if (ok) {
            bool imageOK = action_postponed.screenshot && GuardarCapturaPNG(
                Constant::checkPath(target + STATE_IMG_EXT), action_postponed.screenshot,
                action_postponed.width, action_postponed.height, action_postponed.bpp);
            reportStateSaved(target, g_currentSlot, imageOK, romPaths.rompath);
        } else stateError("Could not save the state. Previous slot was kept.");
        clearStateScreenshot();
        return ok;
    }

    // Ordinary libretro path: transfer the SINGLE allocation to the worker.
    size_t total = coreSize + 8 + raSize;
    void* buffer = malloc(total);
    if (!buffer || !retro_serialize(buffer, coreSize)) {
        free(buffer); clearStateScreenshot();
        stateError("Not enough memory, or the core could not serialize.");
        stateLog("save-serialize", coreSize, false); return false;
    }
    uint8_t* trailer = (uint8_t*)buffer + coreSize;
    memcpy(trailer, "RCHV", 4); memcpy(trailer + 4, &raLength, 4);
    if (raSize && rc_client_serialize_progress(client, trailer + 8) != 0) {
        free(buffer); clearStateScreenshot();
        stateError("Could not serialize achievement progress."); return false;
    }
    SDL_mutexP(g_saveQueue.saveMutex);
    g_saveQueue.buffer = buffer;
    g_saveQueue.bufferSize = total;
    g_saveQueue.targetPath = target;
    g_saveQueue.romPath = romPaths.rompath;
    g_saveQueue.slot = g_currentSlot;
    g_saveQueue.width = action_postponed.width;
    g_saveQueue.height = action_postponed.height;
    g_saveQueue.bpp = action_postponed.bpp;
    g_saveQueue.screenshot = action_postponed.screenshot;
    action_postponed.screenshot = NULL;
    g_saveQueue.action = SAVE_STATE;
    SDL_mutexV(g_saveQueue.saveMutex);
    SDL_SemPost(g_saveQueue.semaphore);
    return true;
}

bool loadState() {
    pollSaveResult();
    cfg::t_cfg_props* cfg = gameMenu->getCfgLoader()->configMain;
    if (cfg[cfg::enableAchievements].valueBool && cfg[cfg::hardcoreRA].valueBool) {
        gameMenu->showLangSystemMessage("msg.error.hardcore.loadstate", 3000);
        return false;
    }
    if (saveSystemBusy()) {
        stateError("A save is still in progress. Please wait."); return false;
    }
    const std::string path = Constant::checkPath(getSlotPath(romPaths.savestate, g_currentSlot));
    size_t coreSize = retro_serialize_size();
    stateLog("load-start", coreSize, true);
    if (!coreSize) { stateError("The core does not support states."); return false; }
    gzFile file = gzopen(path.c_str(), "rb");
    if (!file) { stateError("Could not open the state file."); return false; }
    StateAudioPause audioPause;
    salvia_state_progress(true);
    uint32_t raSize = 0;
    bool ok = SalviaStateFile::validate(file, coreSize, raSize);
    stateLog("load-validate", coreSize, ok);
    if (!ok || gzrewind(file) != 0) {
        gzclose(file); stateError("Invalid or incomplete state. Game was not changed."); return false;
    }
    void* raBuffer = raSize ? malloc(raSize) : NULL;
    if (raSize && !raBuffer) {
        gzclose(file); stateError("Not enough memory for achievements."); return false;
    }
    FbneoStateStream stream = getFbneoStateStream();
    if (stream) ok = stream(1, coreSize, streamStateRead, file);
    else {
        void* buffer = malloc(coreSize);
        ok = buffer && SalviaStateFile::read(file, buffer, coreSize);
        if (ok) ok = retro_unserialize(buffer, coreSize);
        free(buffer);
    }
    stateLog("load-core", coreSize, ok);
    if (ok && raSize) {
        char marker[4]; uint32_t length = 0;
        ok = SalviaStateFile::read(file, marker, 4) && memcmp(marker, "RCHV", 4) == 0 &&
             SalviaStateFile::read(file, &length, 4) && length == raSize &&
             SalviaStateFile::read(file, raBuffer, raSize);
    }
    if (gzclose(file) != Z_OK) ok = false;
    if (ok) {
        rc_client_t* client = Achievements::instance()->getClient();
        if (client) rc_client_deserialize_progress(client, (const uint8_t*)raBuffer);
        gameMenu->showSystemMessage(LanguageManager::instance()->get("msg.state.load") +
                                   Constant::TipoToStr(g_currentSlot), 3000);
    } else stateError("The core or storage could not restore this state.");
    free(raBuffer);
    stateLog("load-done", coreSize, ok);
    return ok;
}

bool guardar_archivo_raw(const char* path, void* buffer, std::size_t size) {
    std::string temporary = std::string(path) + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool ok = fwrite(buffer, 1, size, file) == size;
    if (fflush(file) != 0) ok = false;
#if defined(_XBOX) || defined(_XBOX360)
    if (_commit(_fileno(file)) != 0) ok = false;
#endif
    if (fclose(file) != 0) ok = false;
    if (ok) ok = SalviaStateFile::replace(temporary, path);
    if (!ok) remove(temporary.c_str());
    return ok;
}

int SaveThreadFunc(void* data) {
    SaveData* sd = (SaveData*)data;
    for (;;) {
        SDL_SemWait(sd->semaphore);
        SDL_mutexP(sd->saveMutex);
        ThreadAction action = sd->action;
        if (action == SAVE_NONE) {
            bool stop = !sd->running;
            SDL_mutexV(sd->saveMutex);
            if (stop) break;
            continue;
        }
        std::string path = sd->targetPath, rom = sd->romPath;
        void* buffer = sd->buffer;
        size_t size = sd->bufferSize;
        uint8_t* screenshot = sd->screenshot;
        unsigned width = sd->width, height = sd->height;
        int bpp = sd->bpp, slot = sd->slot;
        // Detach BEFORE unlocking: only this worker owns these pointers now.
        sd->buffer = NULL; sd->screenshot = NULL; sd->bufferSize = 0;
        sd->action = SAVE_NONE; sd->busy = true;
        SDL_mutexV(sd->saveMutex);
        bool ok = false, imageOK = false;
        if (action == SAVE_SRAM)
            ok = guardar_archivo_raw(Constant::checkPath(path).c_str(), buffer, size);
        else if (action == SAVE_STATE) {
            ok = guardar_comprimido_zlib(Constant::checkPath(path).c_str(), buffer, size);
            if (ok && screenshot)
                imageOK = GuardarCapturaPNG(Constant::checkPath(path + STATE_IMG_EXT),
                                            screenshot, width, height, bpp);
        }
        free(buffer); delete[] screenshot;
        SDL_mutexP(sd->saveMutex);
        sd->busy = false;
        sd->resultPending = true; sd->resultAction = action;
        sd->resultSuccess = ok; sd->resultImage = imageOK;
        sd->resultPath = path; sd->resultRom = rom; sd->resultSlot = slot;
        bool stop = !sd->running;
        SDL_mutexV(sd->saveMutex);
        if (stop) break;
    }
    return 0;
}
#if defined(_XBOX) || defined(_XBOX360)
static DWORD WINAPI SaveThreadFuncWin32(LPVOID data) { return (DWORD)SaveThreadFunc(data); }
#endif

void initSaveSystem() {
    // All synchronization objects must exist before the worker can start.
    g_saveQueue.saveMutex = SDL_CreateMutex();
    g_saveQueue.semaphore = SDL_CreateSemaphore(0);
    if (!g_saveQueue.saveMutex || !g_saveQueue.semaphore) {
        stateError("Could not initialize the save worker."); return;
    }
    g_saveQueue.running = true;
#if defined(_XBOX) || defined(_XBOX360)
    g_saveQueue.thread = CreateThread(NULL, 1024 * 1024, SaveThreadFuncWin32,
        &g_saveQueue, CREATE_SUSPENDED, NULL);
    if (g_saveQueue.thread)
        Constant::setup_and_run_thread(g_saveQueue.thread, IO_THREAD, false);
#else
    g_saveQueue.thread = SDL_CreateThread(SaveThreadFunc, &g_saveQueue);
#endif
    if (!g_saveQueue.thread) {
        g_saveQueue.running = false;
        stateError("Could not start the save worker.");
    }
}
void deinitSaveSystem() {
    if (g_saveQueue.saveMutex) {
        SDL_mutexP(g_saveQueue.saveMutex);
        g_saveQueue.running = false;
        SDL_mutexV(g_saveQueue.saveMutex);
    }
    if (g_saveQueue.semaphore) SDL_SemPost(g_saveQueue.semaphore);
    if (g_saveQueue.thread) {
#if defined(_XBOX) || defined(_XBOX360)
        WaitForSingleObject(g_saveQueue.thread, INFINITE);
        CloseHandle(g_saveQueue.thread);
#else
        SDL_WaitThread(g_saveQueue.thread, NULL);
#endif
        g_saveQueue.thread = NULL;
    }
    pollSaveResult();
    if (g_saveQueue.saveMutex) SDL_DestroyMutex(g_saveQueue.saveMutex);
    if (g_saveQueue.semaphore) SDL_DestroySemaphore(g_saveQueue.semaphore);
    g_saveQueue.saveMutex = NULL; g_saveQueue.semaphore = NULL;
    free(g_saveQueue.buffer); g_saveQueue.buffer = NULL;
    delete[] g_saveQueue.screenshot; g_saveQueue.screenshot = NULL;
    clearStateScreenshot();
    free(g_sram_data_last); g_sram_data_last = NULL; g_sram_size_last = 0;
}
