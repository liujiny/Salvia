// Optional Xbox FBNeo diagnostics. Normal builds do not write diagnostic logs.
#ifndef SALVIA_FBNEO_DIAGNOSTICS_H
#define SALVIA_FBNEO_DIAGNOSTICS_H
#ifndef SALVIA_FBNEO_DIAGNOSTICS
#define SALVIA_FBNEO_DIAGNOSTICS 0
#endif
#if SALVIA_FBNEO_DIAGNOSTICS != 0 && SALVIA_FBNEO_DIAGNOSTICS != 1
#error SALVIA_FBNEO_DIAGNOSTICS must be 0 or 1
#endif
// Write-stamp probe level for the CV1000 PPC DRC. The mechanism is described
// in docs/optimization/2026-10-09-cv1k-write-stamps.md. 0 compiles it out
// entirely, 1 keeps it as a cross-check that still compares the snapshot (the
// gate measurement), 2 lets a clean pair of stamps skip that comparison. Only
// the diagnostics image turns it on; the release value is the default here.
#ifndef SALVIA_CV1K_STAMP_PROBE
#define SALVIA_CV1K_STAMP_PROBE 0
#endif
#if SALVIA_CV1K_STAMP_PROBE < 0 || SALVIA_CV1K_STAMP_PROBE > 2
#error SALVIA_CV1K_STAMP_PROBE must be 0, 1 or 2
#endif
#endif
