#ifndef ROUND4P_PROFILE_H
#define ROUND4P_PROFILE_H

#include "osd_cpu.h"

#if defined(_XBOX360)
#include <xtl.h>
#ifndef X360_MAME_PROFILE
#define X360_MAME_PROFILE 0
#endif
#else
#define X360_MAME_PROFILE 0
#endif

#if X360_MAME_PROFILE
typedef struct round4p_profile_data
{
   UINT64 frequency;
   UINT64 runtime_ticks, retro_run_ticks, mame_frame_ticks;
   UINT64 mips_ticks, mips_calls, mips_cycles;
   UINT64 adsp_ticks, adsp_calls, adsp_cycles;
   UINT64 video_update_ticks, video_update_calls;
   UINT64 osd_ticks, osd_calls;
   UINT64 palette_ticks, palette_calls;
   UINT64 direct_ticks, direct_calls, direct_success;
   UINT64 gpu_prepare_ticks, gpu_prepare_calls;
   UINT64 indexed_copy_ticks;
   UINT64 video_callback_ticks, video_callback_calls;
   UINT64 video_worker_ticks, video_wait_ticks;
   UINT64 audio_worker_ticks, audio_wait_ticks, audio_callback_ticks;
   UINT64 fast_read_hit, fast_read_miss;
   UINT64 fast_write_hit, fast_write_miss;
   UINT64 generic_fallback;
   UINT64 frames, gpu_indexed_frames, palto565_frames, fallback_frames;
} round4p_profile_data;

extern round4p_profile_data round4p_profile;
#endif

#if X360_MAME_PROFILE
static __inline UINT64 round4p_ticks(void)
{
   LARGE_INTEGER value;
   QueryPerformanceCounter(&value);
   return (UINT64)value.QuadPart;
}
#define R4P_BEGIN(name) UINT64 name = round4p_ticks()
#define R4P_ADD(field, name) (round4p_profile.field += round4p_ticks() - (name))
#define R4P_INC(field)       (++round4p_profile.field)
#define R4P_ADD_VALUE(field, value) (round4p_profile.field += (UINT64)(value))
#else
#define R4P_BEGIN(name)               ((void)0)
#define R4P_ADD(field, name)          ((void)0)
#define R4P_INC(field)                ((void)0)
#define R4P_ADD_VALUE(field, value)   ((void)0)
#endif

void round4p_profile_reset(void);
void round4p_profile_dump(int final_dump);

#endif
