// Exact validation of the already bounded SH3 instruction snapshot.
// Both operands contain UINT16 objects. Never round the length up, read past
// the mapped source page, or require stronger alignment than one SH3 opcode.
#ifndef FBNEO_SH3_DRC_SOURCE_CHECK_H
#define FBNEO_SH3_DRC_SOURCE_CHECK_H

#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#define SH3_SOURCE_INLINE __forceinline
#else
#define SH3_SOURCE_INLINE inline
#endif

static SH3_SOURCE_INLINE uint64_t sh3_load64_unaligned(const void *p)
{
	uint64_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static SH3_SOURCE_INLINE uint32_t sh3_load32_unaligned(const void *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static SH3_SOURCE_INLINE bool sh3_drc_source_equal(const UINT16 *expected,
 const UINT16 *actual, unsigned words)
{
	if (expected == actual) return true;
	if (words <= 4) {
		switch (words) {
			case 0: return true;
			case 1: return expected[0] == actual[0];
			case 2: return sh3_load32_unaligned(expected) == sh3_load32_unaligned(actual);
			case 3: return (sh3_load32_unaligned(expected) == sh3_load32_unaligned(actual)) & (expected[2] == actual[2]);
			case 4: return sh3_load64_unaligned(expected) == sh3_load64_unaligned(actual);
		}
	}
	while (words >= 16) {
		const uint64_t diff = (sh3_load64_unaligned(expected) ^ sh3_load64_unaligned(actual))
		                    | (sh3_load64_unaligned(expected + 4) ^ sh3_load64_unaligned(actual + 4))
		                    | (sh3_load64_unaligned(expected + 8) ^ sh3_load64_unaligned(actual + 8))
		                    | (sh3_load64_unaligned(expected + 12) ^ sh3_load64_unaligned(actual + 12));
		if (diff) return false;
		expected += 16; actual += 16; words -= 16;
	}
	while (words >= 8) {
		const uint64_t diff = (sh3_load64_unaligned(expected) ^ sh3_load64_unaligned(actual))
		                    | (sh3_load64_unaligned(expected + 4) ^ sh3_load64_unaligned(actual + 4));
		if (diff) return false;
		expected += 8; actual += 8; words -= 8;
	}
	while (words >= 4) {
		if (sh3_load64_unaligned(expected) != sh3_load64_unaligned(actual)) return false;
		expected += 4; actual += 4; words -= 4;
	}
	while (words >= 2) {
		if (sh3_load32_unaligned(expected) != sh3_load32_unaligned(actual)) return false;
		expected += 2; actual += 2; words -= 2;
	}
	if (words) {
		return *expected == *actual;
	}
	return true;
}
#undef SH3_SOURCE_INLINE
#endif
