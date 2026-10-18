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
 if (words <= 8) {
  switch(words) {
   case 0: return true;
   case 1: return expected[0] == actual[0];
   case 2: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]));
   case 3: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]));
   case 4: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]) | (unsigned)(expected[3] ^ actual[3]));
   case 5: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]) | (unsigned)(expected[3] ^ actual[3]) | (unsigned)(expected[4] ^ actual[4]));
   case 6: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]) | (unsigned)(expected[3] ^ actual[3]) | (unsigned)(expected[4] ^ actual[4]) | (unsigned)(expected[5] ^ actual[5]));
   case 7: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]) | (unsigned)(expected[3] ^ actual[3]) | (unsigned)(expected[4] ^ actual[4]) | (unsigned)(expected[5] ^ actual[5]) | (unsigned)(expected[6] ^ actual[6]));
   case 8: return !((unsigned)(expected[0] ^ actual[0]) | (unsigned)(expected[1] ^ actual[1]) | (unsigned)(expected[2] ^ actual[2]) | (unsigned)(expected[3] ^ actual[3]) | (unsigned)(expected[4] ^ actual[4]) | (unsigned)(expected[5] ^ actual[5]) | (unsigned)(expected[6] ^ actual[6]) | (unsigned)(expected[7] ^ actual[7]));
  }
 }
 while(words>=8) {
  unsigned diff = (unsigned)(expected[0] ^ actual[0])
                | (unsigned)(expected[1] ^ actual[1])
                | (unsigned)(expected[2] ^ actual[2])
                | (unsigned)(expected[3] ^ actual[3])
                | (unsigned)(expected[4] ^ actual[4])
                | (unsigned)(expected[5] ^ actual[5])
                | (unsigned)(expected[6] ^ actual[6])
                | (unsigned)(expected[7] ^ actual[7]);
  if(diff) return false;
  expected+=8; actual+=8; words-=8;
 }
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
