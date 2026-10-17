// CV1000 GPU command batching. CPU VRAM remains authoritative between lists.
// Included by epic12.cpp after the original software rasterizers.
#ifndef FBNEO_EPIC12_GPU_H
#define FBNEO_EPIC12_GPU_H

#include "salvia_cv1k_blit_paths.h"

struct Epic12GpuCommand {
	int sx, sy, x, y, w, h;
	int flipx, flipy, transparent, blend, dmode, sa, da;
	clr_t tint;
};

#define EPIC12_GPU_BINS 1
static Epic12GpuCommand epic12_gpu_commands[EPIC12_GPU_BINS][4096];
static int epic12_gpu_count[EPIC12_GPU_BINS];
static unsigned epic12_gpu_pixels[EPIC12_GPU_BINS];
static bool epic12_gpu_source_pages[EPIC12_GPU_BINS][2048];
static bool epic12_gpu_dest_pages[EPIC12_GPU_BINS][2048];
static unsigned epic12_gpu_source_count[EPIC12_GPU_BINS];
static rectangle epic12_gpu_bounds[EPIC12_GPU_BINS];
static rectangle epic12_gpu_source_bounds[EPIC12_GPU_BINS];
static bool epic12_gpu_enabled;

// Batches below this many pixels are rasterized on the CPU instead of being
// transferred to the Xenos. The split point was chosen against ddpdfk-like
// traffic; epic12.cpp's gfx_exec() re-reads it from the DIPs each list so a
// console A/B can move it without a rebuild.
static unsigned epic12_gpu_threshold = 512;
static bool epic12_gpu_force_bringup = false;

// Replay cost per batch size. The threshold is only worth moving if the CPU
// path's marginal cost per pixel is below the Xenos path's fixed per-batch
// cost, so the size of each replayed batch is the number that decides it.
#define EPIC12_GPU_REPLAY_BUCKETS 4
// One boundary per bucket except the first, so a bucket index is the count of
// boundaries the batch has passed.
static const unsigned epic12_gpu_replay_bucket_px[EPIC12_GPU_REPLAY_BUCKETS - 1] = { 8192, 32768, 131072 };

enum Epic12GpuFlushReason {
	EPIC12_GPU_FLUSH_END_LIST = 0,
	EPIC12_GPU_FLUSH_SOURCE_PAGES,
	EPIC12_GPU_FLUSH_REGION,
	EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY,
	EPIC12_GPU_FLUSH_COMMAND_LIMIT,
	EPIC12_GPU_FLUSH_CPU_UPLOAD,
	EPIC12_GPU_FLUSH_CPU_DRAW,
	EPIC12_GPU_FLUSH_OTHER,
	EPIC12_GPU_FLUSH_REASON_COUNT
};
static unsigned epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_REASON_COUNT];
static unsigned epic12_gpu_flush_small_cpu;
static unsigned epic12_gpu_flush_render_fallback;
static unsigned epic12_gpu_reject_blend;
static unsigned epic12_gpu_reject_size;
static unsigned epic12_gpu_reject_oob;
static unsigned epic12_gpu_reject_sy;
static unsigned epic12_gpu_reject_intersect;
static unsigned epic12_gpu_reject_smode_dmode[8][8];
#if SALVIA_FBNEO_DIAGNOSTICS
static unsigned epic12_gpu_diag_dep_true;
static unsigned epic12_gpu_diag_dep_false;
static unsigned epic12_gpu_diag_dep_count_buckets[5];
static unsigned epic12_gpu_diag_reg_count_buckets[5];
static unsigned epic12_gpu_diag_small_cpu_buckets[5];
static unsigned epic12_gpu_diag_reg_exceed_x;
static unsigned epic12_gpu_diag_reg_exceed_y;
static unsigned epic12_gpu_diag_reg_clip_fs;
static unsigned epic12_gpu_diag_reg_clip_scr;
static unsigned epic12_gpu_diag_reg_span[4];
static int epic12_gpu_diag_reg_samples[5][6];
static int epic12_gpu_diag_reg_sample_count;
static unsigned epic12_gpu_diag_bin_hits[EPIC12_GPU_BINS];
static unsigned epic12_gpu_diag_c_dest;
static unsigned epic12_gpu_diag_c_raw;
static int epic12_gpu_diag_cfail_samples[5][8];
static int epic12_gpu_diag_cfail_sample_count;
static int epic12_gpu_diag_small_cpu_samples[10][6];
static int epic12_gpu_diag_small_cpu_sample_count;
#endif

