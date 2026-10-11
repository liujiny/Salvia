// Row-strided rectangle copy out of CPU VRAM into a locked Xenos texture: one
// batch's bounding box, which is what the compositor hands the input texture
// every batch.
//
// `width` bytes from each of `rows` source rows, whose pitch is 8192 words
// (32 KB) in VRAM, are written to rows whose pitch is the driver's texture
// pitch. Neither side is contiguous and no row is longer than 2 KB, so one
// memcpy() per row hands the copy to a routine that sizes itself for a single
// long run of bytes: it cannot know that the next source line is 32 KB away
// and that the copy will never come back for the lines in between. This helper
// does know, so it fetches the source row a few rows ahead while copying this
// one.
//
// The destination is a locked texture, and on Xenon that is GPU write-combined
// memory. epic12_gpu_tile.h writes the same memory and states the rule for it:
// "all writes are aligned and sequential ... Never read it or use unaligned
// stores." The first version of this helper kept the unaligned pair on the
// store side -- legal, and in use, for the cached memory epic12_gpu_untile.h
// writes -- and the console went back to the dashboard the moment a state
// exercised that branch. So the store decides the shape of the loop here: a
// store must land on a 16-byte boundary, the source is the side allowed to be
// off, and anything with no aligned-store form falls back to memcpy() a row at
// a time, which is what the caller did before this helper existed.
//
// The rectangle is exact in both directions: bytes of a destination row beyond
// `width` are never read or written, and the copy rounds nothing out to a
// vector or a cache line.
#ifndef FBNEO_EPIC12_GPU_REGION_H
#define FBNEO_EPIC12_GPU_REGION_H
#include <stddef.h>
#include <string.h>

#if defined(_XBOX)
#include <ppcintrinsics.h>
#include <vectorintrinsics.h>
#define EPIC12_REGION_VECTOR 1
#elif defined(EPIC12_GPU_REGION_VECTOR_TEST)
// The standalone test supplies byte-accurate vector load/store intrinsics and
// asserts the alignment and bounds rules the SDK intrinsics require.
#define EPIC12_REGION_VECTOR 1
#endif

// Rows fetched ahead of the row being copied. The source rows are 32 KB apart,
// so the distance is small in rows and large in bytes.
#define EPIC12_REGION_PREFETCH_ROWS 4

static void epic12_gpu_copy_rows(void *destination, size_t dstPitch,
	const void *source, size_t srcPitch, size_t width, size_t rows)
{
	if (!width || !rows) return;
	unsigned char *out = (unsigned char*)destination;
	const unsigned char *in = (const unsigned char*)source;
#if defined(EPIC12_REGION_VECTOR)
	// Both pitches are multiples of 16, so both sides keep their alignment
	// down the rectangle and the split is decided once, outside the row loop.
	// A destination that is not vector aligned, or a source that is not even
	// word aligned, has no aligned-store form at all.
	if ((((size_t)out & 15) == 0) && (((size_t)in & 3) == 0) &&
		(((dstPitch | srcPitch) & 15) == 0)) {
		const size_t body = (width >> 4) << 4;
		for (size_t y = 0; y < rows; ++y, out += dstPitch, in += srcPitch) {
			if (y + EPIC12_REGION_PREFETCH_ROWS < rows) {
				// dcbt is a hint: it cannot fault and cannot change a value.
				const unsigned char *ahead = in + EPIC12_REGION_PREFETCH_ROWS * srcPitch;
				for (size_t x = 0; x < width; x += 128) __dcbt((int)x, ahead);
			}
			size_t x = 0;
			// The store leads and the load follows it: 16 bytes written at a
			// 16-byte boundary, 16 bytes read from anywhere, and the load
			// covers exactly the bytes the store writes, so neither side ever
			// leaves [0,width) of the row.
			for (; x < body; x += 16)
				__stvx_volatile(__loadunalignedvector((const unsigned int*)(in + x)), out + x, 0);
			// A row that is not a whole number of vectors ends in plain bytes.
			for (; x < width; ++x) out[x] = in[x];
		}
		return;
	}
#endif
	// No aligned-store form: the same copy, a row at a time, by the routine
	// that handles its own alignment.
	for (size_t y = 0; y < rows; ++y, out += dstPitch, in += srcPitch)
		memcpy(out, in, width);
}

#undef EPIC12_REGION_VECTOR
#endif
