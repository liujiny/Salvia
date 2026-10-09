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
typedef char MetadataAllocationUnchanged[(32768*sizeof(Sh3Ppc::Block)==2752512)?1:-1];
extern "C" unsigned sh3_layout_contract(unsigned which) {
 const unsigned values[]={sizeof(Sh3Ppc::Block),offsetof(Sh3Ppc::Block,entry),
  offsetof(Sh3Ppc::Block,words),offsetof(Sh3Ppc::Block,cycles),
  offsetof(Sh3Ppc::Block,check_read_map),offsetof(Sh3Ppc::Block,original)};
 return values[which%6];
}