static void epic12_gpu_diag_reset()
{
	memset(epic12_gpu_flush_reasons,0,sizeof(epic12_gpu_flush_reasons));
	epic12_gpu_flush_small_cpu=0;
	epic12_gpu_flush_render_fallback=0;
	epic12_gpu_reject_blend=0;
	epic12_gpu_reject_size=0;
	epic12_gpu_reject_oob=0;
	epic12_gpu_reject_sy=0;
	epic12_gpu_reject_intersect=0;
	memset(epic12_gpu_reject_smode_dmode,0,sizeof(epic12_gpu_reject_smode_dmode));
#if SALVIA_FBNEO_DIAGNOSTICS
	epic12_gpu_diag_dep_true=0;
	epic12_gpu_diag_dep_false=0;
	memset(epic12_gpu_diag_dep_count_buckets,0,sizeof(epic12_gpu_diag_dep_count_buckets));
	memset(epic12_gpu_diag_reg_count_buckets,0,sizeof(epic12_gpu_diag_reg_count_buckets));
	memset(epic12_gpu_diag_small_cpu_buckets,0,sizeof(epic12_gpu_diag_small_cpu_buckets));
	epic12_gpu_diag_reg_exceed_x=0;
	epic12_gpu_diag_reg_exceed_y=0;
	epic12_gpu_diag_reg_clip_fs=0;
	epic12_gpu_diag_reg_clip_scr=0;
	memset(epic12_gpu_diag_reg_span,0,sizeof(epic12_gpu_diag_reg_span));
	epic12_gpu_diag_reg_sample_count=0;
	memset(epic12_gpu_diag_bin_hits,0,sizeof(epic12_gpu_diag_bin_hits));
	epic12_gpu_diag_c_dest=0;
	epic12_gpu_diag_c_raw=0;
	epic12_gpu_diag_cfail_sample_count=0;
	epic12_gpu_diag_small_cpu_sample_count=0;
#endif
}

static inline bool epic12_gpu_intersects(int x, int y, int w, int h, const rectangle &r)
{
	return x <= r.max_x && x + w > r.min_x && y <= r.max_y && y + h > r.min_y;
}

static inline void epic12_gpu_add_initial_pages(int b, int px0, int px1, int py0, int py1)
{
	if (py0 == py1 && px0 == px1) {
		const int page = (py0 << 6) | px0;
		epic12_gpu_source_pages[b][page] = true;
		epic12_gpu_source_count[b] = 1;
	} else {
		for (int py = py0; py <= py1; ++py) {
			const int row = py << 6;
			for (int px = px0; px <= px1; ++px) {
				const int page = row | px;
				if (!epic12_gpu_source_pages[b][page]) {
					epic12_gpu_source_pages[b][page] = true;
					++epic12_gpu_source_count[b];
				}
			}
		}
	}
}

#if SALVIA_CV1K_BLIT_PATH_PROBE
// Which inner loop epic12_gpu_replay() will pick for this command. This mirrors
// the dispatch below; EPIC12_BLIT_PATH_UNKNOWN is the census's own alarm that
// the two have drifted apart.
static unsigned epic12_blit_path_index(const Epic12GpuCommand &c)
{
	if (!c.blend && c.tint.r == 32 && c.tint.g == 32 && c.tint.b == 32)
		return EPIC12_BLIT_PATH_SIMPLE_F0_TR0 + (c.flipx ? 2 : 0) + (c.transparent ? 1 : 0);
	if (c.blend && c.dmode == 0)
		return EPIC12_BLIT_PATH_FIXED_F0_TR0 + (c.flipx ? 2 : 0) + (c.transparent ? 1 : 0);
	if (!c.blend) return EPIC12_BLIT_PATH_TINTED_PLAIN;
	if (c.dmode == 1) return EPIC12_BLIT_PATH_BLEND_D1;
	if (c.dmode == 4) return EPIC12_BLIT_PATH_BLEND_D4;
	return EPIC12_BLIT_PATH_UNKNOWN;
}
#endif

