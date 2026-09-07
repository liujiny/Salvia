/* Sintetizador General MIDI del frontend.  Ver midisynth.h para el reparto de
 * hilos y el porque del diseno.
 *
 * Esta es la UNICA unidad de compilacion que define TSF_IMPLEMENTATION: tsf.h
 * son 2000 lineas y no tienen por que colarse en el resto del proyecto, asi que
 * midisynth.h solo declara `struct tsf;` hacia delante. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utils/logger.h>

/* TSF_NO_STDIO quita tsf_load_filename y el stream de stdio, que no queremos:
 * el loader lee CAMPO A CAMPO (un stream->read por cada u16 de la hydra), o sea
 * decenas de miles de lecturas diminutas.  Contra el disco de la 360 eso es
 * lentisimo, asi que el fichero se lee entero de una vez aqui y se le pasa a
 * tsf_load_memory.
 *
 * Los TSF_MALLOC/REALLOC/FREE se dejan en los de stdlib a proposito: el guard de
 * upstream es "todos o ninguno" (`#if !defined(TSF_MALLOC) || !defined(TSF_FREE)
 * || !defined(TSF_REALLOC)`), y definir solo algunos acabaria liberando punteros
 * en el heap equivocado. */
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(push)
/* 4244 double->float en la conversion de muestras y en las envolventes,
 * 4127 el `if (0)` del manejador de sin-memoria, 4505 getters sin usar. */
#pragma warning(disable: 4244 4127 4505 4310)
#endif
/* `$(ProjectDir)\libs` ya esta en los include dirs de las dos plataformas, asi
 * que no hace falta anadir ninguno nuevo al vcxproj. */
#include <tinysoundfont/tsf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "midisynth.h"

/* Tope de voces simultaneas.  Se renderiza en el hilo de emulacion, asi que cada
 * voz sale del presupuesto de frame del core; en la 360 ademas cada muestra
 * pasa por una conversion float->int que en PPC es un viaje a memoria. */
#ifdef _XBOX
	#define MIDI_MAX_VOICES 32
#else
	#define MIDI_MAX_VOICES 96
#endif

