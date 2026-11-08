# CV1000: YMZ770 Audio Sequencer Looping Channel Preservation Fix

**Baseline**: `4aa30214`  
**Topic**: Fix for background music (BGM) disappearance in *DoDonPachi DaiFukkatsu* (`ddpdfk`) and other CV1000/PGM2 games during track loops.

---

## 1. Why

In Cave CV1000 emulation (`ymz770.cpp`), background music (BGM) abruptly cuts out during gameplay while sound effects (SFX) remain normal.

Through disassembly of DDPDFK Sound ROM (`u23` + `u24`), Stage 1 BGM (Sequence 22) was found to run for exactly 117.638 seconds (58,819 wait states across phrases 0x0D, 0x0E, and 0x10). At the end of the sequence, it executes `REG 0x43 KEYON=0` followed by opcode `0x0F` (End of Sequence).

In upstream FBNeo `ymz770_sequencer()` and `ymz774_sequencer()`:
```cpp
case 0x0f:
    for (int ch = 0; ch < 8; ch++) // might be wrong, ie not needed in case of loop
        if (sequence.stopchan & (1 << ch))
            m_channels[ch].is_playing = false;
    if (sequence.loop)
        sequence.offset = get_seq_offs(sequence.sequence);
    else
        sequence.is_playing = false;
    break;
```
1. `m_channels[ch].is_playing = false` was executed **unconditionally**, forcibly silencing the channels mapped in `stopchan` even when `sequence.loop != 0`.
2. When `sequence.loop` rewound `sequence.offset`, the channel had already been killed. In `ymz770_stream_update()`, if `channel.output_remaining == 0 && !channel.is_playing`, the MPEG decoder was immediately cleared (`mpeg_decoder[ch]->clear()`).
3. Because non-wait register writes take 1 sample each (via `sequence.delay = -1`), the channel remained un-keyed for 4 output samples. When re-keyed, re-priming the cleared MPEG decoder frequently failed, resulting in `channel.is_playing = false` becoming permanent.

---

## 2. What

1. **`libretro/FBNeo/src/burn/snd/ymz770.cpp`**:
   - In `ymz770_sequencer()` (`case 0x0f:`): moved channel silencing into the non-looping `else` branch.
   - In `ymz774_sequencer()` (`case 0xff:`): moved 16-channel silencing into the non-looping `else` branch.
   ```cpp
   case 0x0f:
       if (sequence.loop)
       {
           sequence.offset = get_seq_offs(sequence.sequence); // loop sequence
       }
       else
       {
           for (int ch = 0; ch < 8; ch++)
               if (sequence.stopchan & (1 << ch))
                   m_channels[ch].is_playing = false;
           sequence.is_playing = false;
       }
       break;
   ```
2. **`libretro/FBNeo/tests/ymz770_sequencer/`**:
   - Added unit test suite (`test.cpp`, `run.py`) covering:
     - Test 1: Looping sequence preserving channel playback across EOS `0x0F` boundary.
     - Test 2: Non-looping sequence silencing `stopchan` and terminating cleanly.
     - Test 3: Multi-channel `stopchan` bitmask selective evaluation.

---

## 3. Verification

1. **Sequencer Unit Test**:
   - Host native: `python3 libretro/FBNeo/tests/ymz770_sequencer/run.py` -> **PASS**.
   - Host ASan/UBSan: `python3 libretro/FBNeo/tests/ymz770_sequencer/run.py --sanitize` -> **PASS** (zero sanitization errors).
   - QEMU PowerPC cross-compilation: `python3 libretro/FBNeo/tests/ymz770_sequencer/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root` -> **PASS** under PowerPC architecture.

2. **Subsystem Regressions**:
   - `python3 libretro/FBNeo/tests/ymz770_idct/run.py` -> **PASS** (100,000 vectors bit-identical).
   - `python3 libretro/FBNeo/tests/frontend_state/run.py` -> **PASS** (streaming, 151 MiB state, CRC).
   - `python3 libretro/FBNeo/tests/audio_tempo/run.py` -> **PASS** (all tempo, WSOLA, underrun tests).

---

## 4. Validation Limits & Next Steps

- This test verifies the sequencer bytecode parsing and channel state machine under both x86_64 and PowerPC ISA.
- Complete game audio loop verification during 24-minute gameplay sessions requires Xbox 360 Jasper deployment.

---

## 5. Artifact & Console Deployment

- **Build Tag**: `cv1k-onchip-audioloop`
- **Code Revision**: `617ed654` (based on `8cc9246b`)
- **Flavor**: Diagnostics (`SALVIA_FBNEO_DIAGNOSTICS 1`)
- **Built Images**:
  - `fbneo.xex` (diag): 34,865,152 bytes, SHA-256 `16cd49469ae3832784a26e291b75c49fe5f59e23350ff8eb2e6b080c326f185c`
  - `mmpork.xex` (diag): 34,832,384 bytes, SHA-256 `c5b3f59857722764da28602a3d016c28beabc9acac6a2488b63b80466b2ed20e`
  - `ddpsdoj.xex` (diag): 34,832,384 bytes, SHA-256 `d74b3031f6fc78c814a3cf7a41cd97422710664353b344eaf59a7efe0133f35e`
- **Deployment**: Both `fbneo.xex` and `mmpork.xex` mirrored to `/Usb0/Salvia-360` on Xbox 360 Jasper (`192.168.2.232`).

