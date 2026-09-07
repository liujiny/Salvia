#pragma once

#include <stdint.h>

/* Traduccion Roland MT-32 (LA) -> General MIDI.
 *
 * POR QUE HACE FALTA
 *
 * El catalogo MIDI del X68000 es casi todo MT-32/CM-64, no GM (las notas del
 * propio px68k lo dicen: libretro/px68k/game_notes.md -- Gemini Wing ofrece
 * MT-32, CM-32L y CM-64; Granada y Sol-Feace activan el MIDI con la tecla
 * touroku).  Esos juegos mandan numeros de programa DEL MT-32, que no
 * coinciden con los de GM: el timbre 8 del MT-32 es un organo y el 8 de GM es
 * una celesta.  Sin traducir, un banco GM toca la partitura con los
 * instrumentos equivocados.
 *
 * PROCEDENCIA -- NO INVENTAR ENTRADAS
 *
 * Generadas de ScummVM, engines/sci/sound/drivers/map-mt32-to-gm.h (GPL):
 *   MT32_TO_GM_PROGRAM  <- Mt32PresetTimbreMaps (campo gmInstr)
 *   MT32_RHYTHM_KEY     <- Mt32PresetRhythmKeymap
 *
 * Se eligio esta y no la Mt32ToGm generica de audio/mididrv.cpp por dos
 * razones: trae el NOMBRE de cada timbre del MT-32, asi que cada entrada es
 * verificable de un vistazo (van abajo como comentario); y sus elecciones son
 * literales por nombre -- ElecPiano1 va al Electric Piano 1 de GM, no al
 * Electric Grand.  Ademas coincide con un mapa de MIDI-OX de origen
 * independiente en justo la zona donde la generica discrepaba.
 *
 * La tabla de ritmo esta CORROBORADA por una segunda fuente independiente:
 * FreeSCI (src/sfx/midi_mt32.c, ~2002) trae la misma tabla byte a byte, con
 * -1 donde aqui hay 0xFF, y su codigo documenta la semantica -- si la tecla no
 * esta mapeada, la nota SE DESCARTA:
 *
 *     if (rhythmkey_map[note] == -1) return 0;
 *     else buffer[1] = rhythmkey_map[note];
 *
 * Fijate en que la tabla de ritmo es casi la IDENTIDAD (35..51 y 60..75 van a
 * si mismas).  No es una conversion: la numeracion de teclas de percusion del
 * MT-32 ya coincide con la de GM, y lo que aporta la tabla es saber CUALES no
 * tienen equivalente para no tocar un sonido equivocado en su lugar.
 *
 * QUE NO CUBRE
 *
 * - Los timbres personalizados.  Muchos juegos de MT-32 no usan los presets de
 *   fabrica: los suben por SysEx.  Ninguna tabla arregla eso; haria falta
 *   emular el LA32 (munt), descartado por CPU en la 360 y por depender de ROMs.
 * - El mapa de ritmo propio del juego.  FreeSCI lo lee del SysEx de
 *   configuracion (memcpy desde el offset 384) y sustituye la tabla por
 *   defecto.  Aqui no se parsea ese SysEx, asi que se usa siempre la de fabrica.
 * - Las tres entradas MIDI_MAPPED_TO_RHYTHM del original (114 Deep Snare,
 *   119 Cymbal, 120 Castanets): son percusion tocada en un canal melodico, y
 *   ScummVM las desvia al canal de bateria.  Aqui se resuelven con los
 *   programas de percusion melodica de GM.  ESAS TRES SON JUICIO PROPIO, no
 *   vienen de ScummVM, y van marcadas con [propio] mas abajo.
 *
 * Un numero mal puesto no da error: da musica silenciosamente equivocada, que
 * es de lo mas caro de localizar.  Si hay que retocar algo, contra una fuente
 * publicada.
 */

