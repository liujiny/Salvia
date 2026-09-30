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
 // The XDK inlines variable-size memcmp here as a byte-at-a-time loop. SH3
 // opcodes are aligned halfwords; compare four complete opcodes per iteration.
 // OR/XOR only answers equality, exactly what the dispatcher needs.
 while(words>=4) {
  unsigned difference = (unsigned)(expected[0]^actual[0]) |
   (unsigned)(expected[1]^actual[1]) |
   (unsigned)(expected[2]^actual[2]) |
   (unsigned)(expected[3]^actual[3]);
  if(difference)return false;
  expected+=4; actual+=4; words-=4;
 }
 switch(words) {
  case 3: if(expected[2]!=actual[2])return false; // fall through
  case 2: if(expected[1]!=actual[1])return false; // fall through
  case 1: if(expected[0]!=actual[0])return false; // fall through
  default: return true;
 }
}
#undef SH3_SOURCE_INLINE
#endif
