# CV1000: what of the PS4 FBNeo work is portable to the Xbox 360 backend

Baseline SHA for this round: `c4e92f3380ac85ef1ee09897806806790ace8b9a`.

## Why

The PS4 FBNeo CV1000 project ended with a retained, hardware-confirmed
optimization list. The Xbox 360 Salvia port targets the same games, so the
obvious next step is to ask which of those changes apply here. The answer is not
uniform: the two ports share the emulation algorithms but not the CPU backends
(PS4 is x86-64 with SSE2/AVX and Orbis; Salvia is Xenon/PPC with VMX128 and
Xenos), so the list has to be split by *layer* rather than copied wholesale.

References: PS4 playbook `PS4_CV1000_OPTIMIZATION_PLAYBOOK.md` sections 6.1-6.4
(retained commits), and this checkout's `libretro/FBNeo/src/cpu/sh4/`,
`libretro/FBNeo/src/burn/devices/epic12*`.

## Classification

| PS4 retained item (commit) | Layer | Salvia status | Verdict |
| --- | --- | --- | --- |
| 8 MiB code arena (`27476d177`) | SH3 guest->host translation, CPU independent | `Sh3Ppc::CACHE_BYTES` is 8 MiB, same bump-allocated arena with a full reset on overflow | already present, same design |
| 64 MiB code arena (`0d5df85c7`) | same | not present; measured here (see `2026-10-07-cv1k-arena-size.md`) | **portable, landed** |
| Native literal constant displacement (`d22990c71`) | x86-64 JIT encoding | PPC already uses `constant(SO(field), imm)` / `addi` with 16-bit displacements | equivalent idea already in place; revisit only with a measured regression |
| Fuse terminating branch and PR access (`c081e5d09`) | x86-64 JIT encoding | PPC DRC emits exits explicitly and already folds delay-slot and PR handling per block | needs a PPC-specific rewrite, not a port |
| Batched protected byte loops in the JIT fallback (`d6b299ad0`) | translation-time work, CPU independent | Salvia uses the interpreter fallback path instead; `sh3_interpreter_hot.h` is the current shape | candidate, needs host-side hotness evidence first |
| Bounded scalar comparison of opcode tails (`eeba0514d`) | translation-time work | PPC validates with `MemMapR`/`MemMapF` page checks and per-word compares | candidate, needs measurement |
| Exact DIV1 flag arithmetic without branches (`a4ab81a3c`) | SH3 flag semantics, portable | `sh3_drc_ppc.h` DIV1 already avoids the 64-bit CA flag but still emits an add/sub `branch`+`jump` pair | candidate; Xenon has no `isel`, so a branchless select needs mask arithmetic - measure before changing |
| Cached prescaler reciprocals, timer tick batching, stopped-timer iteration (`9614731d8`, `c711071c6`, `efb057ecb`, `6de3b434f`) | SH2/timer emulation, CPU independent | not present in this checkout's timer path | candidate, low risk, needs a workload that actually exercises timers |
| Ordered CV1000 blitter worker, CPU blend fast path (`23c916ee2`, `a80b459c6`) | renderer | `epic12_thread.h` + `epic12_gpu_batch.h` already carry ordering and batching | largely already ported |
| Alpha tile cache: load only on miss (`eb195a61f`) | renderer | `Epic12GpuPageCache` exists with `cache_hits`/`upload_pages` counters and `epic12_xenos_alpha_trim` | already ported in spirit; compare counters before redoing |
| Fused fixed-alpha reciprocal multiply (`1155178d2`) | renderer | `epic12_gpu_alpha_vmx.h` exists (VMX128, not SSE2) | already ported to VMX; re-verify rather than re-port |
| Skip destination read on zero alpha, specialised self-blend (`17c80fbd7`, `ff51a4701`) | renderer | `ALPHA ... empty=` counter shows empty commands are already tracked | already ported in spirit |
| `epic12_blend_vector.h` SSE2 (`_mm_mulhi_epu16`) | renderer, x86-specific | Xbox path is VMX128 | **not portable**; the VMX file is the equivalent |

## What must not be copied

- Any x86/x86-64 JIT encoding, SSE2/AVX intrinsic, or Orbis SDK, ELF/SELF/PKG
  path. Xenon is big-endian PPC with VMX128; Xenos is not a GCN GPU.
- Host-measured PS4 numbers. Frame rates do not transfer across consoles; only
  equivalence and relative work counts do.

## Ordering of remaining work

1. Code arena size (landed this round) - same root cause, same fix, already
   confirmed on PS4.
2. Renderer side: verify the already-present VMX alpha / page cache / batching
   code against the PS4 counters instead of re-porting it, since the Xbox
   counters in `epic12_xenos_report()` already expose the same quantities.
3. Translation-cost items (batched byte loops, bounded opcode tails) only after
   a measured hotness signal, because the PPC DRC already avoids much of the
   per-block work the PS4 work targeted.
4. DIV1 flag arithmetic last: it is the most delicate, and the PPC branch there
   may be cheaper than the mask arithmetic that would replace it.

## Validation limits

Classification above is source-level analysis plus the PS4 project's recorded
evidence. It is not an Xbox measurement. Anything marked "candidate" needs its
own host differential run and, for user-visible claims, an Xbox session.