static void epic12_gpu_replay(const Epic12GpuCommand &c)
{
#if SALVIA_CV1K_BLIT_PATH_PROBE
	// Charge the command to its loop before the clip maths runs: a command the
	// source-wrap check then rejects still cost its dispatch, and it is part of
	// the mixture. The pixels actually drawn are charged by the loop itself.
	const unsigned path = epic12_blit_path_index(c);
	salvia_cv1k_blit_path_current = (int)path;
	++salvia_cv1k_blit_path_commands[path];
#endif
	rectangle clip(c.x, c.x + c.w - 1, c.y, c.y + c.h - 1);
	// Normalized sources name their top-left, including after clipping.
#define GPU_CPU_ARGS &clip,m_bitmaps,c.sx,c.sy,c.x,c.y,c.w,c.h,c.flipy,c.sa,c.da,&c.tint
	if (!c.blend && c.tint.r == 32 && c.tint.g == 32 && c.tint.b == 32) {
		if (c.flipx) {
			if (c.transparent) draw_sprite_f1_ti0_tr1_simple(GPU_CPU_ARGS);
			else draw_sprite_f1_ti0_tr0_simple(GPU_CPU_ARGS);
		} else {
			if (c.transparent) draw_sprite_f0_ti0_tr1_simple(GPU_CPU_ARGS);
			else draw_sprite_f0_ti0_tr0_simple(GPU_CPU_ARGS);
		}
		return;
	}
	if (c.blend && c.dmode == 0) {
		epic12_draw_fixed(c.flipx,c.transparent,GPU_CPU_ARGS);
		return;
	}
#define GPU_CPU_CASE(f,t) \
	if (!c.blend) draw_sprite_f##f##_ti1_tr##t##_plain(GPU_CPU_ARGS); \
	else if (c.dmode == 1) draw_sprite_f##f##_ti1_tr##t##_s0_d1(GPU_CPU_ARGS); \
	else if (c.dmode == 7) draw_sprite_f##f##_ti1_tr##t##_s0_d7(GPU_CPU_ARGS); \
	else if (c.dmode == 3) draw_sprite_f##f##_ti1_tr##t##_s0_d3(GPU_CPU_ARGS); \
	else draw_sprite_f##f##_ti1_tr##t##_s0_d4(GPU_CPU_ARGS)
	if (c.flipx) { if (c.transparent) { GPU_CPU_CASE(1,1); } else { GPU_CPU_CASE(1,0); } }
	else { if (c.transparent) { GPU_CPU_CASE(0,1); } else { GPU_CPU_CASE(0,0); } }
#undef GPU_CPU_CASE
#undef GPU_CPU_ARGS
}

#include "epic12_gpu_batch.h"

#ifdef EPIC12_GPU_TEST
#include "epic12_gpu_mock.h"
#else
#include "epic12_gpu_xbox.h"
#endif

