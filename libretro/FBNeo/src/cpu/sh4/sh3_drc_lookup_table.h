// Included inside Sh3Ppc after CACHE_SETS and WAYS are defined.
// Keep hot tags contiguous; miss-only replacement cursors occupy the tail.
#ifndef FBNEO_SH3_DRC_LOOKUP_TABLE_H
#define FBNEO_SH3_DRC_LOOKUP_TABLE_H
struct LookupTable {
 UINT32 tag[CACHE_SETS][WAYS];
 unsigned next[CACHE_SETS];
};
// One allocation and the same byte count as the old 20-byte interleaved sets.
typedef char LookupTagStrideMustBe16[(WAYS==4 && sizeof(UINT32)==4 &&
 sizeof(((LookupTable*)0)->tag[0])==16) ? 1 : -1];
typedef char LookupCursorMustBe32Bits[(sizeof(unsigned)==4) ? 1 : -1];
typedef char LookupAllocationUnchanged[(sizeof(LookupTable)==
 CACHE_SETS*(WAYS*sizeof(UINT32)+sizeof(unsigned))) ? 1 : -1];
#endif
