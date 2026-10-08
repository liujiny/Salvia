# CV1000: make cv1000-gpu.log name its own image

Baseline: `6a1fa357`.

## Problem

Every pause report starts with

```
CV1000 GPU: review build=<SALVIA_CV1K_REVIEW_BUILD> game=... interval_frames=...
```

`SALVIA_CV1K_REVIEW_BUILD` was a hardcoded literal
(`"cv1k-clipped-invalidation-20261001-r1"`), so every diagnostics image printed
the same string. A log therefore could not say which XEX produced it. That
misfired during the emitter-link measurement: three appended windows in one log
were read as "old image vs new image" when they were three pauses of a single
run, and nothing in the file could contradict it.

## Change

- New `burn/devices/salvia_cv1k_buildtag.h` holds an optional
  `SALVIA_CV1K_BUILD_TAG`. The checkout leaves it unset.
- `cv1k_review_profile.h` includes it and uses the override when present;
  otherwise it falls back to the compile date/time plus the flavor, so even a
  hand build is distinguishable and a release image can never claim a
  diagnostics tag.
- `xex-build/build-xex.sh` overwrites that header inside the runtime tree for
  each flavor with `<tag> <short-sha> <yyyymmdd-hhmm> <flavor>`, and excludes it
  from the mirror list because the script owns it.

The string is referenced only inside `#if SALVIA_FBNEO_DIAGNOSTICS`, so a
release image is byte-for-byte unaffected.

## Verification

- Host compile of `cv1k_review_profile.h` with the override unset prints
  `drc Oct  8 2026 21:44:58 release`; with the script's header written in place
  it prints the injected `emitter-link 6a1fa357 20261008-2140 diag`.
- After the next diagnostics build, `d_cv1k.obj`/`libretro.lib` contain the
  injected tag, and the next `cv1000-gpu.log` will carry it in every window.

## Revert

Revert this commit; the tag header is additive and the profile header keeps a
working default without it.
