# CV1000: Hardware Acceleration & Shader Feedback for SMODE 2 Modulation Blend (DDPSDOJ Explosion White-Screen Fix)

Baseline `d4450125` (`perf(epic12): add 2-way 512-set associative snapshot cache for dialogue blits`).

## 1. Problem & Root Cause Analysis

### Explosion White-Screen Washout in DoDonPachi SaiDaiOuJou (ddpsdoj)
During massive burning explosion sequences in *DoDonPachi SaiDaiOuJou* (`ddpsdoj`, verified at savestate slot 0), the screen washed out into solid white (`RGB (248, 248, 248)` covering over 67% of the display), obscuring the explosion fireball sprite animation and gameplay until the sequence ended.

Diagnostics log (`cv1000-gpu.log`) revealed:
```
CV1000 GPU: rejections blend=17592 size=0 oob=0 sy=0 intersect=896 (batches=36722)
CV1000 GPU: reject_mode smode=2 dmode=0 count=17592 (batches=36722)
```
17,592 blit commands were rejected by `epic12_gpu_submit` because `smode != 0` was unconditionally rejected.

When rejected:
1. Every rejected command fell back to the CPU rasterizer `draw_sprite_..._s2_d0` in `epic12pixel.inc`:
   $$s\_clr = \min(31, \lfloor \frac{d \cdot s}{31} \rfloor + \lfloor \frac{d \cdot d_\alpha}{31} \rfloor)$$
2. Because the Xbox 360 GPU draws to EDRAM without writing back intermediate frames to host memory (`m_bitmaps`), host VRAM was stale and desynchronized from EDRAM.
3. Furthermore, when multiple fireball particle sprites overlapped, destination $d$ rapidly saturated to 31 ($s + d_\alpha \ge 31$). With GPU and CPU out of sync, repeated CPU fallbacks caused the center of the display to lock into solid white.

---

## 2. Solution: GPU Hardware Acceleration & Shader Feedback for SMODE 2 DMODE 0

1. **Pipeline & Submission Integration**:
   - Extended `Epic12GpuCommand` with `int smode;`.
   - Updated `epic12_gpu_submit` to accept `smode == 2 && dmode == 0` instead of rejecting it.
   - Updated `epic12_gpu_dest_mode(c)`: returns `2` when `c.smode == 2 && c.dmode == 0`.
   - In `epic12_gpu_batch.h`, `epic12_gpu_feedback(c)` automatically identifies `dest_mode == 2` as a destination-dependent feedback draw, allocating snapshot textures and configuring the pipeline for resolve passes.

2. **Pixel Shader Implementation**:
   - In Direct3D 9 / Xenos HLSL shaders (`shade` and `shade_feedback`), implemented the exact CV1000 modulation/screen blend formula when `mode.z > 1.5`:
     ```hlsl
     if (mode.z > 1.5) {
         outc = min(31.0, floor(d * s / 31.0 + 0.0001) + floor(d * mode.w / 31.0 + 0.0001));
     }
     ```
   - All 2,097,152 component combinations of `tint`, `source`, `destination`, and `da` were tested and verified to be bit-for-bit identical to the integer reference lookup tables.

3. **Replay Fallback Support**:
   - In `epic12_gpu_replay`, mapped `c.smode == 2 && c.dmode == 0` to `draw_sprite_f##f##_ti##ti##_tr##t##_s2_d0` to ensure complete CPU replay equivalence.

---

## 3. Verification

1. `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 differential image cases).
2. `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (6.29M unified cases, 4.19M additive cases, 2.10M modulation cases, and all batching/coherency invariants).
