#pragma once

#include <stdint.h>
#include <string.h> // memcpy, memset
#include <utils/logger.h>
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
#include <audio/audiotempo.h>
#endif

#ifdef _XBOX
#define BUFF_SIZE 16384
#include <xtl.h>
#else
#define BUFF_SIZE 8192
#include <windows.h>
#endif

// Ring buffer de audio, un productor (callback de audio del core) y un
// consumidor (callback de SDL).  head/tail son volatile long: el ring es
// lock-free (un solo productor, un solo consumidor).
//
// Gestion de underrun (buffer sin datos suficientes) por RAMPA DE GANANCIA:
// en vez de conmutar entre "audio" y "silencio" (lo que produce clicks o
// pausas), la salida se multiplica por una ganancia que se mueve como mucho
// GAIN_STEP por frame.  Al haber datos la ganancia sube hacia pleno; al
// faltar, baja hacia cero manteniendo la ultima muestra.  Como la ganancia
// nunca salta, la senal es SIEMPRE continua -> es imposible que suene un
// click, se llene el buffer como se llene.  Un bajon breve de framerate solo
// baja un pelin el volumen (inaudible), no genera un silencio; un underrun
// largo se desvanece limpio a silencio y vuelve con un fundido de entrada.
class AudioBuffer {
private:
    int16_t buffer[BUFF_SIZE];
    volatile long head;
    volatile long tail;
    HANDLE hSpaceEvent;  // Señalizado cuando Read() libera espacio

#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
    bool tempoEnabled, tempoRunning;
    AudioTempo tempo;
    struct TempoSource {
        AudioBuffer& owner;
        TempoSource(AudioBuffer& buffer) : owner(buffer) {}
        size_t available() const {
            long h = InterlockedCompareExchange(&owner.head, 0, 0);
            return owner.used(h, owner.tail) / 2;
        }
        void copy(size_t offset, int16_t* dst, size_t frames) {
            owner.copyOut(((size_t)owner.tail + offset * 2) % capacity,
                          dst, frames * 2);
        }
        void consume(size_t frames) {
            InterlockedExchange(&owner.tail,
                (LONG)(((size_t)owner.tail + frames * 2) % capacity));
        }
    };
#endif

    void publishHead(long value) {
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        if (tempoEnabled) { InterlockedExchange(&head, value); return; }
#endif
        head = value;
    }

    long readTail() const {
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        // Xenon requires acquire/release ordering, not only volatile, when
        // samples cross between the emulation and SDL callback threads.
        if (tempoEnabled) return InterlockedCompareExchange(
            const_cast<volatile long*>(&tail), 0, 0);
#endif
        return tail;
    }

    // Telemetria (volatile long, incrementada via Interlocked desde el
    // productor y leida desde el hilo principal sin race).
    //   dropsTotal:     muestras descartadas por overflow en Write() no
    //                   bloqueante (productor mas rapido que consumidor).
    //   underrunsTotal: callbacks que no tenian datos suficientes (productor
    //                   mas lento: CPU stall, CD slow read, core lento...).
    volatile long dropsTotal;
    volatile long underrunsTotal;

    // Ultima muestra real entregada (L/R).  Durante un hueco se mantiene esta
    // muestra y la ganancia la desvanece, para que la transicion sea continua.
    int16_t lastOutL;
    int16_t lastOutR;

    // Ganancia de reproduccion en punto fijo [0 .. GAIN_ONE].  Attack/release
    // simetricos de GAIN_STEP por frame -> rampa de ~5 ms de extremo a extremo.
    int gain;
    static const int GAIN_SHIFT = 12;
    static const int GAIN_ONE   = 1 << GAIN_SHIFT;   // 4096 = volumen pleno
    static const int GAIN_STEP  = 16;                // 4096/16 = 256 frames ~ 5.3 ms @48k

    static const size_t capacity = BUFF_SIZE;

    size_t used(long h, long t) const {
        return (h >= t) ? (size_t)(h - t) : capacity - (size_t)(t - h);
    }

    size_t copyIn(size_t pos, const int16_t* src, size_t count) {
        size_t first = capacity - pos;
        if (first >= count) {
            memcpy(buffer + pos, src, count * sizeof(int16_t));
        } else {
            memcpy(buffer + pos, src, first * sizeof(int16_t));
            memcpy(buffer, src + first, (count - first) * sizeof(int16_t));
        }
        return (pos + count) % capacity;
    }

