# Xbox FBNeo audio continuity tests

`src/audio/audiotempo.h` implements bounded WSOLA slowdown over the existing
stereo source ring. It is enabled only by the Xbox `fbneo` frontend. The SDL
callback allocates no memory and performs no file I/O or blocking waits.

```sh
python3 libretro/FBNeo/tests/audio_tempo/run.py --output /tmp/fbneo-audio
python3 libretro/FBNeo/tests/audio_tempo/run.py --sanitize --output /tmp/fbneo-audio-asan
python3 libretro/FBNeo/tests/audio_tempo/run.py --toolchain-root /path/to/ppc/root --output /tmp/fbneo-audio-ppc
```

Coverage:

- Normal-speed stereo output remains bit exact, including ring wrapping.
- 60, 40, 30 and 20 FPS supply; 60 -> 30 -> 60 FPS transitions and recovery.
- 440 Hz pitch measurement and antiphase stereo alignment.
- Real AudioBuffer + AudioRateControl integration at 60/40/30/24/20 FPS, with
  mixed 220/660/880 Hz material and a decaying percussion component.
- 44.1 -> 48 kHz conversion, 1024/2048-frame callbacks, a 200 ms producer stall.
- Whole-stereo-frame overflow behavior, pause/fast-forward/reset, oversized
  blocking writes with concurrent normal and WSOLA consumers.

The source target is 3072 frames (64 ms of 48 kHz source audio), including the
WSOLA lookahead. At sustained 30 FPS it represents about 128 ms of wall time;
at 20 FPS about 192 ms, plus the SDL/device queue. This reserve keeps lookahead
and packet jitter from causing another underrun; sound effects may therefore
be delayed. The target is not advertised as a fixed 64 ms end-to-end latency. Silence is
still necessary during initial refill and sufficiently long stalls. Stretching
is bounded at 0.28 source frames per output frame, and sustained lower supply
cannot be concealed indefinitely. Normal playback resumes tempo 1.0 as supply
recovers. This improves continuity at low emulation speed, not emulation FPS.

Only the fixed core/device sample-rate conversion remains active when WSOLA is
enabled; the older +/-2% occupancy resampler is bypassed to avoid competing
controllers and pitch drift. Pause diagnostics go to `game:\\fbneo-audio.log`.

Host and QEMU benchmark timings are not Xbox performance measurements. QEMU
checks 32-bit big-endian arithmetic and the same source implementation.
