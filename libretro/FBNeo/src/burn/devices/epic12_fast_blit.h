// Exact integer path for the fixed source/destination alpha blend used by
// CV1000 effects. Keep the two source rounding/clamping stages in a compact
// table, then saturate all three packed colour channels together.
#ifndef FBNEO_EPIC12_FAST_BLIT_H
#define FBNEO_EPIC12_FAST_BLIT_H

#if defined(_XBOX)
#include <ppcintrinsics.h>
#elif !defined(__dcbt)
#if defined(__powerpc__) && defined(__GNUC__)
static inline void __dcbt_impl(int offset, const void *ptr) {
	__asm__ volatile("dcbt %0, %1" : : "r"(offset), "r"(ptr) : "memory");
}
#define __dcbt(offset, ptr) __dcbt_impl(offset, ptr)
#else
#define __dcbt(offset, ptr) do { (void)(offset); (void)(ptr); } while(0)
#endif
#endif

static UINT8 epic12_source_scale[64][32][32];
static UINT32 epic12_source_scale_r[64][32][32];
static UINT32 epic12_source_scale_g[64][32][32];
static UINT32 epic12_source_scale_b[64][32][32];

static UINT32 epic12_dest_scale_r[32][32];
static UINT32 epic12_dest_scale_g[32][32];
static UINT32 epic12_dest_scale_b[32][32];

static void epic12_init_blend_tables()
{
	for (int tint = 0; tint < 64; tint++) {
		for (int alpha = 0; alpha < 32; alpha++) {
			for (int c = 0; c < 32; c++) {
				UINT8 v = epic12_device_colrtable[epic12_device_colrtable[c][tint]][alpha];
				epic12_source_scale[tint][alpha][c] = v;
				epic12_source_scale_r[tint][alpha][c] = (UINT32)v << 19;
				epic12_source_scale_g[tint][alpha][c] = (UINT32)v << 11;
				epic12_source_scale_b[tint][alpha][c] = (UINT32)v << 3;
			}
		}
	}
	for (int alpha = 0; alpha < 32; alpha++) {
		for (int c = 0; c < 32; c++) {
			UINT8 v = epic12_device_colrtable[alpha][c];
			epic12_dest_scale_r[alpha][c] = (UINT32)v << 19;
			epic12_dest_scale_g[alpha][c] = (UINT32)v << 11;
			epic12_dest_scale_b[alpha][c] = (UINT32)v << 3;
		}
	}
}

static inline UINT32 epic12_packed_add(UINT32 s, UINT32 d)
{
	// Channels occupy bits 3..7 of separate bytes. Their carry bits fit in
	// the gaps, so one word add and a mask implement three saturated adds.
	UINT32 sum = s + d;
	UINT32 carry = sum & 0x01010100;
	return (sum | (carry - (carry >> 5))) & 0x00f8f8f8;
}

