# GA2: parallel System 32 layer mixing

Baseline: `2ce7629a` (V60 Xbox byte reads); full stable baseline before both changes: `7df834cbd38f577f0b0ed95ee6e361d77c160761`.

## Reason and implementation

A host GA2 profile identified layer mixing as the largest individual rendering task: approximately 0.68–0.88 ms/frame, compared with about 0.50–0.52 ms main V60 execution, 0.21–0.22 ms V25 execution and 0.46–0.54 ms Z80 execution. These are host measurements, not console timings.

The existing System 32 mixer prepares layer priority, blend and RGB-offset state once. Persistent workers from `render_worker.h` then own disjoint output rows; every job finishes before palette conversion, the next emulated CPU slice, saving or exiting. No CPU/interrupt/protection/sound scheduling was changed. One core executes the same row function without workers. Existing Rendering cores settings (1/2/3) apply immediately; default remains 2. The option description now includes Sega System 32.

For a pixel with no blend and zero RGB offset, direct RGB555 bit operations replace channel adjustment/clamping. Shadow still halves each channel independently. Palette RAM, color offsets, blend equations, sprite priority and all emulated clocks remain unchanged. The reported blue clothing is not corrected in this experiment, per the user's subsequent instruction.

## Validation

- `g++ -O2 libretro/FBNeo/tests/system32_mixer/rgb555.cpp -o /tmp/system32-rgb555 && /tmp/system32-rgb555`: PASS for all 65,536 input colors, with and without shadow (131,072 cases).
- GA2 libretro replay from the same original savestate, 6,000 frames with scripted controls: single-core, two-core, three-core and changing 1/2/3 every 300 frames all matched every original video/audio hash and final serialized state. Inputs include a gameplay segment, subsequent game-over and attract scenes; this is not a full-game or Xbox console test.
- Host test command pattern: `GA2_CORES=2 /home/humor/salvia-tests/ga2/headless /home/humor/salvia-tests/ga2/mix-thread.so '<ROM-directory>/ga2.zip' <output-directory> 6000 16 3 /home/humor/salvia-tests/ga2/boot/end.state 1 1`. Use `GA2_CORES=3` for three cores and `GA2_SWITCH=1` for live switching. Artifacts, ROM and raw replay output are outside Git.
- Sequential host benchmarks over 3,000 frames, excluding callback copying/hashing: original 2.9441 ms/frame; combined candidate one core 2.8131 ms/frame; two cores 2.3947 ms/frame; three cores 2.4070 ms/frame. Two-core reduction about 18.7%. These are one host run per setting while a Windows link was in progress; no console FPS claim is made. Desktop CPUs also do not reproduce Xbox unaligned-load costs.
- Final Xbox FBNeo Release and Salvia Release_finalburn build: PASS; XEX2 output verified (34611200 bytes). No other emulation core was built.

XEX SHA256: `bf1eeb679e542b69e8b7206160e56698f6d2ed19d042d42532af5c389fca231a`.

Rollback: `git revert <commit containing this document>`; mirror `libretro/FBNeo/src/burn/drv/sega/d_segas32.cpp` and `libretro/FBNeo/src/burner/libretro/retro_common.cpp` to sibling `../Salvia` and rebuild. To return to the original stable release, also revert `2ce7629a`, or restore the verified baseline XEX.

Windows baseline backup and candidate: `E:\Baiduyundownload\salvia-toolchain\.work\ga2-system32-20261003\`. Delivery is `...\Salvia\Distro360\fbneo.xex`.

## Console acceptance

First use two rendering cores with the same scaling/shader settings as the original 30 FPS observation. Check boot, character selection, several enemy-heavy scenes, audio, save/load and returning to the menu. Compare three cores only after that. Actual stable 60 FPS requires console confirmation.