static void epic12_gpu_flush_bin(int b, Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	if (!epic12_gpu_count[b]) return;
	++epic12_gpu_flush_reasons[reason];
	bool rendered=false;
#if SALVIA_CV1K_PROBE
	bool probeSteady=epic12_xenos_validated;
	unsigned long long probeStart=salvia_cv1k_tick();
#endif
	bool routeCpu = epic12_gpu_pixels[b] < epic12_gpu_threshold;
#ifdef _XBOX
	if (routeCpu && epic12_gpu_force_bringup && !epic12_xenos_validated && !epic12_xenos_failed) routeCpu=false;
#endif
	if (routeCpu) {
		++epic12_gpu_flush_small_cpu;
#if SALVIA_FBNEO_DIAGNOSTICS
		int sb = epic12_gpu_pixels[b] < 512 ? 0 : (epic12_gpu_pixels[b] < 2048 ? 1 : (epic12_gpu_pixels[b] < 4096 ? 2 : (epic12_gpu_pixels[b] < 8192 ? 3 : 4)));
		++epic12_gpu_diag_small_cpu_buckets[sb];
		if (epic12_gpu_diag_small_cpu_sample_count < 10) {
			int s = epic12_gpu_diag_small_cpu_sample_count++;
			epic12_gpu_diag_small_cpu_samples[s][0] = epic12_gpu_count[b];
			epic12_gpu_diag_small_cpu_samples[s][1] = epic12_gpu_pixels[b];
			epic12_gpu_diag_small_cpu_samples[s][2] = epic12_gpu_bounds[b].min_x;
			epic12_gpu_diag_small_cpu_samples[s][3] = epic12_gpu_bounds[b].max_x;
			epic12_gpu_diag_small_cpu_samples[s][4] = (int)reason;
			epic12_gpu_diag_small_cpu_samples[s][5] = b;
		}
#endif
	} else if (epic12_gpu_render(epic12_gpu_commands[b], epic12_gpu_count[b], epic12_gpu_bounds[b])) {
		rendered=true;
	} else {
		++epic12_gpu_flush_render_fallback;
	}
	if (!rendered) {
		UINT64 delay = epic12_device_blit_delay;
		for (int i = 0; i < epic12_gpu_count[b]; ++i) epic12_gpu_replay(epic12_gpu_commands[b][i]);
		epic12_device_blit_delay = delay;
#if SALVIA_CV1K_BLIT_PATH_PROBE
		salvia_cv1k_blit_path_current = -1;
#endif
	}
#if SALVIA_CV1K_PROBE
	if (probeStart) {
		unsigned long long probeEnd=salvia_cv1k_tick();
		if (probeEnd) {
			unsigned long long ticks=probeEnd-probeStart;
			if (rendered) {
				if (probeSteady) {
					salvia_cv1k_probe[14]+=ticks; ++salvia_cv1k_probe[15];
					salvia_cv1k_probe[19]+=epic12_gpu_pixels[b];
				}
			} else {
				salvia_cv1k_probe[16]+=ticks; ++salvia_cv1k_probe[17];
				salvia_cv1k_probe[18]+=epic12_gpu_pixels[b];
				unsigned bucket=0;
				while (bucket+1<EPIC12_GPU_REPLAY_BUCKETS &&
				       epic12_gpu_pixels[b]>=epic12_gpu_replay_bucket_px[bucket]) ++bucket;
				salvia_cv1k_probe[24+bucket*3]+=ticks;
				salvia_cv1k_probe[25+bucket*3]+=epic12_gpu_pixels[b];
				++salvia_cv1k_probe[26+bucket*3];
			}
		}
	}
#endif
	epic12_gpu_invalidate(epic12_gpu_bounds[b]);
	epic12_gpu_count[b] = 0;
	epic12_gpu_pixels[b] = 0;
	epic12_gpu_source_count[b] = 0;
	memset(epic12_gpu_source_pages[b],0,sizeof(epic12_gpu_source_pages[b]));
	memset(epic12_gpu_dest_pages[b],0,sizeof(epic12_gpu_dest_pages[b]));
}

static void epic12_gpu_retire_bin0(Epic12GpuFlushReason reason)
{
	epic12_gpu_flush_bin(0, reason);
}

static void epic12_gpu_flush(Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	epic12_gpu_flush_bin(0, reason);
}


