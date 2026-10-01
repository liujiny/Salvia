// CV1000 GPU command batching. CPU VRAM remains authoritative between lists.
// Included by epic12.cpp after the original software rasterizers.
#ifndef FBNEO_EPIC12_GPU_H
#define FBNEO_EPIC12_GPU_H

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

static void epic12_gpu_diag_reset()
{
	memset(epic12_gpu_flush_reasons,0,sizeof(epic12_gpu_flush_reasons));
	epic12_gpu_flush_small_cpu=0;
	epic12_gpu_flush_render_fallback=0;
}

static bool epic12_gpu_intersects(int x, int y, int w, int h, const rectangle &r)
{
	return x <= r.max_x && x + w > r.min_x && y <= r.max_y && y + h > r.min_y;
}

static void epic12_gpu_replay(const Epic12GpuCommand &c)
{
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
	if (epic12_gpu_pixels < 8192) {
		++epic12_gpu_flush_small_cpu;
	} else if (epic12_gpu_render(epic12_gpu_commands, epic12_gpu_count, epic12_gpu_bounds)) {
		rendered=true;
	} else {
		++epic12_gpu_flush_render_fallback;
	}
	if (!rendered) {
		UINT64 delay = epic12_device_blit_delay;
		for (int i = 0; i < epic12_gpu_count; ++i) epic12_gpu_replay(epic12_gpu_commands[i]);
		epic12_device_blit_delay = delay;
	}
	epic12_gpu_invalidate(epic12_gpu_bounds);
	epic12_gpu_count = 0;
	epic12_gpu_pixels = 0;
	epic12_gpu_source_count = 0;
	memset(epic12_gpu_source_pages,0,sizeof(epic12_gpu_source_pages));
}

// Return true only when the draw was queued, or is an original no-op.
static bool epic12_gpu_submit(int flipx, int transparent, int blend, int smode, int dmode, BLIT_PARAMS)
{
	if (!epic12_gpu_enabled) return false;
	if (blend && (smode != 0 || (dmode != 0 && dmode != 1 && dmode != 4))) return false;
	// Original rasterizers reject horizontal wrap before clipping.
	if (src_x + dimx > 8192) return true;
	int x0 = dst_x_start < clip->min_x ? clip->min_x - dst_x_start : 0;
	int y0 = dst_y_start < clip->min_y ? clip->min_y - dst_y_start : 0;
	int x1 = dimx, y1 = dimy;
	if (dst_x_start + x1 - 1 > clip->max_x) x1 = clip->max_x - dst_x_start + 1;
	if (dst_y_start + y1 - 1 > clip->max_y) y1 = clip->max_y - dst_y_start + 1;
	if (x1 <= x0 || y1 <= y0) return true;
	Epic12GpuCommand c;
	c.sx = src_x + (flipx ? dimx - x1 : x0);
	c.sy = (src_y + (flipy ? dimy - y1 : y0)) & 4095;
	c.x = dst_x_start + x0; c.y = dst_y_start + y0;
	c.w = x1 - x0; c.h = y1 - y0;
	c.flipx = !!flipx; c.flipy = !!flipy; c.transparent = !!transparent;
	c.blend = !!blend; c.dmode = dmode; c.sa = s_alpha; c.da = d_alpha; c.tint = *tint_clr;
	if (c.x < 0 || c.y < 0 || c.x + c.w > 8192 || c.y + c.h > 4096 ||
		c.sy + c.h > 4096 || c.w > 512 || c.h > 512) return false;
	rectangle dest(c.x,c.x+c.w-1,c.y,c.y+c.h-1);
	// In-place copies depend on the original per-pixel write order.
	if (epic12_gpu_intersects(c.sx,c.sy,c.w,c.h,dest)) return false;
	unsigned newPages=0;
	for (int py=c.sy/128;py<=(c.sy+c.h-1)/128;++py)
		for (int px=c.sx/128;px<=(c.sx+c.w-1)/128;++px)
			if (!epic12_gpu_source_pages[py*64+px]) ++newPages;
	if (epic12_gpu_source_count+newPages>epic12_gpu_batch_page_capacity())
		epic12_gpu_flush(EPIC12_GPU_FLUSH_SOURCE_PAGES);
	if (epic12_gpu_count) {
		rectangle merged = epic12_gpu_bounds;
		if (c.x < merged.min_x) merged.min_x = c.x;
		if (c.y < merged.min_y) merged.min_y = c.y;
		if (dest.max_x > merged.max_x) merged.max_x = dest.max_x;
		if (dest.max_y > merged.max_y) merged.max_y = dest.max_y;
		if (epic12_gpu_count == 1024)
			epic12_gpu_flush(EPIC12_GPU_FLUSH_COMMAND_LIMIT);
		else if (merged.max_x - merged.min_x >= 512 || merged.max_y - merged.min_y >= 512)
			epic12_gpu_flush(EPIC12_GPU_FLUSH_REGION);
		else if (epic12_gpu_intersects(c.sx,c.sy,c.w,c.h,epic12_gpu_bounds))
			epic12_gpu_flush(EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY);
		else
			epic12_gpu_bounds = merged;
	}
	if (!epic12_gpu_count) epic12_gpu_bounds = dest;
	for (int py=c.sy/128;py<=(c.sy+c.h-1)/128;++py)
		for (int px=c.sx/128;px<=(c.sx+c.w-1)/128;++px) {
			int page=py*64+px;
			if (!epic12_gpu_source_pages[page]) { epic12_gpu_source_pages[page]=true; ++epic12_gpu_source_count; }
		}
	epic12_gpu_commands[epic12_gpu_count++] = c;
	epic12_gpu_pixels += c.w * c.h;
	epic12_device_blit_delay += c.w * c.h;
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
