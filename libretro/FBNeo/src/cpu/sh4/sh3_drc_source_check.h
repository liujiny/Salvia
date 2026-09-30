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
 // Short snapshots are common in the sampled CV1000 workload. Select the
 // exact length once, then compare only its valid halfwords without a loop.
 // Reads are ordinary bounded RAM, not MMIO. Mismatch order is immaterial;
 // equal snapshots still validate every opcode. Zero words touches neither pointer.
 if(words<=8) {
  switch(words) {
   case 8: if(expected[7]!=actual[7])return false; // fall through
   case 7: if(expected[6]!=actual[6])return false; // fall through
   case 6: if(expected[5]!=actual[5])return false; // fall through
   case 5: if(expected[4]!=actual[4])return false; // fall through
   case 4: if(expected[3]!=actual[3])return false; // fall through
   case 3: if(expected[2]!=actual[2])return false; // fall through
   case 2: if(expected[1]!=actual[1])return false; // fall through
   case 1: if(expected[0]!=actual[0])return false; // fall through
   default: return true;
  }
 }
 // Compare complete opcodes with direct equality branches. Keeping a group
 // of four avoids a per-opcode loop while avoiding a live XOR/OR reduction.
 // Every opcode in an equal snapshot is checked; mismatch exits request the
 // same recompilation as before. No wider load or new alignment is assumed.
 while(words>=4) {
  if(expected[0]!=actual[0])return false;
  if(expected[1]!=actual[1])return false;
  if(expected[2]!=actual[2])return false;
  if(expected[3]!=actual[3])return false;
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