static int epic12_gpu_can_fit(int b, const rectangle &dest, int sx, int sy, int w, int h,
	int px0, int px1, int py0, int py1, unsigned &newPages, int *newPageList, rectangle &merged)
{
	if (!epic12_gpu_count[b]) {
		merged = dest;
		newPages = 0;
		return 0;
	}
	merged = epic12_gpu_bounds[b];
	if (dest.min_x >= merged.min_x && dest.max_x <= merged.max_x &&
	    dest.min_y >= merged.min_y && dest.max_y <= merged.max_y) {
		if (epic12_gpu_count[b] >= 4096) return 1;
	} else {
		if (dest.min_x < merged.min_x) merged.min_x = dest.min_x;
		if (dest.min_y < merged.min_y) merged.min_y = dest.min_y;
		if (dest.max_x > merged.max_x) merged.max_x = dest.max_x;
		if (dest.max_y > merged.max_y) merged.max_y = dest.max_y;

		if (epic12_gpu_count[b] >= 4096) return 1;
		if (merged.max_x - merged.min_x >= 512 || merged.max_y - merged.min_y >= 512)
			return 1;
	}

	bool page_overlap = false;
	if (py0 == py1 && px0 == px1) {
		page_overlap = epic12_gpu_dest_pages[b][(py0 << 6) | px0];
	} else {
		for (int py = py0; py <= py1; ++py) {
			const int row = py << 6;
			for (int px = px0; px <= px1; ++px) {
				if (epic12_gpu_dest_pages[b][row | px]) {
					page_overlap = true;
					break;
				}
			}
			if (page_overlap) break;
		}
	}
	if (page_overlap && epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds[b])) {
		return 2;
	}

	newPages = 0;
	if (py0 == py1 && px0 == px1) {
		const int page = (py0 << 6) | px0;
		if (!epic12_gpu_source_pages[b][page]) {
			newPageList[0] = page;
			newPages = 1;
		}
	} else {
		for (int py = py0; py <= py1; ++py) {
			const int row = py << 6;
			for (int px = px0; px <= px1; ++px) {
				const int page = row | px;
				if (!epic12_gpu_source_pages[b][page]) {
					newPageList[newPages++] = page;
				}
			}
		}
	}
	if (epic12_gpu_source_count[b] + newPages > epic12_gpu_batch_page_capacity())
		return 3;

	return 0;
}

static void epic12_gpu_add_to_bin(int b, const rectangle &merged, unsigned newPages, const int *newPageList,
	int px0, int px1, int py0, int py1,
	int sx, int sy, int x, int y, int w, int h, int flipx, int flipy, int transparent,
	int blend, int dmode, int s_alpha, int d_alpha, const clr_t *tint_clr)
{
	const int dpy0 = y >> 7, dpy1 = (y + h - 1) >> 7;
	const int dpx0 = x >> 7, dpx1 = (x + w - 1) >> 7;
	if (dpy0 == dpy1 && dpx0 == dpx1) {
		epic12_gpu_dest_pages[b][(dpy0 << 6) | dpx0] = true;
	} else {
		for (int dpy = dpy0; dpy <= dpy1; ++dpy) {
			const int row = dpy << 6;
			for (int dpx = dpx0; dpx <= dpx1; ++dpx) {
				epic12_gpu_dest_pages[b][row | dpx] = true;
			}
		}
	}
	if (!epic12_gpu_count[b]) {
		epic12_gpu_bounds[b] = merged;
		epic12_gpu_source_bounds[b] = rectangle(sx, sx + w - 1, sy, sy + h - 1);
		epic12_gpu_add_initial_pages(b, px0, px1, py0, py1);
	} else {
		epic12_gpu_bounds[b] = merged;
		if (sx < epic12_gpu_source_bounds[b].min_x) epic12_gpu_source_bounds[b].min_x = sx;
		if (sy < epic12_gpu_source_bounds[b].min_y) epic12_gpu_source_bounds[b].min_y = sy;
		if (sx + w - 1 > epic12_gpu_source_bounds[b].max_x) epic12_gpu_source_bounds[b].max_x = sx + w - 1;
		if (sy + h - 1 > epic12_gpu_source_bounds[b].max_y) epic12_gpu_source_bounds[b].max_y = sy + h - 1;
		for (unsigned i = 0; i < newPages; ++i) {
			epic12_gpu_source_pages[b][newPageList[i]] = true;
		}
		epic12_gpu_source_count[b] += newPages;
	}
	Epic12GpuCommand &cmd = epic12_gpu_commands[b][epic12_gpu_count[b]++];
	cmd.sx = sx; cmd.sy = sy; cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
	cmd.flipx = !!flipx; cmd.flipy = !!flipy; cmd.transparent = !!transparent;
	cmd.blend = !!blend; cmd.dmode = dmode; cmd.sa = s_alpha; cmd.da = d_alpha; cmd.tint = *tint_clr;
	const unsigned pixels = (unsigned)(w * h);
	epic12_gpu_pixels[b] += pixels;
	epic12_device_blit_delay += pixels;
}

