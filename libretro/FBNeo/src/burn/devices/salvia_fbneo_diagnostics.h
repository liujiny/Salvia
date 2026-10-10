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
// Block-link probe. 1 counts how many dispatcher entries are the successor of a
// block that ended at its own sequential completion (the population a successor
// fast path could serve) and how much of the lookup table's set array the
// workload touches (the cache footprint that fast path would avoid). 2 adds the
// gate probe: the share of entries whose bytes are immutable, how often a link
// prediction lands, whether the code it points at is still arena-resident, and
// the line footprint of the record layouts a host/snapshot split would produce.
// Diagnostics only, no behaviour change at either level.
#ifndef SALVIA_CV1K_LINK_PROBE
#define SALVIA_CV1K_LINK_PROBE 0
#endif
// Size of the compact link table the level-2 probe models, as a mask on the
// set index (32767 is the full 32,768 entries, 1023 is 1,024). The point of the
// sweep is that a link table small enough to stay in L1 would have to hold its
// prediction across the workload's predecessor population, which is what the
// hit rate in the report measures.
#ifndef SALVIA_CV1K_LINK_MASK
#define SALVIA_CV1K_LINK_MASK 32767u
#endif
#endif
