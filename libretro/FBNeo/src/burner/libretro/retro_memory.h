#ifndef __RETRO_MEMORY__
#define __RETRO_MEMORY__

#include <stddef.h>
#include "libretro.h"

void CheevosInit();
void CheevosExit();

extern bool bLibretroSupportsSavestateContext;

// Optional synchronous, paused-main-thread state transport. Mode 0 saves
// (data is read-only to the callback), mode 1 loads (callback fills data).
// Each callback transfers its entire <=64 KiB block or returns false.
// size is the exact retro_serialize_size() raw length, with no new headers.
// Validate a load's complete file before calling: failed reads cannot roll
// back already restored areas without allocating another complete state.
typedef bool (RETRO_CALLCONV *fbneo_state_stream_io_t)(void *context, void *data, size_t bytes);
extern "C" bool RETRO_CALLCONV fbneo_state_stream_v1(unsigned mode, size_t size,
	fbneo_state_stream_io_t io, void *context);
retro_proc_address_t RETRO_CALLCONV fbneo_get_proc_address(const char *symbol);

#endif
