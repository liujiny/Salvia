/* Stage3 hardware diagnostic build. Set to 0 for the normal release. */
#include "raiden2_diag_api.h"
/* 0=normal, 1=BG only, 2=MG only, 3=FG only, 4=TX only, 5=sprites only.
   This never changes the emulated CRTC register. */
#ifndef RAIDEN2_FORCE_VIDEO_DIAG
#define RAIDEN2_FORCE_VIDEO_DIAG 0
#endif
#if RAIDEN2_DEBUG
static void r2_debug_access(unsigned addr, unsigned value, int write);
static void r2_debug_reset(void);
static void r2_debug_frame(void);
static void r2_debug_video(struct mame_bitmap *, const struct rectangle *);
static void r2_debug_bank(void);
static void r2_debug_sound_access(unsigned addr, unsigned value, int write);
#else
#define r2_debug_access(a,v,w) ((void)0)
#define r2_debug_reset() ((void)0)
#define r2_debug_frame() ((void)0)
#define r2_debug_video(b,c) ((void)0)
#define r2_debug_bank() ((void)0)
#define r2_debug_sound_access(a,v,w) ((void)0)
#endif

static unsigned r2_video_enable(void)
{
#if RAIDEN2_DEBUG && RAIDEN2_FORCE_VIDEO_DIAG
 return 0x1f ^ (1U << (RAIDEN2_FORCE_VIDEO_DIAG - 1));
#else
 return r2_layer_enable;
#endif
}
