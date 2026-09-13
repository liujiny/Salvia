#ifndef RAIDEN2_DIAG_API_H
#define RAIDEN2_DIAG_API_H
#ifndef RAIDEN2_DEBUG
/* Play build: enable explicitly only when collecting hardware diagnostics. */
#define RAIDEN2_DEBUG 0
#endif
#if RAIDEN2_DEBUG
void raiden2_debug_output(const void *data, unsigned width, unsigned height,
 unsigned pitch, unsigned stride, int indexed, const UINT32 *palette);
#else
#define raiden2_debug_output(d,w,h,p,s,i,c) ((void)0)
#endif
#endif