/* Cuantos bytes de datos lleva un mensaje de canal. */
static inline uint8_t midiDataLen(uint8_t status)
{
	const uint8_t hi = (uint8_t)(status & 0xF0);
	/* 0xC0 program change y 0xD0 channel pressure llevan uno; el resto, dos. */
	return (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
}

MidiSynth::MidiSynth()
	: m_tsf(0), m_sampleRate(44100), m_volumePct(100), m_hasDrumBank(false), m_sawData(false),
	  m_haveState(false), m_dbgBytes(0), m_dbgNotes(0)
{
	memset(m_ch, 0, sizeof(m_ch));
	resetFraming();
}

MidiSynth::~MidiSynth()
{
	close();
}

void MidiSynth::resetFraming()
{
	m_running       = 0;
	m_status        = 0;
	m_data[0]       = 0;
	m_data[1]       = 0;
	m_dataLen       = 0;
	m_dataNeed      = 0;
	m_inSysex       = 0;
	m_sysexOverflow = 0;
	m_sysexLen      = 0;
}

bool MidiSynth::open(const std::string& sf2Path, int coreSampleRate)
{
	close();

	if (sf2Path.empty()) return false;

	FILE* f = fopen(sf2Path.c_str(), "rb");
	if (!f) {
		LOG_ERROR("MIDI: no se puede abrir el soundfont %s", sf2Path.c_str());
		return false;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0) {
		fclose(f);
		LOG_ERROR("MIDI: soundfont vacio %s", sf2Path.c_str());
		return false;
	}

	void* buf = malloc((size_t)size);
	if (!buf) {
		fclose(f);
		LOG_ERROR("MIDI: sin memoria para leer el soundfont (%ld bytes)", size);
		return false;
	}

	const size_t got = fread(buf, 1, (size_t)size, f);
	fclose(f);

	if (got != (size_t)size) {
		free(buf);
		LOG_ERROR("MIDI: lectura incompleta del soundfont %s", sf2Path.c_str());
		return false;
	}

	/* tsf_load_memory copia lo que necesita, asi que el buffer del fichero se
	 * suelta aqui mismo.  Lo que queda residente son las muestras ya expandidas
	 * a float: del orden del DOBLE del tamano del fichero. */
	m_tsf = tsf_load_memory(buf, (int)size);
	free(buf);

	if (!m_tsf) {
		LOG_ERROR("MIDI: el soundfont no se ha podido interpretar: %s", sf2Path.c_str());
		return false;
	}

	m_path       = sf2Path;
	m_sampleRate = (coreSampleRate > 0) ? coreSampleRate : 44100;
	m_sawData    = false;

	/* Banco 128 = percusion en GM.  Sin el, tsf_channel_set_presetnumber con
	 * flag_mididrums cae hacia atras a un preset melodico y el canal 10 suena
	 * como un piano aporreando la parte de bateria, que es PEOR que el silencio. */
	m_hasDrumBank = (tsf_get_presetindex(m_tsf, 128, 0) != -1);
	if (!m_hasDrumBank)
		LOG_INFO("MIDI: el soundfont no trae banco de percusion (bank 128); se silencia el canal 10");

	tsf_set_output(m_tsf, TSF_STEREO_INTERLEAVED, m_sampleRate, 0.0f);
	tsf_set_max_voices(m_tsf, MIDI_MAX_VOICES);
	setVolumePercent(m_volumePct);

	resetFraming();

	/* Si la cancion en curso ya habia configurado los canales (cambio de banco
	 * en caliente), esa configuracion se ha ido con la instancia anterior: la
	 * nueva no va a recibir otra vez los program change hasta la proxima
	 * cancion.  Se guarda antes de reiniciar y se reaplica despues, para que
	 * cambiar de SoundFont no altere instrumentos ni volumenes.
	 *
	 * Sin esto, recargar a mitad de partida dejaba TODOS los canales en el
	 * preset 0 (piano) y a volumen por defecto, que suena mas fuerte pero es la
	 * partitura mal instrumentada. */
	{
		ChannelState saved[MIDI_CHANNELS];
		const bool   hadState = m_haveState;
		memcpy(saved, m_ch, sizeof(saved));

		resetChannels(true);

		if (hadState) {
			memcpy(m_ch, saved, sizeof(m_ch));
			reapplyChannels();
		}
	}

	LOG_INFO("MIDI: soundfont cargado (%s, %ld KB, %d presets, %d voces max)",
	         sf2Path.c_str(), size / 1024, tsf_get_presetcount(m_tsf), MIDI_MAX_VOICES);
	return true;
}

void MidiSynth::close()
{
	if (m_tsf) {
		tsf_close(m_tsf);
		m_tsf = 0;
	}
	m_path.clear();
	m_sawData     = false;
	m_hasDrumBank = false;
	resetFraming();
}

void MidiSynth::setSampleRate(int coreSampleRate)
{
	if (coreSampleRate <= 0) return;
	m_sampleRate = coreSampleRate;
	if (!m_tsf) return;
	/* Solo reescribe la tasa de salida y la ganancia; las voces vivas se
	 * reafinan solas en el siguiente bloque. */
	tsf_set_output(m_tsf, TSF_STEREO_INTERLEAVED, m_sampleRate, 0.0f);
	setVolumePercent(m_volumePct);
}

void MidiSynth::setVolumePercent(int pct)
{
	if (pct < 0)   pct = 0;
	if (pct > 100) pct = 100;
	m_volumePct = pct;
	if (!m_tsf) return;
	/* tsf_set_volume espera un FACTOR lineal (1.0 = 100%) y hace 1.0/factor por
	 * dentro, asi que el cero hay que atajarlo antes de que divida. */
	tsf_set_volume(m_tsf, (pct <= 0) ? 0.0001f : ((float)pct / 100.0f));
}

void MidiSynth::panic()
{
	resetFraming();
	if (!m_tsf) return;
	for (int ch = 0; ch < MIDI_CHANNELS; ++ch)
		tsf_channel_sounds_off_all(m_tsf, ch);
	m_sawData = false;
}

/* Descomentar para volcar por el log, una vez por segundo, que esta recibiendo
 * el sintetizador y en que estado esta: sirve para separar "el core no manda
 * bytes", "los manda pero no se convierten en notas" y "hay notas sonando pero
 * el canal esta a volumen cero". */
/* #define MIDI_SYNTH_DEBUG 1 */

void MidiSynth::render(int16_t* interleavedStereo, int frames)
{
	if (!m_tsf || frames <= 0) return;

#ifdef MIDI_SYNTH_DEBUG
	{
		static int      dbg_frames = 0;
		static uint32_t dbg_bytes  = 0;
		static uint32_t dbg_notes  = 0;

		dbg_frames += frames;
		if (dbg_frames >= m_sampleRate)   /* ~1 s de audio renderizado */
		{
			LOG_INFO("[MIDI-SYNTH] bytes=%u notas_on=%u voces=%d vol=%d%% "
			         "ch0[prog=%d vol=%.2f pan=%.2f] ch1[prog=%d vol=%.2f]",
			         (unsigned)(m_dbgBytes - dbg_bytes),
			         (unsigned)(m_dbgNotes - dbg_notes),
			         tsf_active_voice_count(m_tsf), m_volumePct,
			         tsf_channel_get_preset_number(m_tsf, 0),
			         tsf_channel_get_volume(m_tsf, 0),
			         tsf_channel_get_pan(m_tsf, 0),
			         tsf_channel_get_preset_number(m_tsf, 1),
			         tsf_channel_get_volume(m_tsf, 1));
			dbg_bytes  = m_dbgBytes;
			dbg_notes  = m_dbgNotes;
			dbg_frames = 0;
		}
	}
#endif
	/* flag_mixing = 1: suma sobre lo que ya hay y satura a [-32768, 32767], que
	 * es justo el mezclador que hace falta y que el frontend no tiene. */
	tsf_render_short(m_tsf, interleavedStereo, frames, 1);
}

/* ------------------------------------------------------------------------- */
/* Maquina de trama del stream MIDI                                          */
/* ------------------------------------------------------------------------- */

void MidiSynth::writeByte(uint8_t b)
{
	if (!m_tsf) return;
	m_sawData = true;
	m_dbgBytes++;

	/* 1. Tiempo real (0xF8..0xFF).  Pueden aparecer EN MEDIO de cualquier
	 *    mensaje, incluso entre un status y sus datos, asi que no pueden tocar
	 *    m_status, m_running, m_dataLen ni el sysex.
	 *
	 *    0xFF (System Reset) se ignora a proposito: prboom y dosbox-pure meten
	 *    ficheros MIDI por su propio secuenciador, y un meta-evento de SMF que
	 *    se escape (FF <tipo> <len> ...) provocaria un reset a mitad de cancion
	 *    y ademas soltaria su carga como bytes de datos sueltos.  Los resets de
	 *    verdad llegan como SysEx GM/GS/XG, que no son ambiguos. */
	if (b >= 0xF8)
		return;

	/* 2. Cuerpo de un SysEx. */
	if (m_inSysex) {
		if (b < 0x80) {
			if (m_sysexLen < SYSEX_MAX) m_sysex[m_sysexLen++] = b;
			else                        m_sysexOverflow = 1;
			return;
		}
		m_inSysex = 0;                       /* cualquier status lo termina */
		if (!m_sysexOverflow) handleSysex();
		m_sysexOverflow = 0;
		if (b == 0xF7) return;               /* EOX normal */
		/* Malformado: un status de verdad ha cortado el SysEx.  Se sigue hacia
		 * abajo para procesarlo en vez de tirarlo. */
	}

	/* 3. Byte de status (0x80..0xF7). */
	if (b & 0x80) {
		if (b < 0xF0) {                      /* mensaje de canal */
			m_status = m_running = b;
			m_dataNeed = midiDataLen(b);
			m_dataLen  = 0;
			return;
		}
		/* System common: CANCELA el running status. */
		m_running = 0;
		m_dataLen = 0;
		switch (b) {
			case 0xF0: m_inSysex = 1; m_sysexLen = 0; m_sysexOverflow = 0; m_status = 0; return;
			case 0xF1: m_status = b; m_dataNeed = 1; return;  /* MTC quarter frame */
			case 0xF2: m_status = b; m_dataNeed = 2; return;  /* song position */
			case 0xF3: m_status = b; m_dataNeed = 1; return;  /* song select */
			default:   m_status = 0; return;                  /* F4/F5 sin definir, F6 tune, F7 suelto */
		}
	}

	/* 4. Byte de datos. */
	if (m_status == 0) {
		/* Running status: un byte de datos sin status nuevo repite el ultimo
		 * status DE CANAL.  m_status == 0 es justo el estado "a la espera". */
		if (m_running == 0) return;          /* dato huerfano */
		m_status   = m_running;
		m_dataNeed = midiDataLen(m_status);
		m_dataLen  = 0;
	}

	m_data[m_dataLen++] = b;
	if (m_dataLen < m_dataNeed) return;

	if (m_status < 0xF0)
		applyChannelMessage(m_status, m_data[0], (uint8_t)(m_dataNeed > 1 ? m_data[1] : 0));
	/* F1/F2/F3 se consumen y se tiran: TSF no tiene concepto de transporte. */

	m_dataLen = 0;
	m_status  = 0;   /* vuelve a "a la espera"; m_running sigue armado */
}

void MidiSynth::applyChannelMessage(uint8_t st, uint8_t d0, uint8_t d1)
{
	const int ch = st & 0x0F;
	switch (st & 0xF0) {
		case 0x80:                                   /* note off */
			tsf_channel_note_off(m_tsf, ch, d0);
			break;
		case 0x90:                                   /* note on (velocidad 0 = note off) */
			if (d1 == 0) tsf_channel_note_off(m_tsf, ch, d0);
			else       { tsf_channel_note_on(m_tsf, ch, d0, (float)d1 / 127.0f); m_dbgNotes++; }
			break;
		case 0xA0:                                   /* aftertouch polifonico: TSF no lo modela */
			break;
		case 0xB0:
			/* Anotar los controladores que hay que poder reaplicar si se recarga
			 * el banco a mitad de cancion (ver open()). */
			if      (d0 ==  7) { m_ch[ch].volume     = d1; m_haveState = true; }
			else if (d0 == 10) { m_ch[ch].pan        = d1; m_haveState = true; }
			else if (d0 == 11) { m_ch[ch].expression = d1; m_haveState = true; }
			/* TODOS los control change se reenvian tal cual.  TSF ya implementa
			 * el modelo completo (bank select 0/32, volumen 7/39, pan 10/42,
			 * expresion 11/43, sustain 64, RPN 100/101 + data entry 6/38,
			 * 120/121/123..127), y ademas guarda el banco marcado con 0x8000
			 * para que lo consuma el siguiente program change.  Interceptar
			 * cualquiera de estos aqui desincronizaria su estado interno. */
			tsf_channel_midi_control(m_tsf, ch, d0, d1);
			break;
		case 0xC0:                                   /* program change */
			applyProgram(ch, d0);
			break;
		case 0xD0:                                   /* aftertouch de canal */
			break;
		case 0xE0:
			/* Pitch bend: 14 bits, LSB primero.  Esto es el orden de bytes DEL
			 * PROTOCOLO, no la endianness de la CPU: la expresion es aritmetica
			 * sobre int y vale igual en x86 y en PPC.  No "arreglarla". */
			m_ch[ch].pitchwheel = (uint16_t)((int)d0 | ((int)d1 << 7));
			m_haveState = true;
			tsf_channel_set_pitchwheel(m_tsf, ch, (int)m_ch[ch].pitchwheel);
			break;
	}
}

void MidiSynth::applyProgram(int ch, uint8_t program)
{
	if (ch < 0 || ch >= MIDI_CHANNELS) return;
	m_ch[ch].program = program;
	m_haveState      = true;
	tsf_channel_set_presetnumber(m_tsf, ch, program, m_ch[ch].isDrum ? 1 : 0);
}

void MidiSynth::resetChannels(bool hardReset)
{
	if (!m_tsf) return;
	if (hardReset) tsf_reset(m_tsf);

	for (int ch = 0; ch < MIDI_CHANNELS; ++ch) {
		m_ch[ch].isDrum     = (ch == DRUM_CHANNEL) ? 1 : 0;
		m_ch[ch].program    = 0;
		m_ch[ch].volume     = 100;   /* valores por defecto de GM */
		m_ch[ch].expression = 127;
		m_ch[ch].pan        = 64;
		m_ch[ch].pitchwheel = 8192;

		tsf_channel_set_bank_preset(m_tsf, ch, m_ch[ch].isDrum ? 128 : 0, 0);
		tsf_channel_midi_control(m_tsf, ch,   7, 100);   /* volumen por defecto GM */
		tsf_channel_midi_control(m_tsf, ch,  11, 127);   /* expresion */
		tsf_channel_midi_control(m_tsf, ch,  10,  64);   /* pan centrado */
		tsf_channel_midi_control(m_tsf, ch,  64,   0);   /* sustain suelto */
		tsf_channel_set_pitchwheel(m_tsf, ch, 8192);
		tsf_channel_set_pitchrange(m_tsf, ch, 2.0f);     /* +/- 2 semitonos, GM */
	}

	/* Sin banco de percusion, callar el canal 10 antes que dejarlo tocar la
	 * parte de bateria con un instrumento melodico. */
	if (!m_hasDrumBank)
		tsf_channel_midi_control(m_tsf, DRUM_CHANNEL, 7, 0);
}

/* Vuelca m_ch[] al sintetizador.  Se usa tras reabrir el banco en caliente para
 * que la instancia nueva quede como estaba la anterior; el orden importa: los
 * controladores primero y el program change al final, porque este ultimo
 * resuelve el preset con el banco ya asentado. */
void MidiSynth::reapplyChannels()
{
	if (!m_tsf) return;
	for (int ch = 0; ch < MIDI_CHANNELS; ++ch) {
		tsf_channel_midi_control(m_tsf, ch,  7, m_ch[ch].volume);
		tsf_channel_midi_control(m_tsf, ch, 11, m_ch[ch].expression);
		tsf_channel_midi_control(m_tsf, ch, 10, m_ch[ch].pan);
		tsf_channel_set_pitchwheel(m_tsf, ch, (int)m_ch[ch].pitchwheel);
		applyProgram(ch, m_ch[ch].program);
	}
	if (!m_hasDrumBank)
		tsf_channel_midi_control(m_tsf, DRUM_CHANNEL, 7, 0);
}

void MidiSynth::handleSysex()
{
	const uint8_t* s = m_sysex;
	const uint16_t n = m_sysexLen;

	/* GM1 / GM2 System On, GM System Off:  F0 7E <dev> 09 <01|02|03> F7 */
	if (n >= 4 && s[0] == 0x7E && s[2] == 0x09) {
		if (s[3] == 0x01 || s[3] == 0x02 || s[3] == 0x03) { resetChannels(true); return; }
	}

	/* Roland GS Reset:  F0 41 <dev> 42 12 40 00 7F 00 41 F7 */
	if (n >= 9 && s[0] == 0x41 && s[2] == 0x42 && s[3] == 0x12 &&
	    s[4] == 0x40 && s[5] == 0x00 && s[6] == 0x7F && s[7] == 0x00) {
		resetChannels(true);
		return;
	}

	/* Roland GS "part to rhythm":  F0 41 <dev> 42 12 40 1<parte> 15 <mapa> <sum> F7
	 * Es lo que usa un juego para mover la percusion fuera del canal 10. */
	if (n >= 9 && s[0] == 0x41 && s[2] == 0x42 && s[3] == 0x12 &&
	    s[4] == 0x40 && (s[5] & 0xF0) == 0x10 && s[6] == 0x15) {
		const int part = s[5] & 0x0F;
		const int ch   = (part == 0) ? 9 : (part <= 9 ? part - 1 : part);
		if (ch >= 0 && ch < MIDI_CHANNELS) {
			m_ch[ch].isDrum = (s[7] != 0) ? 1 : 0;
			applyProgram(ch, m_ch[ch].program);   /* re-resolver con el flag nuevo */
		}
		return;
	}

	/* Yamaha XG System On:  F0 43 1<dev> 4C 00 00 7E 00 F7 */
	if (n >= 7 && s[0] == 0x43 && (s[1] & 0xF0) == 0x10 && s[2] == 0x4C &&
	    s[3] == 0x00 && s[4] == 0x00 && s[5] == 0x7E) {
		resetChannels(true);
		return;
	}

	/* Todo lo demas (parches MT-32, volcados masivos, datos de fabricante) se
	 * ignora: no hay nada que TSF pueda hacer con ello. */
}