// Return true only when the draw was queued, or is an original no-op.
static bool epic12_gpu_submit(int flipx, int transparent, int blend, int smode, int dmode, BLIT_PARAMS)
{
#if SALVIA_CV1K_PROBE
	SalviaCv1kSampledSpan<64> submitSpan(36);
#endif
	if (!epic12_gpu_enabled) return false;
#ifndef EPIC12_GPU_TEST
	const int norm_dm = (dmode == 4 || dmode == 7 || dmode == 3) ? 0 : dmode;
	if (blend && norm_dm != 0 && !(norm_dm == 1 && s_alpha == 0)) {
		return false;
	}
	if (blend && ((dimy <= 2 && dimx >= 120) || (dimx <= 2 && dimy >= 120) ||
	             (dmode == 4 && tint_clr->r == 0 && tint_clr->g == 0 && tint_clr->b == 0))) {
		return false;
	}
#endif
	if (blend && (smode != 0 || (dmode != 0 && dmode != 1 && dmode != 4 && dmode != 7 && dmode != 3))) {
		++epic12_gpu_reject_blend;
		++epic12_gpu_reject_smode_dmode[smode & 7][dmode & 7];
		return false;
	}
	// Original rasterizers reject horizontal wrap before clipping.
	if (src_x + dimx > 8192) return true;
	int x0 = dst_x_start < clip->min_x ? clip->min_x - dst_x_start : 0;
	int y0 = dst_y_start < clip->min_y ? clip->min_y - dst_y_start : 0;
	int x1 = dimx, y1 = dimy;
	if (dst_x_start + x1 - 1 > clip->max_x) x1 = clip->max_x - dst_x_start + 1;
	if (dst_y_start + y1 - 1 > clip->max_y) y1 = clip->max_y - dst_y_start + 1;
	if (x1 <= x0 || y1 <= y0) return true;
	const int w = x1 - x0, h = y1 - y0;
	if (w > 512 || h > 512) {
		++epic12_gpu_reject_size;
		return false;
	}
	const int x = dst_x_start + x0, y = dst_y_start + y0;
	if (x < 0 || y < 0 || x + w > 8192 || y + h > 4096) {
		++epic12_gpu_reject_oob;
		return false;
	}
	const int sx = src_x + (flipx ? dimx - x1 : x0);
	const int sy = (src_y + (flipy ? dimy - y1 : y0)) & 4095;
	if (sy + h > 4096) {
		++epic12_gpu_reject_sy;
		return false;
	}
	const rectangle dest(x, x + w - 1, y, y + h - 1);
	// In-place copies depend on the original per-pixel write order.
	if (epic12_gpu_intersects(sx, sy, w, h, dest)) {
		++epic12_gpu_reject_intersect;
		return false;
	}
	const int py0 = sy >> 7, py1 = (sy + h - 1) >> 7;
	const int px0 = sx >> 7, px1 = (sx + w - 1) >> 7;

	// Direct CPU blit for off-screen staging buffers (x >= 768):
	// Cave CV1000 games use off-screen VRAM (Zones 1 & 2, x >= 768) as scratchpads
	// to composite multi-part sprites with a few tiny draws (<8,192 px) before
	// blitting them onto the visible display (Buffer 0: x < 320, Buffer 1: 384 <= x < 704).
	// Keeping x < 768 ensures both on-screen double buffers remain fully on the GPU.
	if (x >= 768) {
		if (epic12_gpu_count[0]) {
			// 1. RAW hazard: does this CPU blit read from a page that queued GPU commands wrote to?
			bool page_overlap = false;
			if (py0 == py1 && px0 == px1) {
				page_overlap = epic12_gpu_dest_pages[0][(py0 << 6) | px0];
			} else {
				for (int py = py0; py <= py1; ++py) {
					const int row = py << 6;
					for (int px = px0; px <= px1; ++px) {
						if (epic12_gpu_dest_pages[0][row | px]) { page_overlap = true; break; }
					}
					if (page_overlap) break;
				}
			}
			if (page_overlap && epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds[0])) {
				epic12_gpu_flush_bin(0, EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY);
			}

			// 2. WAR hazard: does this CPU blit write to a page that queued GPU commands sample from?
			if (epic12_gpu_count[0]) {
				const int dpy0 = y >> 7, dpy1 = (y + h - 1) >> 7;
				const int dpx0 = x >> 7, dpx1 = (x + w - 1) >> 7;
				bool war_overlap = false;
				for (int py = dpy0; py <= dpy1 && !war_overlap; ++py) {
					const int row = py << 6;
					for (int px = dpx0; px <= dpx1; ++px) {
						if (epic12_gpu_source_pages[0][row | px]) { war_overlap = true; break; }
					}
				}
				if (war_overlap) {
					epic12_gpu_flush_bin(0, EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY);
				}
			}
		}
		Epic12GpuCommand c;
		c.sx = sx; c.sy = sy; c.x = x; c.y = y; c.w = w; c.h = h;
		c.flipx = !!flipx; c.flipy = !!flipy; c.transparent = !!transparent;
		c.blend = !!blend; c.dmode = dmode; c.sa = s_alpha; c.da = d_alpha; c.tint = *tint_clr;
		epic12_gpu_replay(c);
		epic12_gpu_invalidate(dest);
		return true;
	}

	unsigned np = 0;
	int npl[32];
	rectangle m;
	int fit = epic12_gpu_can_fit(0, dest, sx, sy, w, h, px0, px1, py0, py1, np, npl, m);
	if (fit != 0) {
		Epic12GpuFlushReason reason0 = (fit == 2) ? EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY :
			((fit == 3) ? EPIC12_GPU_FLUSH_SOURCE_PAGES : EPIC12_GPU_FLUSH_REGION);
		epic12_gpu_retire_bin0(reason0);
		fit = epic12_gpu_can_fit(0, dest, sx, sy, w, h, px0, px1, py0, py1, np, npl, m);
	}
	if (fit == 0) {
#if SALVIA_FBNEO_DIAGNOSTICS
		++epic12_gpu_diag_bin_hits[0];
#endif
		epic12_gpu_add_to_bin(0, m, np, npl, px0, px1, py0, py1,
			sx, sy, x, y, w, h, flipx, flipy, transparent, blend, dmode, s_alpha, d_alpha, tint_clr);
		return true;
	}
	return false;
}

