// CV1000 blit-path census. Console and host diagnostic builds only.
//
// The blitter job is timed as one number and the CPU replay of it as another,
// but the replay is a mixture: an untinted copy, an exact integer blend, and
// the table-driven generic loops, which cost very different amounts per pixel.
// A single nanoseconds-per-pixel average therefore cannot say which inner loop
// is worth making faster. This attributes every pixel a replayed command draws
// to the loop that drew it, so the mixture is visible.
//
// Counting only: the probe changes no emulation state and its counters are
// compiled out unless SALVIA_CV1K_BLIT_PATH_PROBE is on.
#ifndef SALVIA_CV1K_BLIT_PATHS_H
#define SALVIA_CV1K_BLIT_PATHS_H
#include "salvia_fbneo_diagnostics.h"

// One entry per inner loop that epic12_gpu_replay() can select. The simple
// loops are the untinted, unblended copies; "fixed" is the exact integer blend
// of epic12_draw_fixed(); plain and the two blend modes are the table-driven
// generic loops. UNKNOWN catches a command the replay dispatches to a loop
// this census does not name, so the entries always add up to the replay total.
enum {
	EPIC12_BLIT_PATH_SIMPLE_F0_TR0 = 0,
	EPIC12_BLIT_PATH_SIMPLE_F0_TR1,
	EPIC12_BLIT_PATH_SIMPLE_F1_TR0,
	EPIC12_BLIT_PATH_SIMPLE_F1_TR1,
	EPIC12_BLIT_PATH_FIXED_F0_TR0,
	EPIC12_BLIT_PATH_FIXED_F0_TR1,
	EPIC12_BLIT_PATH_FIXED_F1_TR0,
	EPIC12_BLIT_PATH_FIXED_F1_TR1,
	EPIC12_BLIT_PATH_TINTED_PLAIN,
	EPIC12_BLIT_PATH_BLEND_D1,
	EPIC12_BLIT_PATH_BLEND_D4,
	EPIC12_BLIT_PATH_UNKNOWN,
	EPIC12_BLIT_PATH_COUNT
};

#if SALVIA_CV1K_BLIT_PATH_PROBE
extern "C" {
	extern unsigned long long salvia_cv1k_blit_path_pixels[EPIC12_BLIT_PATH_COUNT];
	extern unsigned long long salvia_cv1k_blit_path_commands[EPIC12_BLIT_PATH_COUNT];
	// Set by epic12_gpu_replay() around each replayed command and left at -1
	// everywhere else, so an inner loop can charge the pixels it draws without
	// knowing which caller reached it. A pixel count is charged once per row,
	// not once per pixel, so the probe cannot perturb what it measures.
	extern int salvia_cv1k_blit_path_current;
}
#define EPIC12_BLIT_PATH_CHARGE(pixels) \
	do { if (salvia_cv1k_blit_path_current >= 0 && (pixels) > 0) \
		salvia_cv1k_blit_path_pixels[salvia_cv1k_blit_path_current] += (unsigned long long)(pixels); } while (0)
#else
#define EPIC12_BLIT_PATH_CHARGE(pixels) do { } while (0)
#endif

#endif
