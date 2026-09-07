TinySoundFont en Salvia
=======================

Origen : https://github.com/schellingb/TinySoundFont  (tsf.h, v0.9)
Licencia: MIT (c) 2017-2025 Bernhard Schelling, basado en SFZero (c) 2012 Steve Folta
Bajado : rama main, 2026-09-06

Para que esta aqui
------------------
Sintetizador General MIDI por software del frontend.  Los cores de libretro que
piden RETRO_ENVIRONMENT_GET_MIDI_INTERFACE (px68k, dosbox-pure, prboom) nos
mandan bytes MIDI crudos y hay que convertirlos en sonido.  En Xbox 360 no hay
alternativa: el XDK no trae midiOut* de winmm ni driver USB MIDI class.

Que se ha cambiado respecto a upstream
--------------------------------------
Solo una cosa: SOPORTE BIG-ENDIAN.  Upstream lee los campos multi-byte del .sf2
crudos del fichero, sin convertir, asi que en PowerPC (Xbox 360) el banco se
carga como basura.

Los cinco puntos parcheados van marcados en el fuente con [BE-1] .. [BE-5] y
estan TODOS dentro de #ifdef TSF_BIG_ENDIAN.  En little-endian las macros se
expanden a nada, asi que la compilacion de Windows es la de upstream.

  [BE-1] Deteccion de endianness + helper tsf_fixendian + macro TSF_FIXEND.
         Se activa solo con _XBOX (que ya viene definido en las configuraciones
         Xbox 360 del vcxproj), o forzando TSF_BIG_ENDIAN / TSF_LITTLE_ENDIAN.
  [BE-2] Macro TSFR: swap por tamano tras cada lectura de campo de la hydra.
  [BE-3] tsf_hydra_read_pgen / _igen: genAmount es una union de 2 bytes cuyo
         significado depende del generador y NO se puede swapear a ciegas.
  [BE-4] tsf_riffchunk_read: el tamano del chunk RIFF.
  [BE-5] tsf_load_samples: las muestras de 16 bits del chunk smpl.

Al re-vendorizar una version nueva hay que volver a aplicarlos: son ~35 lineas
y la cabecera del fichero lleva la lista.

Como se compila
---------------
No hay proyecto propio.  TSF_IMPLEMENTATION se define en UNA sola unidad de
compilacion, src/audio/midisynth.cpp, que es la que esta en Salvia.vcxproj.
Ese fichero define ademas TSF_NO_STDIO y los TSF_MALLOC/REALLOC/FREE (los tres
juntos, o el guard de upstream mete los de stdlib para los que falten).

Limitaciones asumidas
---------------------
- Solo .sf2.  El camino SF3/Ogg (STB_VORBIS_INCLUDE_STB_VORBIS_H) NO se activa:
  stb_vorbis.c esta lleno de suposiciones little-endian y seria un segundo port
  bastante mas grande.
- TSF convierte todas las muestras a float, 4 bytes cada una: un banco ocupa en
  RAM del orden del DOBLE de lo que ocupa el fichero.  En la 360 conviene un
  banco pequeno (<= 8 MB).