static void epic12_gpu_cpu_write(int x, int y, int w, int h,
	Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	// Conservative invalidation also handles a wrapped source/large upload.
	// The upload loop can spill across a VRAM row; a geometric rectangle
	// alone would miss the beginning of the next row in that case.
	if (x < 0 || y < 0 || x+w > 8192 || y+h > 4096) {
		epic12_gpu_flush(reason);
		epic12_gpu_reset();
		return;
	}
	if (reason == EPIC12_GPU_FLUSH_CPU_UPLOAD) {
		bool conflict = false;
		if (epic12_gpu_count[0]) {
			if (epic12_gpu_intersects(x, y, w, h, epic12_gpu_bounds[0])) {
				conflict = true;
			} else {
				const int py0 = y >> 7, py1 = (y + h - 1) >> 7;
				const int px0 = x >> 7, px1 = (x + w - 1) >> 7;
				for (int py = py0; py <= py1 && !conflict; ++py) {
					const int row = py << 6;
					for (int px = px0; px <= px1; ++px) {
						if (epic12_gpu_source_pages[0][row | px]) {
							conflict = true;
							break;
						}
					}
				}
			}
		}
		if (conflict) {
			epic12_gpu_flush(reason);
		}
	} else {
		epic12_gpu_flush(reason);
	}
	rectangle r(x,x+w-1,y,y+h-1);
	epic12_gpu_invalidate(r);
}
// Software sprite rasterizers clip destination writes; uploads can spill rows
// and must keep the conservative cpu_write path above. Always drain queued
// draws, including for an empty rectangle, preserving the existing ordering.
static void epic12_gpu_cpu_draw(int x, int y, int w, int h, const rectangle &clip)
{
	int x0=x>clip.min_x?x:clip.min_x;
	int y0=y>clip.min_y?y:clip.min_y;
	int x1=x+w-1<clip.max_x?x+w-1:clip.max_x;
	int y1=y+h-1<clip.max_y?y+h-1:clip.max_y;
	if (x1<x0 || y1<y0) {
		return;
	}
	epic12_gpu_cpu_write(x0,y0,x1-x0+1,y1-y0+1,EPIC12_GPU_FLUSH_CPU_DRAW);
}
#endif