    size_t copyOut(size_t pos, int16_t* dst, size_t count) {
        size_t first = capacity - pos;
        if (first >= count) {
            memcpy(dst, buffer + pos, count * sizeof(int16_t));
        } else {
            memcpy(dst, buffer + pos, first * sizeof(int16_t));
            memcpy(dst + first, buffer, (count - first) * sizeof(int16_t));
        }
        return (pos + count) % capacity;
    }

public:
    AudioBuffer() : head(0), tail(0), dropsTotal(0), underrunsTotal(0),
                    lastOutL(0), lastOutR(0), gain(0) {
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        tempoEnabled = tempoRunning = false;
#endif
        memset(buffer, 0, sizeof(buffer));
        // Auto-reset: vuelve a no-señalizado tras despertar un hilo
        hSpaceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    }

    ~AudioBuffer() {
        if (hSpaceEvent) CloseHandle(hSpaceEvent);
    }

    // Escritura no bloqueante: descarta si no hay espacio suficiente.
    // Devuelve el numero de muestras escritas realmente (puede ser < count
    // si hubo overflow).  Las descartadas se contabilizan en dropsTotal.
    size_t Write(const int16_t* samples, size_t count) {
        long h = head;
        size_t free_space = capacity - used(h, readTail()) - 1;
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        if (tempoEnabled) free_space &= ~(size_t)1; // Never split a stereo frame.
#endif
        size_t to_write = (count > free_space) ? free_space : count;

        if (to_write < count) {
#ifdef AUDIO_LOG
            LOG_DEBUG("[ABUF] DROP: requested=%lu written=%lu dropped=%lu fill=%lu/%lu",
                (unsigned long)count, (unsigned long)to_write,
                (unsigned long)(count - to_write),
                (unsigned long)used(h, tail), (unsigned long)capacity);
#endif
            InterlockedExchangeAdd(&dropsTotal, (LONG)(count - to_write));
        }
        if (to_write == 0) return 0;

        publishHead((long)copyIn((size_t)h, samples, to_write));
        return to_write;
    }

    // Escritura bloqueante: duerme el hilo hasta que Read() libere espacio.
    //
    // Robusta a count > capacity-1: el ring solo admite capacity-1 muestras
    // (1 slot sentinel para distinguir vacio/lleno), asi que esperar por
    // count > capacity-1 seria un deadlock matematico.  Para soportar rafagas
    // grandes (p.ej. la primera tras boot del PSX, ~93 ms) troceamos el write
    // en chunks de hasta capacity-1 y esperamos espacio entre chunks.  El
    // consumidor drena y SetEvent(hSpaceEvent) en cada Read despierta la espera.
    void WriteBlocking(const int16_t* samples, size_t count) {
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        if (tempoEnabled) {
            count &= ~(size_t)1;
            while (count) {
                size_t free = (capacity - used(head, readTail()) - 1) & ~(size_t)1;
                if (!free) { WaitForSingleObject(hSpaceEvent, 2); continue; }
                size_t n = count < free ? count : free;
                publishHead((long)copyIn((size_t)head, samples, n));
                samples += n;
                count -= n;
            }
            return;
        }
#endif
        const size_t max_chunk = capacity - 1;

        while (count > 0) {
            size_t want = (count > max_chunk) ? max_chunk : count;

            while (capacity - used(head, readTail()) - 1 < want) {
                WaitForSingleObject(hSpaceEvent, 2);
            }

            publishHead((long)copyIn((size_t)head, samples, want));
            samples += want;
            count   -= want;
        }
    }

    // Lectura desde el callback de SDL.  `count` es numero de int16_t totales
    // (stereo intercalado), no frames; SDL siempre pide multiplos de 2.
    void Read(int16_t* stream, size_t count) {
        if (count == 0) return;

        size_t t     = (size_t)tail;
        long publishedHead = head;
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        if (tempoEnabled) publishedHead = InterlockedCompareExchange(&head, 0, 0);
#endif
        size_t avail = used(publishedHead, (long)t);
        size_t to_copy = (avail < count) ? avail : count;   // muestras reales disponibles

#ifdef AUDIO_LOG
        {   // Traza periodica del nivel de llenado
            static DWORD lastTrace = 0;
            DWORD now = GetTickCount();
            if (now - lastTrace >= 2000) {
                lastTrace = now;
                LOG_DEBUG("[ABUF] fill=%lu/%lu drops=%ld underruns=%ld",
                    (unsigned long)avail, (unsigned long)capacity,
                    dropsTotal, underrunsTotal);
            }
        }
#endif

#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        if (tempoEnabled && tempoRunning) {
            TempoSource source(*this);
            to_copy = tempo.read(source, stream, count / 2) * 2;
        } else
#endif
        if (to_copy > 0) {
            t = copyOut(t, stream, to_copy);
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
            if (tempoEnabled) InterlockedExchange(&tail, (LONG)t);
            else
#endif
                tail = (long)t;
        }
        if (to_copy < count)
            InterlockedIncrement(&underrunsTotal);

