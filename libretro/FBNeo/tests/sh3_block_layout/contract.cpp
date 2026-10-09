// Compile-only ABI contract; no 32-bit host runtime libraries are required.
#include <stddef.h>
typedef unsigned int UINT32;
typedef unsigned short UINT16;
struct Sh3PpcState;
namespace Sh3Ppc {
enum { MAX_INSNS=32 };
#include "../../src/cpu/sh4/sh3_drc_block.h"
}
typedef char Require32BitPointers[(sizeof(void*)==4)?1:-1];
// The record grew from 84 to 88 bytes when the per-page code generation was
// appended after the snapshot (sh3_drc_block.h), so the 32768-record table
// moved from 2752512 to 2883584 bytes. Every offset above is unchanged.
typedef char MetadataAllocationUnchanged[(32768*sizeof(Sh3Ppc::Block)==2883584)?1:-1];
extern "C" unsigned sh3_layout_contract(unsigned which) {
 const unsigned values[]={sizeof(Sh3Ppc::Block),offsetof(Sh3Ppc::Block,entry),
  offsetof(Sh3Ppc::Block,words),offsetof(Sh3Ppc::Block,cycles),
  offsetof(Sh3Ppc::Block,check_read_map),offsetof(Sh3Ppc::Block,original)};
 return values[which%6];
}