/* Timbre de fabrica del MT-32 -> programa de GM. */
static const uint8_t MT32_TO_GM_PROGRAM[128] = {
	/*   0 AcouPiano1 */   0,
	/*   1 AcouPiano2 */   1,
	/*   2 AcouPiano3 */   0,
	/*   3 ElecPiano1 */   4,
	/*   4 ElecPiano2 */   5,
	/*   5 ElecPiano3 */   4,
	/*   6 ElecPiano4 */   5,
	/*   7 Honkytonk  */   3,
	/*   8 Elec Org 1 */  16,
	/*   9 Elec Org 2 */  17,
	/*  10 Elec Org 3 */  18,
	/*  11 Elec Org 4 */  18,
	/*  12 Pipe Org 1 */  19,
	/*  13 Pipe Org 2 */  19,
	/*  14 Pipe Org 3 */  20,
	/*  15 Accordion  */  21,
	/*  16 Harpsi 1   */   6,
	/*  17 Harpsi 2   */   6,
	/*  18 Harpsi 3   */   6,
	/*  19 Clavi 1    */   7,
	/*  20 Clavi 2    */   7,
	/*  21 Clavi 3    */   7,
	/*  22 Celesta 1  */   8,
	/*  23 Celesta 2  */   8,
	/*  24 Syn Brass1 */  62,
	/*  25 Syn Brass2 */  63,
	/*  26 Syn Brass3 */  62,
	/*  27 Syn Brass4 */  63,
	/*  28 Syn Bass 1 */  38,
	/*  29 Syn Bass 2 */  39,
	/*  30 Syn Bass 3 */  38,
	/*  31 Syn Bass 4 */  39,
	/*  32 Fantasy    */  88,
	/*  33 Harmo Pan  */  89,
	/*  34 Chorale    */  52,
	/*  35 Glasses    */  98,
	/*  36 Soundtrack */  97,
	/*  37 Atmosphere */  99,
	/*  38 Warm Bell  */  89,
	/*  39 Funny Vox  */  85,
	/*  40 Echo Bell  */  39,
	/*  41 Ice Rain   */ 101,
	/*  42 Oboe 2001  */  68,
	/*  43 Echo Pan   */  87,
	/*  44 DoctorSolo */  86,
	/*  45 Schooldaze */ 103,
	/*  46 BellSinger */  88,
	/*  47 SquareWave */  80,
	/*  48 Str Sect 1 */  48,
	/*  49 Str Sect 2 */  48,
	/*  50 Str Sect 3 */  49,
	/*  51 Pizzicato  */  45,
	/*  52 Violin 1   */  40,
	/*  53 Violin 2   */  40,
	/*  54 Cello 1    */  42,
	/*  55 Cello 2    */  42,
	/*  56 Contrabass */  43,
	/*  57 Harp 1     */  46,
	/*  58 Harp 2     */  46,
	/*  59 Guitar 1   */  24,
	/*  60 Guitar 2   */  25,
	/*  61 Elec Gtr 1 */  26,
	/*  62 Elec Gtr 2 */  27,
	/*  63 Sitar      */ 104,
	/*  64 Acou Bass1 */  32,
	/*  65 Acou Bass2 */  33,
	/*  66 Elec Bass1 */  34,
	/*  67 Elec Bass2 */  39,
	/*  68 Slap Bass1 */  36,
	/*  69 Slap Bass2 */  37,
	/*  70 Fretless 1 */  35,
	/*  71 Fretless 2 */  35,
	/*  72 Flute 1    */  73,
	/*  73 Flute 2    */  73,
	/*  74 Piccolo 1  */  72,
	/*  75 Piccolo 2  */  72,
	/*  76 Recorder   */  74,
	/*  77 Panpipes   */  75,
	/*  78 Sax 1      */  64,
	/*  79 Sax 2      */  65,
	/*  80 Sax 3      */  66,
	/*  81 Sax 4      */  67,
	/*  82 Clarinet 1 */  71,
	/*  83 Clarinet 2 */  71,
	/*  84 Oboe       */  68,
	/*  85 Engl Horn  */  69,
	/*  86 Bassoon    */  70,
	/*  87 Harmonica  */  22,
	/*  88 Trumpet 1  */  56,
	/*  89 Trumpet 2  */  56,
	/*  90 Trombone 1 */  57,
	/*  91 Trombone 2 */  57,
	/*  92 Fr Horn 1  */  60,
	/*  93 Fr Horn 2  */  60,
	/*  94 Tuba       */  58,
	/*  95 Brs Sect 1 */  61,
	/*  96 Brs Sect 2 */  61,
	/*  97 Vibe 1     */  11,
	/*  98 Vibe 2     */  11,
	/*  99 Syn Mallet */  15,
	/* 100 Wind Bell  */  88,
	/* 101 Glock      */   9,
	/* 102 Tube Bell  */  14,
	/* 103 Xylophone  */  13,
	/* 104 Marimba    */  12,
	/* 105 Koto       */ 107,
	/* 106 Sho        */ 111,
	/* 107 Shakuhachi */  77,
	/* 108 Whistle 1  */  78,
	/* 109 Whistle 2  */  78,
	/* 110 BottleBlow */  76,
	/* 111 BreathPipe */ 121,
	/* 112 Timpani    */  47,
	/* 113 MelodicTom */ 117,
	/* 114 Deep Snare */ 118,   /* [propio] */
	/* 115 Elec Perc1 */ 115,
	/* 116 Elec Perc2 */ 118,
	/* 117 Taiko      */ 116,
	/* 118 Taiko Rim  */ 118,
	/* 119 Cymbal     */ 119,   /* [propio] */
	/* 120 Castanets  */ 115,   /* [propio] */
	/* 121 Triangle   */ 112,
	/* 122 Orche Hit  */  55,
	/* 123 Telephone  */ 124,
	/* 124 Bird Tweet */ 123,
	/* 125 OneNoteJam */   8,
	/* 126 WaterBells */  98,
	/* 127 JungleTune */  75,
};

/* Tecla de la parte ritmica del MT-32 -> tecla de percusion de GM.
 * MT32_RHYTHM_UNMAPPED = esa tecla no tiene equivalente: la nota se descarta. */
#define MT32_RHYTHM_UNMAPPED 0xFF

static const uint8_t MT32_RHYTHM_KEY[128] = {
	/* Abreviado para que la tabla se lea de un golpe. */
	#define UNMAP MT32_RHYTHM_UNMAPPED
	/*   0 */ UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP,
	/*  16 */ UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP,
	/*  32 */ UNMAP, UNMAP, UNMAP,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
	/*  48 */  48,  49,  50,  51, UNMAP, UNMAP,  54, UNMAP,  56, UNMAP, UNMAP, UNMAP,  60,  61,  62,  63,
	/*  64 */  64,  65,  66,  67,  68,  69,  70,  71,  72,  73, UNMAP,  75, UNMAP, UNMAP, UNMAP, UNMAP,
	/*  80 */ UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP,
	/*  96 */ UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP,
	/* 112 */ UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP, UNMAP,
	#undef UNMAP
};