        // Camino rapido: buffer sano y volumen ya pleno -> los datos ya estan
        // en su sitio, sin coste por muestra.
        if (to_copy == count && gain == GAIN_ONE) {
            lastOutL = stream[count - 2];
            lastOutR = stream[count - 1];
            SetEvent(hSpaceEvent);
            return;
        }

        // Rampa de ganancia. Los frames con datos suben la ganancia hacia
        // pleno; los que faltan mantienen la ultima muestra real y la bajan
        // hacia cero.  Continuo por construccion -> nunca hay click.
        size_t frames = count / 2;
        size_t haveFr = to_copy / 2;
        int gL = lastOutL, gR = lastOutR;
        int g  = gain;
        for (size_t i = 0; i < frames; i++) {
            int sL, sR;
            if (i < haveFr) {
                sL = stream[i * 2]; sR = stream[i * 2 + 1];
                gL = sL; gR = sR;                       // recuerda la ultima muestra real
                g += GAIN_STEP; if (g > GAIN_ONE) g = GAIN_ONE;
            } else {
                sL = gL; sR = gR;                       // mantiene la ultima durante el hueco
                g -= GAIN_STEP; if (g < 0) g = 0;
            }
            stream[i * 2]     = (int16_t)((sL * g) >> GAIN_SHIFT);
            stream[i * 2 + 1] = (int16_t)((sR * g) >> GAIN_SHIFT);
        }
        gain     = g;
        lastOutL = (int16_t)gL;
        lastOutR = (int16_t)gR;

        SetEvent(hSpaceEvent);
    }

#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
    // Configure only while SDL_LockAudio is held, or while its device is paused.
    // Other cores keep the original read/write path.
    void ConfigureTempo(bool enabled) {
        tempoEnabled = enabled;
        tempoRunning = false;
        tempo.clear();
    }

    // SDL callback only. A pause/fast-forward transition discards queued game
    // sound and the overlap history, so a later game cannot inherit old audio.
    void SetTempoPlayback(bool running) {
        if (!tempoEnabled || tempoRunning == running) return;
        tempoRunning = running;
        tempo.resetStream();
        InterlockedExchange(&tail, InterlockedCompareExchange(&head, 0, 0));
        lastOutL = lastOutR = 0;
        gain = 0;
        SetEvent(hSpaceEvent);
    }
    bool HasTempo() const { return tempoEnabled; }
    AudioTempo::Stats getTempoStats() const { return tempo.getStats(); }
#endif

    // === Telemetria === (contadores acumulados, no reseteables salvo Clear())
    long getDropsTotal()     const { return dropsTotal; }
    long getUnderrunsTotal() const { return underrunsTotal; }
    size_t getUsed() const {
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        if (tempoEnabled) {
            long h = InterlockedCompareExchange(const_cast<volatile long*>(&head), 0, 0);
            return used(h, readTail());
        }
#endif
        return used(head, tail);
    }
    size_t getCapacity() const { return capacity; }

    // Vacia el buffer entre cargas de juego (solo seguro con el callback de
    // SDL en pausa, SDL_PauseAudio(1), sino tail se modifica concurrentemente).
    // Resetea telemetria y ganancia para que el juego nuevo empiece limpio y
    // con un fundido de entrada desde silencio.
    void Clear() {
        head = 0;
        tail = 0;
        dropsTotal = 0;
        underrunsTotal = 0;
        lastOutL = 0;
        lastOutR = 0;
        gain = 0;
#if defined(_XBOX) || defined(SALVIA_AUDIO_TEST)
        tempoRunning = false;
        tempo.clear();
#endif
        memset(buffer, 0, sizeof(buffer));
    }
};