template<bool FlipX, bool Transparent, bool FullDestination>
static void epic12_blend_fixed(BLIT_PARAMS)
{
	if (FlipX) src_x += dimx - 1;
	int yf = flipy ? -1 : 1;
	if (flipy) src_y += dimy - 1;

	// Match the existing source-wrap rejection before destination clipping.
	if (FlipX ? ((src_x & 0x1fff) < ((src_x - (dimx - 1)) & 0x1fff))
	          : ((src_x & 0x1fff) > ((src_x + (dimx - 1)) & 0x1fff))) return;
	int x0 = dst_x_start < clip->min_x ? clip->min_x - dst_x_start : 0;
	int y0 = dst_y_start < clip->min_y ? clip->min_y - dst_y_start : 0;
	int x1 = dimx, y1 = dimy;
	if (dst_x_start + x1 - 1 > clip->max_x) x1 = clip->max_x - dst_x_start + 1;
	if (dst_y_start + y1 - 1 > clip->max_y) y1 = clip->max_y - dst_y_start + 1;
	if (x1 <= x0 || y1 <= y0) return;
	epic12_device_blit_delay += (y1 - y0) * (x1 - x0);
	// Blit-path census: the exact integer blend draws this rectangle. A row is
	// charged once here rather than per pixel so the probe stays out of the
	// inner loop it is measuring.
	EPIC12_BLIT_PATH_CHARGE((y1 - y0) * (x1 - x0));
	const UINT32 *red = epic12_source_scale_r[tint_clr->r][s_alpha];
	const UINT32 *green = epic12_source_scale_g[tint_clr->g][s_alpha];
	const UINT32 *blue = epic12_source_scale_b[tint_clr->b][s_alpha];
	const UINT32 *dred = epic12_dest_scale_r[d_alpha];
	const UINT32 *dgreen = epic12_dest_scale_g[d_alpha];
	const UINT32 *dblue = epic12_dest_scale_b[d_alpha];
	const int width_bytes = (x1 - x0) * (int)sizeof(UINT32);

	const UINT32 *first_in = gfx + (((src_y + yf * y0) & 0x0fff) * 0x2000) + src_x + (FlipX ? -x0 : x0);
	const UINT32 *first_out = m_bitmaps + (dst_y_start + y0) * 0x2000 + dst_x_start + x0;
	for (int b = 0; b < width_bytes; b += 128) {
		__dcbt(FlipX ? -b : b, first_in);
		__dcbt(b, first_out);
	}
	if (width_bytes > 0) {
		__dcbt(FlipX ? -(width_bytes - 1) : (width_bytes - 1), first_in);
		__dcbt(width_bytes - 1, first_out);
	}

	for (int y = y0; y < y1; y++) {
		if (y + 1 < y1) {
			const UINT32 *next_in = gfx + (((src_y + yf * (y + 1)) & 0x0fff) * 0x2000) + src_x + (FlipX ? -x0 : x0);
			const UINT32 *next_out = m_bitmaps + (dst_y_start + y + 1) * 0x2000 + dst_x_start + x0;
			for (int b = 0; b < width_bytes; b += 128) {
				__dcbt(FlipX ? -b : b, next_in);
				__dcbt(b, next_out);
			}
			if (width_bytes > 0) {
				__dcbt(FlipX ? -(width_bytes - 1) : (width_bytes - 1), next_in);
				__dcbt(width_bytes - 1, next_out);
			}
		}
		UINT32 *out = m_bitmaps + (dst_y_start + y) * 0x2000 + dst_x_start + x0;
		const UINT32 *in = gfx + (((src_y + yf * y) & 0x0fff) * 0x2000) + src_x + (FlipX ? -x0 : x0);
		const bool same_row = (gfx == m_bitmaps) && (((src_y + yf * y) & 0x0fff) == (dst_y_start + y));
		int x = x0;
		if (!same_row && Transparent) {
			for (; x + 1 < x1; x += 2) {
				UINT32 pen0 = *in;
				UINT32 pen1 = in[FlipX ? -1 : 1];
				if (!((pen0 | pen1) & 0x20000000)) {
					out += 2;
					in += FlipX ? -2 : 2;
					continue;
				}
				if ((pen0 & pen1) & 0x20000000) {
					UINT32 s0 = red[(pen0 >> 19) & 31] |
					           green[(pen0 >> 11) & 31] |
					           blue[(pen0 >> 3) & 31];
					UINT32 s1 = red[(pen1 >> 19) & 31] |
					           green[(pen1 >> 11) & 31] |
					           blue[(pen1 >> 3) & 31];
					UINT32 d0 = out[0];
					UINT32 d1 = out[1];
					if (FullDestination) {
						d0 &= 0x00f8f8f8;
						d1 &= 0x00f8f8f8;
					} else {
						d0 = dred[(d0 >> 19) & 31] |
						     dgreen[(d0 >> 11) & 31] |
						     dblue[(d0 >> 3) & 31];
						d1 = dred[(d1 >> 19) & 31] |
						     dgreen[(d1 >> 11) & 31] |
						     dblue[(d1 >> 3) & 31];
					}
					out[0] = epic12_packed_add(s0, d0) | 0x20000000;
					out[1] = epic12_packed_add(s1, d1) | 0x20000000;
				} else {
					if (pen0 & 0x20000000) {
						UINT32 s0 = red[(pen0 >> 19) & 31] |
						           green[(pen0 >> 11) & 31] |
						           blue[(pen0 >> 3) & 31];
						UINT32 d0 = out[0];
						if (FullDestination) d0 &= 0x00f8f8f8;
						else d0 = dred[(d0 >> 19) & 31] |
						         dgreen[(d0 >> 11) & 31] |
						         dblue[(d0 >> 3) & 31];
						out[0] = epic12_packed_add(s0, d0) | 0x20000000;
					}
					if (pen1 & 0x20000000) {
						UINT32 s1 = red[(pen1 >> 19) & 31] |
						           green[(pen1 >> 11) & 31] |
						           blue[(pen1 >> 3) & 31];
						UINT32 d1 = out[1];
						if (FullDestination) d1 &= 0x00f8f8f8;
						else d1 = dred[(d1 >> 19) & 31] |
						         dgreen[(d1 >> 11) & 31] |
						         dblue[(d1 >> 3) & 31];
						out[1] = epic12_packed_add(s1, d1) | 0x20000000;
					}
				}
				out += 2;
				in += FlipX ? -2 : 2;
			}
		} else if (!same_row) {
			for (; x + 1 < x1; x += 2) {
				UINT32 pen0 = *in;
				UINT32 pen1 = in[FlipX ? -1 : 1];
				UINT32 s0 = red[(pen0 >> 19) & 31] |
				           green[(pen0 >> 11) & 31] |
				           blue[(pen0 >> 3) & 31];
				UINT32 s1 = red[(pen1 >> 19) & 31] |
				           green[(pen1 >> 11) & 31] |
				           blue[(pen1 >> 3) & 31];
				UINT32 d0 = out[0];
				UINT32 d1 = out[1];
				if (FullDestination) {
					d0 &= 0x00f8f8f8;
					d1 &= 0x00f8f8f8;
				} else {
					d0 = dred[(d0 >> 19) & 31] |
					     dgreen[(d0 >> 11) & 31] |
					     dblue[(d0 >> 3) & 31];
					d1 = dred[(d1 >> 19) & 31] |
					     dgreen[(d1 >> 11) & 31] |
					     dblue[(d1 >> 3) & 31];
				}
				out[0] = epic12_packed_add(s0, d0) | (pen0 & 0x20000000);
				out[1] = epic12_packed_add(s1, d1) | (pen1 & 0x20000000);
				out += 2;
				in += FlipX ? -2 : 2;
			}
		}
		for (; x < x1; x++, out++, in += FlipX ? -1 : 1) {
			UINT32 pen = *in;
			if (Transparent && !(pen & 0x20000000)) continue;
			UINT32 s = red[(pen >> 19) & 31] |
			           green[(pen >> 11) & 31] |
			           blue[(pen >> 3) & 31];
			UINT32 d = *out;
			if (FullDestination) d &= 0x00f8f8f8;
			else d = dred[(d >> 19) & 31] |
			         dgreen[(d >> 11) & 31] |
			         dblue[(d >> 3) & 31];
			*out = epic12_packed_add(s, d) | (Transparent ? 0x20000000 : (pen & 0x20000000));
		}
	}
}

static void epic12_draw_fixed(int flipx, int transparent, BLIT_PARAMS)
{
#define EPIC12_FIXED_CALL(f,t,d) epic12_blend_fixed<f,t,d>(clip,gfx,src_x,src_y,dst_x_start,dst_y_start,dimx,dimy,flipy,s_alpha,d_alpha,tint_clr)
	if (d_alpha == 31) {
		if (flipx) { if (transparent) EPIC12_FIXED_CALL(true,true,true); else EPIC12_FIXED_CALL(true,false,true); }
		else { if (transparent) EPIC12_FIXED_CALL(false,true,true); else EPIC12_FIXED_CALL(false,false,true); }
	} else {
		if (flipx) { if (transparent) EPIC12_FIXED_CALL(true,true,false); else EPIC12_FIXED_CALL(true,false,false); }
		else { if (transparent) EPIC12_FIXED_CALL(false,true,false); else EPIC12_FIXED_CALL(false,false,false); }
	}
#undef EPIC12_FIXED_CALL
}
#endif
