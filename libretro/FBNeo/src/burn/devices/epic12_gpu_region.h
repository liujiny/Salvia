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
// one, and copies the body 16 bytes at a time.
//
// The rectangle is exact in both directions: bytes of a destination row beyond
// `width` are never read or written, and the copy rounds nothing out to a
// vector or a cache line.
#ifndef FBNEO_EPIC12_GPU_REGION_H
#define FBNEO_EPIC12_GPU_REGION_H
#include <stddef.h>

#if defined(_XBOX)
#include <ppcintrinsics.h>
#include <vectorintrinsics.h>
#define EPIC12_REGION_VECTOR 1
#elif defined(EPIC12_GPU_REGION_VECTOR_TEST)
// The standalone test supplies byte-accurate vector load/store intrinsics.
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
	if (((dstPitch | srcPitch) & 15) == 0) {
		// Both rows keep their byte alignment across the copy -- both pitches
		// are multiples of 16 -- so the split is decided once, outside the row
		// loop. The head aligns the load: an aligned load with an
		// unaligned-safe store is the pair this transfer already uses for the
		// readback, and it is the pair the SDK provides.
		const size_t align = (size_t)in & 15;
		const size_t head = align ? 16 - align : 0;
		const size_t headBytes = head < width ? head : width;
		const size_t bodyEnd = head < width ? head + (((width - head) >> 4) << 4) : width;
		const bool alignedStore = ((((size_t)out ^ (size_t)in) & 15) == 0);
		for (size_t y = 0; y < rows; ++y, out += dstPitch, in += srcPitch) {
			if (y + EPIC12_REGION_PREFETCH_ROWS < rows) {
				// dcbt is a hint: it cannot fault and cannot change a value.
				const unsigned char *ahead = in + EPIC12_REGION_PREFETCH_ROWS * srcPitch;
				for (size_t x = 0; x < width; x += 128) __dcbt((int)x, ahead);
			}
			size_t x = 0;
			// Never read or write a byte outside [0,width): a row shorter than
			// the alignment head copies no head at all.
			for (; x < headBytes; ++x) out[x] = in[x];
			for (; x < bodyEnd; x += 16) {
				const __vector4 value = __lvx(in + x, 0);
				if (alignedStore) __stvx(value, out + x, 0);
				else __storeunalignedvector(value, out + x);
			}
			for (; x < width; ++x) out[x] = in[x];
		}
		return;
	}
#endif
	// A pitch that is not a multiple of 16 would move the alignment from row to
	// row, so the split above cannot be hoisted. No caller has one.
	for (size_t y = 0; y < rows; ++y, out += dstPitch, in += srcPitch)
		for (size_t x = 0; x < width; ++x) out[x] = in[x];
}

#undef EPIC12_REGION_VECTOR
#endif
