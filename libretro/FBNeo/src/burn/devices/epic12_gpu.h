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

static Epic12GpuCommand epic12_gpu_commands[1024];
static int epic12_gpu_count;
static unsigned epic12_gpu_pixels;
static bool epic12_gpu_source_pages[2048];
static unsigned epic12_gpu_source_count;
static rectangle epic12_gpu_bounds;
static bool epic12_gpu_enabled;

// Batches below this many pixels are rasterized on the CPU instead of being
// transferred to the Xenos. The split point was chosen against ddpdfk-like
// traffic; epic12.cpp's gfx_exec() re-reads it from the DIPs each list so a
// console A/B can move it without a rebuild.
static unsigned epic12_gpu_threshold = 8192;
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
#endif
}

static inline bool epic12_gpu_intersects(int x, int y, int w, int h, const rectangle &r)
{
	return x <= r.max_x && x + w > r.min_x && y <= r.max_y && y + h > r.min_y;
}

static inline void epic12_gpu_add_initial_pages(int px0, int px1, int py0, int py1)
{
	if (py0 == py1 && px0 == px1) {
		const int page = (py0 << 6) | px0;
		epic12_gpu_source_pages[page] = true;
		epic12_gpu_source_count = 1;
	} else {
		for (int py = py0; py <= py1; ++py) {
			const int row = py << 6;
			for (int px = px0; px <= px1; ++px) {
				const int page = row | px;
				if (!epic12_gpu_source_pages[page]) {
					epic12_gpu_source_pages[page] = true;
					++epic12_gpu_source_count;
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

static void epic12_gpu_flush(Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	if (!epic12_gpu_count) return;
	++epic12_gpu_flush_reasons[reason];
	// Small batches cost more to transfer than to rasterize. Also retain an
	// exact CPU replay if allocation, shader validation or readback fails.
	bool rendered=false;
#if SALVIA_CV1K_PROBE
	// The blitter job is timed as one number by the worker. Split it by path
	// so a window can weigh Xenos batches against CPU replay. The first batch
	// of a session compiles shaders and runs the pixel self-test, so it is
	// excluded until the compositor is validated.
	bool probeSteady=epic12_xenos_validated;
	unsigned long long probeStart=salvia_cv1k_tick();
#endif
	// The compositor comes up on the first render call, and a live Xenos is
	// what switches the core to the asynchronous present. Raising the
	// threshold must therefore still bring it up once, or the comparison would
	// move the present mode as well as the batches.
	bool routeCpu = epic12_gpu_pixels < epic12_gpu_threshold;
#ifdef _XBOX
	if (routeCpu && epic12_gpu_force_bringup && !epic12_xenos_validated && !epic12_xenos_failed) routeCpu=false;
#endif
	if (routeCpu) {
		++epic12_gpu_flush_small_cpu;
#if SALVIA_FBNEO_DIAGNOSTICS
		int sb = epic12_gpu_pixels < 512 ? 0 : (epic12_gpu_pixels < 2048 ? 1 : (epic12_gpu_pixels < 4096 ? 2 : (epic12_gpu_pixels < 8192 ? 3 : 4)));
		++epic12_gpu_diag_small_cpu_buckets[sb];
#endif
	} else if (epic12_gpu_render(epic12_gpu_commands, epic12_gpu_count, epic12_gpu_bounds)) {
		rendered=true;
	} else {
		++epic12_gpu_flush_render_fallback;
	}
	if (!rendered) {
		UINT64 delay = epic12_device_blit_delay;
		for (int i = 0; i < epic12_gpu_count; ++i) epic12_gpu_replay(epic12_gpu_commands[i]);
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
					salvia_cv1k_probe[19]+=epic12_gpu_pixels;
				}
			} else {
				salvia_cv1k_probe[16]+=ticks; ++salvia_cv1k_probe[17];
				salvia_cv1k_probe[18]+=epic12_gpu_pixels;
				unsigned bucket=0;
				while (bucket+1<EPIC12_GPU_REPLAY_BUCKETS &&
				       epic12_gpu_pixels>=epic12_gpu_replay_bucket_px[bucket]) ++bucket;
				salvia_cv1k_probe[24+bucket*3]+=ticks;
				salvia_cv1k_probe[25+bucket*3]+=epic12_gpu_pixels;
				++salvia_cv1k_probe[26+bucket*3];
			}
		}
	}
#endif
	epic12_gpu_invalidate(epic12_gpu_bounds);
	epic12_gpu_count = 0;
	epic12_gpu_pixels = 0;
	epic12_gpu_source_count = 0;
	memset(epic12_gpu_source_pages,0,sizeof(epic12_gpu_source_pages));
}

// Return true only when the draw was queued, or is an original no-op.
static bool epic12_gpu_submit(int flipx, int transparent, int blend, int smode, int dmode, BLIT_PARAMS)
{
#if SALVIA_CV1K_PROBE
	SalviaCv1kSpan submitSpan(36);
#endif
	if (!epic12_gpu_enabled) return false;
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
	if (epic12_gpu_count) {
		rectangle merged = epic12_gpu_bounds;
		if (x < merged.min_x) merged.min_x = x;
		if (y < merged.min_y) merged.min_y = y;
		if (dest.max_x > merged.max_x) merged.max_x = dest.max_x;
		if (dest.max_y > merged.max_y) merged.max_y = dest.max_y;
		if (epic12_gpu_count == 1024)
			epic12_gpu_flush(EPIC12_GPU_FLUSH_COMMAND_LIMIT);
		else if (merged.max_x - merged.min_x >= 512 || merged.max_y - merged.min_y >= 512) {
#if SALVIA_FBNEO_DIAGNOSTICS
			if (merged.max_x - merged.min_x >= 512) {
				++epic12_gpu_diag_reg_exceed_x;
				if (clip->max_x > 1000) ++epic12_gpu_diag_reg_clip_fs;
				else ++epic12_gpu_diag_reg_clip_scr;
				int span = merged.max_x - merged.min_x;
				if (span < 1024) ++epic12_gpu_diag_reg_span[0];
				else if (span < 2048) ++epic12_gpu_diag_reg_span[1];
				else if (span < 4096) ++epic12_gpu_diag_reg_span[2];
				else ++epic12_gpu_diag_reg_span[3];
				if (epic12_gpu_diag_reg_sample_count < 5) {
					int s = epic12_gpu_diag_reg_sample_count++;
					epic12_gpu_diag_reg_samples[s][0] = epic12_gpu_bounds.min_x;
					epic12_gpu_diag_reg_samples[s][1] = epic12_gpu_bounds.max_x;
					epic12_gpu_diag_reg_samples[s][2] = dest.min_x;
					epic12_gpu_diag_reg_samples[s][3] = dest.max_x;
					epic12_gpu_diag_reg_samples[s][4] = clip->min_x;
					epic12_gpu_diag_reg_samples[s][5] = clip->max_x;
				}
			}
			if (merged.max_y - merged.min_y >= 512) ++epic12_gpu_diag_reg_exceed_y;
			int rb = epic12_gpu_count == 1 ? 0 : (epic12_gpu_count <= 4 ? 1 : (epic12_gpu_count <= 16 ? 2 : (epic12_gpu_count <= 64 ? 3 : 4)));
			++epic12_gpu_diag_reg_count_buckets[rb];
#endif
			epic12_gpu_flush(EPIC12_GPU_FLUSH_REGION);
		} else if (epic12_gpu_intersects(sx, sy, w, h, epic12_gpu_bounds)) {
			bool real_dep = false;
			const int sx2 = sx + w;
			const int sy2 = sy + h;
			for (int i = 0; i < epic12_gpu_count; ++i) {
				const Epic12GpuCommand &c = epic12_gpu_commands[i];
				if (sx < c.x + c.w && sx2 > c.x && sy < c.y + c.h && sy2 > c.y) {
					real_dep = true;
					break;
				}
			}
			if (real_dep) {
#if SALVIA_FBNEO_DIAGNOSTICS
				++epic12_gpu_diag_dep_true;
				int db = epic12_gpu_count == 1 ? 0 : (epic12_gpu_count <= 4 ? 1 : (epic12_gpu_count <= 16 ? 2 : (epic12_gpu_count <= 64 ? 3 : 4)));
				++epic12_gpu_diag_dep_count_buckets[db];
#endif
				epic12_gpu_flush(EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY);
			} else {
#if SALVIA_FBNEO_DIAGNOSTICS
				++epic12_gpu_diag_dep_false;
#endif
				epic12_gpu_bounds = merged;
			}
		} else
			epic12_gpu_bounds = merged;
	}
	if (!epic12_gpu_count) {
		epic12_gpu_bounds = dest;
		epic12_gpu_add_initial_pages(px0, px1, py0, py1);
	} else {
		unsigned newPages = 0;
		int newPageList[32];
		if (py0 == py1 && px0 == px1) {
			const int page = (py0 << 6) | px0;
			if (!epic12_gpu_source_pages[page]) {
				newPageList[0] = page;
				newPages = 1;
			}
		} else {
			for (int py = py0; py <= py1; ++py) {
				const int row = py << 6;
				for (int px = px0; px <= px1; ++px) {
					const int page = row | px;
					if (!epic12_gpu_source_pages[page]) {
						newPageList[newPages++] = page;
					}
				}
			}
		}
		if (epic12_gpu_source_count + newPages > epic12_gpu_batch_page_capacity()) {
			epic12_gpu_flush(EPIC12_GPU_FLUSH_SOURCE_PAGES);
			epic12_gpu_bounds = dest;
			epic12_gpu_add_initial_pages(px0, px1, py0, py1);
		} else {
			for (unsigned i = 0; i < newPages; ++i) {
				epic12_gpu_source_pages[newPageList[i]] = true;
			}
			epic12_gpu_source_count += newPages;
		}
	}
	Epic12GpuCommand &cmd = epic12_gpu_commands[epic12_gpu_count++];
	cmd.sx = sx; cmd.sy = sy; cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
	cmd.flipx = !!flipx; cmd.flipy = !!flipy; cmd.transparent = !!transparent;
	cmd.blend = !!blend; cmd.dmode = dmode; cmd.sa = s_alpha; cmd.da = d_alpha; cmd.tint = *tint_clr;
	const unsigned pixels = (unsigned)(w * h);
	epic12_gpu_pixels += pixels;
	epic12_device_blit_delay += pixels;
	return true;
}

static void epic12_gpu_cpu_write(int x, int y, int w, int h,
	Epic12GpuFlushReason reason = EPIC12_GPU_FLUSH_OTHER)
{
	epic12_gpu_flush(reason);
	// Conservative invalidation also handles a wrapped source/large upload.
	// The upload loop can spill across a VRAM row; a geometric rectangle
	// alone would miss the beginning of the next row in that case.
	if (x < 0 || y < 0 || x+w > 8192 || y+h > 4096) {
		epic12_gpu_reset();
		return;
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
		epic12_gpu_flush(EPIC12_GPU_FLUSH_CPU_DRAW);
		return;
	}
	// cpu_write still resets the cache if the clipped rectangle is outside
	// physical VRAM. Do not assume that every configured clip is in bounds.
	epic12_gpu_cpu_write(x0,y0,x1-x0+1,y1-y0+1,EPIC12_GPU_FLUSH_CPU_DRAW);
}
#endif
