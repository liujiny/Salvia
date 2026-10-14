// Exact validation of the already bounded SH3 instruction snapshot.
// Both operands contain UINT16 objects. Never round the length up, read past
// the mapped source page, or require stronger alignment than one SH3 opcode.
#ifndef FBNEO_SH3_DRC_SOURCE_CHECK_H
#define FBNEO_SH3_DRC_SOURCE_CHECK_H

#if defined(_MSC_VER)
#define SH3_SOURCE_INLINE __forceinline
#else
#define SH3_SOURCE_INLINE inline
#endif

static SH3_SOURCE_INLINE bool sh3_drc_source_equal(const UINT16 *expected,
 const UINT16 *actual, unsigned words)
{
 // Compare opcodes using bitwise XOR and OR. Keeping a group of four avoids
 // per-opcode loop overhead while evaluating 4 opcodes with a single conditional branch.
 // Bitwise OR cannot cancel non-zero differences; equality semantics remain 100% exact.
 while(words>=4) {
  unsigned diff = (unsigned)(expected[0] ^ actual[0])
                | (unsigned)(expected[1] ^ actual[1])
                | (unsigned)(expected[2] ^ actual[2])
                | (unsigned)(expected[3] ^ actual[3]);
  if(diff) return false;
  expected+=4; actual+=4; words-=4;
 }
 switch(words) {
  case 3:
   return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]));
  case 2:
   return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]));
  case 1:
   return expected[0] == actual[0];
  default:
   return true;
 }
}
#undef SH3_SOURCE_INLINE
#endif
