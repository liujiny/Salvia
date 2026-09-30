# Sampled SH3 fallback detail

This diagnostic does not remove a native guard or change the interpreter. The existing disjoint workload frames (about 1/256) instantiate Count=true; ordinary and frame-timing frames instantiate Count=false. It adds no guest data/opcode read, per-instruction clock, heap allocation or gameplay file write.

`fallback_origin` records the dispatch exit that actually led to the next interpreter step, or the original disabled/delay/IRQ eligibility gate. `partial` alone does not identify the exact generated guard. `fallback_60_opcode` counts all 256 exact 0x60xx opcodes. `fallback_60_access` classifies the pre-instruction Rm address and existing read-map metadata, not an observed bus transaction. The watched class requires a 32-bit read at the board-registered mirrored handler address; it does not prove the idle-PC condition fired. The guest-cycle totals include the interpreter step, subsequent IRQ handling and the existing EAT(1), NOT host execution time.

`fallback_site` is an exact count for an admitted PC/opcode/origin/access/delay key. It records first/last operand addresses for that key, not every address. The fixed 64-entry table uses at most four probes with no replacement or estimated counts. Unadmitted events and their guest cycles are explicitly counted in `site_untracked`; rankings are among admitted sites, never advertised as the global hottest PCs. Opcode/origin/access aggregates remain complete for the sampled frames even when sites overflow. Report/reset runs at the existing pause callback and appends to cv1000-gpu.log.

From the repository root, run:

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/fallback-detail-tests
python3 libretro/FBNeo/tests/sh3_work_profile/run_outer.py --baseline-sh4 /path/to/baseline/sh4.cpp --output /tmp/fallback-detail-outer
```

The detail test uses the actual classifier, accumulator, report and observer with synthetic mapping metadata. It covers all 65536 opcode widths, watched/mirrored/mapped/handler/internal/unaligned reads and alias addresses, 200000 aggregate events, site collision accounting, 64-bit totals and bounded pause formatting. The dispatcher regression checks five exact exit labels. The outer-loop regression checks that each interpreted step gets one observation and that states, ordering and charged cycles match the preserved source. This is not generated PowerPC execution, real MMIO behavior or a console FPS test.
