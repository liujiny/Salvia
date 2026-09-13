#include "libretro.h"
#include "libretro_perf.h"
#include "retro_inline.h"
#include "mame2003.h"
#include "palette.h"
#include "fileio.h"
#include "common.h"
#include "mame.h"
#include "usrintrf.h"
#include "driver.h"
#include "../round4p_profile.h"
#include "../drivers/raiden2_diag_api.h"
#if defined(_XBOX360)
#include "video_rotate565.h"
#endif

/* Compatibility for older libretro.h snapshots used by some mame2003+ trees.
 * The command value and structure match the upstream libretro API. */
#ifndef RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER
#ifndef RETRO_ENVIRONMENT_EXPERIMENTAL
#define RETRO_ENVIRONMENT_EXPERIMENTAL 0x10000
#endif
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER (40 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#ifndef RETRO_MEMORY_ACCESS_WRITE
#define RETRO_MEMORY_ACCESS_WRITE (1 << 0)
#endif
struct retro_framebuffer
{
   void *data;
   unsigned width;
   unsigned height;
   size_t pitch;
   enum retro_pixel_format format;
   unsigned access_flags;
   unsigned memory_flags;
};
#endif

#if defined(_XBOX360)
#include <xtl.h>
#ifndef X360_AV_WORKERS
#define X360_AV_WORKERS 1
#endif
#ifndef X360_VIDEO_WORKER
#define X360_VIDEO_WORKER X360_AV_WORKERS
#endif
#ifndef X360_DIRECT_FB
#define X360_DIRECT_FB 1
#endif
#ifndef X360_GPU_INDEXED_PALETTE
#define X360_GPU_INDEXED_PALETTE 1
#endif

/* Private Salvia/Xbox extension paired with Round4G frontend.  A 16-bit MAME
 * indexed framebuffer is copied unchanged into the RGB565 game texture and a
 * Xenos pixel shader reconstructs the 16-bit index and fetches RGB from a
 * 256x256 palette texture.  Unsupported frontends simply return false and the
 * normal CPU PALTO565 path is used. */
#define SALVIA_ENVIRONMENT_X360_GET_INDEXED_FRAMEBUFFER 0x53580001u
#define SALVIA_ENVIRONMENT_X360_SET_INDEXED_PALETTE     0x53580002u
struct salvia_x360_indexed_framebuffer
{
   void *data;
   unsigned width;
   unsigned height;
   size_t pitch;
};
struct salvia_x360_indexed_palette
{
   const uint32_t *colors;
   const uint32_t *dirty;
   unsigned entries;
   int full_update;
};
#endif

#define MAX_LED 8

extern retro_log_printf_t log_cb;
extern retro_environment_t environ_cb;
extern retro_video_refresh_t video_cb;
extern retro_set_led_state_t led_state_cb;

/* Part of libretro's API */
extern int gotFrame;

extern struct RunningMachine *Machine;

/* Output bitmap settings */
struct osd_create_params video_config;
unsigned vis_width, vis_height;
unsigned tate_mode;

/* Output bitmap native conversion/transformation related vars */
unsigned video_conversion_type;
unsigned video_do_bypass;
unsigned video_stride_in, video_stride_out;
bool video_flip_x, video_flip_y, video_swap_xy;
bool video_hw_transpose;
const rgb_t *video_palette;
uint16_t *video_buffer;

#if defined(_XBOX360) && X360_GPU_INDEXED_PALETTE
static int x360_gpu_palette_ready = 0;
static int x360_gpu_palette_logged = 0;
#endif

#if defined(_XBOX360) && X360_VIDEO_WORKER
/*
 * Xbox 360 low-latency video conversion worker.
 *
 * VIDEO_UPDATE remains on the MAME/libretro thread because drivers can touch
 * live CPU/timer/video state there.  Once the current frame is complete, the
 * immutable framebuffer is handed to hardware thread 2 for pixel conversion.
 *
 * Unlike Round2/Round2b, this worker NEVER carries a converted frame across
 * retro_run() boundaries.  The main thread waits for the CURRENT frame's
 * conversion, then calls video_cb() for that same frame.  Therefore this path
 * does not intentionally add the old N-1 display / N emulation pipeline frame.
 *
 * Because the producer waits before emulation can advance, the worker can read
 * the completed MAME bitmap and palette directly.  This also removes Round2's
 * full-frame snapshot memcpy and private palette copy from the hot path.
 */
#define X360_VIDEO_HW_THREAD 2

typedef struct
{
   const UINT8 *input;
   int rowpixels;
   struct rectangle visible_area;
   unsigned conversion_type;
   unsigned out_width;
   unsigned out_height;
   unsigned out_pitch;
   void *output;
   int flip_x;
   int flip_y;
   int swap_xy;
   const rgb_t *palette;
} x360_video_job_t;

static HANDLE x360_video_thread;
static HANDLE x360_video_wake_event;
static HANDLE x360_video_done_event;
static volatile LONG x360_video_stop;
static int x360_video_pending;
static x360_video_job_t x360_video_job;
/* Owned by the emulation thread; copied only after the worker completion event. */
static void *x360_staged_texture;
static unsigned x360_staged_pitch;

static int x360_video_worker_start(void);
static void x360_video_worker_stop(void);
#endif

/* Possible pixel conversions (see corresponding function far below) */
enum
{
   VCT_PASS8888,
   VCT_PASS1555,
   VCT_PASSPAL,
   VCT_PALTO565
};

/* Retrieve output geometry (i.e. window dimensions) */
void mame2003_video_get_geometry(struct retro_game_geometry *geom)
{
   /* Shorter variable names, for readability */
   unsigned max_w = video_config.width;
   unsigned max_h = video_config.height;
   unsigned vis_w = vis_width > 0 ? vis_width : max_w;
   unsigned vis_h = vis_height > 0 ? vis_height : max_h;

   /* Maximum dimensions must accomodate all image orientations */
   unsigned max_dim = max_w > max_h ? max_w : max_h;
   geom->max_width = geom->max_height = max_dim;

   /* Hardware rotations don't resize the framebuffer, adjust for that */
   geom->base_width = video_hw_transpose ? vis_h : vis_w;
   geom->base_height = video_hw_transpose ? vis_w : vis_h;

   geom->aspect_ratio = video_hw_transpose ? (float)video_config.aspect_y / (float)video_config.aspect_x : (float)video_config.aspect_x / (float)video_config.aspect_y;

}

void mame2003_video_update_visible_area(struct mame_display *display)
{
   struct retro_game_geometry geom = { 0 };

   struct rectangle visible_area = display->game_visible_area;
   vis_width = visible_area.max_x - visible_area.min_x + 1;
   vis_height = visible_area.max_y - visible_area.min_y + 1;

   /* Adjust for native XY swap */
   if (video_swap_xy)
   {
      unsigned temp;
      temp = vis_width; vis_width = vis_height; vis_height = temp;
   }

   /* Update MAME's own UI visible area */
   set_ui_visarea(
      visible_area.min_x, visible_area.min_y,
      visible_area.max_x, visible_area.max_y);

   /* Notify libretro of the change */
   mame2003_video_get_geometry(&geom);
   environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
}

/* Compute a reverse of a given orientation, accounting for XY swaps */
static unsigned reverse_orientation(unsigned orientation)
{
   int result = orientation;
   if (orientation & ORIENTATION_SWAP_XY)
   {
      result = ORIENTATION_SWAP_XY;
      if (orientation & ORIENTATION_FLIP_X)
         result ^= ORIENTATION_FLIP_Y;
      if (orientation & ORIENTATION_FLIP_Y)
         result ^= ORIENTATION_FLIP_X;
   }
   return result;
}

/* Init video orientation and geometry */
void mame2003_video_init_orientation(void)
{
   unsigned orientation = Machine->gamedrv->flags & ORIENTATION_MASK;
   unsigned rotate_mode;

   rotate_mode = 0; /* Known invalid value */
   tate_mode = options.tate_mode;   /* Acknowledge that the TATE mode is handled */
   video_hw_transpose = false;

   /* test RA if rotation is working if not dont alter the orientation let mame handle it */
   options.ui_orientation = reverse_orientation(orientation);

   if (tate_mode && (orientation & ORIENTATION_SWAP_XY))
      orientation = reverse_orientation(orientation) ^ ROT270;


   if (orientation == ROT0 || orientation == ROT90 || orientation == ROT180 || orientation == ROT270)
   {
      if (environ_cb(RETRO_ENVIRONMENT_SET_ROTATION, &rotate_mode) )
      {
        log_cb(RETRO_LOG_INFO, LOGPRE "RetroArch will perform the rotation.\n");

        rotate_mode = (orientation == ROT270) ? 1
                    : (orientation == ROT180) ? 2
                    : (orientation == ROT90 ) ? 3
                    : rotate_mode;

        if (orientation & ORIENTATION_SWAP_XY) video_hw_transpose = true; /*do this before the rotation reverse*/
        orientation = reverse_orientation(orientation ^ orientation); /* undo mame rotation if retroarch can do it */
        environ_cb(RETRO_ENVIRONMENT_SET_ROTATION, &rotate_mode);
      }

      else
        log_cb(RETRO_LOG_INFO, LOGPRE "This port of RetroArch does not support rotation or it has been disabled. Mame will rotate internally.\n");

   }
   else
     log_cb(RETRO_LOG_INFO, LOGPRE "RetroArch does not support this type of rotation, using mame internal rotation instead.\n");

   tate_mode = options.tate_mode;

   /* Set up native orientation flags that aren't handled by libretro */
   if (orientation & ORIENTATION_SWAP_XY) video_hw_transpose = true; /*dont set this to false if RA changes the flags else vertical games swap the xy*/
   video_flip_x = orientation & ORIENTATION_FLIP_X;
   video_flip_y = orientation & ORIENTATION_FLIP_Y;
   video_swap_xy = orientation & ORIENTATION_SWAP_XY;
   log_cb(RETRO_LOG_DEBUG,"mame internal: video_flip_x:%u video_flip_y:%u video_swap_xy:%u video_hw_transpose:%u\n",video_flip_x,video_flip_y,video_swap_xy,video_hw_transpose);
   Machine->ui_orientation = options.ui_orientation;


}

/* Init video format conversion settings */
void mame2003_video_init_conversion(UINT32 *rgb_components)
{
   unsigned color_mode;

   /* Case I: 16-bit indexed palette */
   if (video_config.depth == 16)
   {
      /* If a 6+ bits per color channel palette is used, do 32-bit XRGB8888 */
      if (video_config.video_attributes & VIDEO_NEEDS_6BITS_PER_GUN)
      {
         video_stride_in = 2; video_stride_out = 4;
         video_conversion_type = VCT_PASSPAL;
         color_mode = RETRO_PIXEL_FORMAT_XRGB8888;
      }
      /* Otherwise 16-bit RGB565 will suffice */
      else
      {
         video_stride_in = 2; video_stride_out = 2;
         video_conversion_type = VCT_PALTO565;
         color_mode = RETRO_PIXEL_FORMAT_RGB565;
      }
   }

   /* Case II: 32-bit XRGB8888, pass it through */
   else if (video_config.depth == 32)
   {
      video_stride_in = 4; video_stride_out = 4;
      video_conversion_type = VCT_PASS8888;
      color_mode = RETRO_PIXEL_FORMAT_XRGB8888;

      /* TODO: figure those out */
      rgb_components[0] = 0x00FF0000;
      rgb_components[1] = 0x0000FF00;
      rgb_components[2] = 0x000000FF;
   }

   /* Case III: 16-bit 0RGB1555, pass it through */
   else if (video_config.depth == 15)
   {
      video_stride_in = 2; video_stride_out = 2;
      video_conversion_type = VCT_PASS1555;
      color_mode = RETRO_PIXEL_FORMAT_0RGB1555;

      /* TODO: figure those out */
      rgb_components[0] = 0x00007C00;
      rgb_components[1] = 0x000003E0;
      rgb_components[2] = 0x0000001F;
   }

   /* Otherwise bail out on unknown video mode */
   else
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "Unsupported color depth: %u\n", video_config.depth);
      abort();
   }

   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &color_mode);
}

/* Do a soft reinit to process new video output parameters */
void mame2003_video_reinit(void)
{
   UINT32 rgb_components[3];
   struct osd_create_params old_params = video_config;
   osd_close_display();
   osd_create_display(&old_params, &rgb_components[0]);
}

int osd_create_display(
   const struct osd_create_params *params, UINT32 *rgb_components)
{
   memcpy(&video_config, params, sizeof(video_config));

   mame2003_video_init_orientation();
   mame2003_video_init_conversion(rgb_components);
#if defined(_XBOX360) && X360_GPU_INDEXED_PALETTE
   x360_gpu_palette_ready = 0;
#endif

   /* Check if a framebuffer conversion can be bypassed */
   video_do_bypass =
      !video_flip_x && !video_flip_y && !video_swap_xy &&
      ((video_config.depth == 15) || (video_config.depth == 32));
   /* Allocate an output video buffer, if necessary */
   if (!video_do_bypass)
   {
      video_buffer = malloc(video_config.width * video_config.height * video_stride_out);
      if (!video_buffer)
         return 1;
#if defined(_XBOX360) && X360_VIDEO_WORKER
      x360_video_worker_start();
#endif
   }

   return 0;
}

void osd_close_display(void)
{
#if defined(_XBOX360) && X360_VIDEO_WORKER
   x360_video_worker_stop();
#endif
   free(video_buffer);
   video_buffer = NULL;
}

static INLINE void pix_convert_pass8888(uint32_t *from, uint32_t *to, const rgb_t *palette)
{
   (void)palette;
   *to = *from;
}

static INLINE void pix_convert_pass1555(uint16_t *from, uint16_t *to, const rgb_t *palette)
{
   (void)palette;
   *to = *from;
}

static INLINE void pix_convert_passpal(uint16_t *from, uint32_t *to, const rgb_t *palette)
{
   *to = palette[*from];
}

static INLINE void pix_convert_palto565(uint16_t *from, uint16_t *to, const rgb_t *palette)
{
   const uint32_t color = palette[*from];
   *to = (color & 0x00F80000) >> 8 | /* red */
         (color & 0x0000FC00) >> 5 | /* green */
         (color & 0x000000F8) >> 3;  /* blue */
}

/* Shared converter used by the synchronous path and the Xbox 360 worker.
 * Keeping one implementation avoids subtle differences in rotation/palette
 * behavior between threaded and non-threaded rendering. */
static void frame_convert_raw(
   char *input,
   char *output,
   signed pitch,
   unsigned output_pitch_bytes,
   const struct rectangle *visible_area,
   unsigned conversion_type,
   const rgb_t *palette,
   bool flip_x,
   bool flip_y,
   bool swap_xy)
{
#if X360_MAME_PROFILE
   UINT64 r4p_convert_start = round4p_ticks();
#endif
   int x, y;
   int x0 = visible_area->min_x, y0 = visible_area->min_y;
   int x1 = visible_area->max_x, y1 = visible_area->max_y;
   int w = x1 - x0 + 1, h = y1 - y0 + 1;

   /* Pixel conversion loop macro for best possible inlining, w/o XY swap.
    * output_pitch_bytes is allowed to be wider than the visible image; this is
    * essential for writing directly into an SDL/Xbox texture surface whose
    * pitch is usually aligned by the driver. */
   #define CONVERT_NOSWAP(CONVERT_FUNC, TYPE_IN, TYPE_OUT, FLIP_X, FLIP_Y)\
      {\
         signed skip;\
         const signed out_stride = (signed)(output_pitch_bytes / sizeof(TYPE_OUT));\
         TYPE_IN *in = (TYPE_IN*)input;\
         TYPE_OUT *out = (TYPE_OUT*)output;\
         \
         in += (!FLIP_X ? x0 : x1) + (!FLIP_Y ? y0 * pitch : y1 * pitch);\
         skip = (!FLIP_X ? -w : w) + (!FLIP_Y ? pitch : -pitch);\
         \
         for (y = 0; y < h; y++)\
         {\
            if (!FLIP_X)\
               for (x = 0; x < w; x++)\
                  CONVERT_FUNC(in++, out++, palette);\
            else\
               for (x = 0; x < w; x++)\
                  CONVERT_FUNC(in--, out++, palette);\
            in += skip;\
            out += out_stride - w;\
         }\
      }

   /* XY-swap path. Each source X becomes one output scanline whose visible
    * width is the original source height. */
   #define CONVERT_SWAP(CONVERT_FUNC, TYPE_IN, TYPE_OUT, FLIP_X, FLIP_Y)\
      {\
         const signed out_stride = (signed)(output_pitch_bytes / sizeof(TYPE_OUT));\
         TYPE_IN *in = (TYPE_IN*)input;\
         TYPE_OUT *out = (TYPE_OUT*)output;\
         \
         for (x = FLIP_Y ? x1 : x0; FLIP_Y ? x >= x0 : x <= x1; x += FLIP_Y ? -1 : 1)\
         {\
            for (y = FLIP_X ? y1 : y0; FLIP_X ? y >= y0 : y <= y1; y += FLIP_X ? -1 : 1)\
               CONVERT_FUNC(&in[y * pitch + x], out++, palette);\
            out += out_stride - h;\
         }\
      }

   #define CONVERT_CHOOSE(CONVERT_MACRO, CONVERT_FUNC, TYPE_IN, TYPE_OUT)\
      {\
         if (!flip_x && !flip_y)\
            CONVERT_MACRO(CONVERT_FUNC, TYPE_IN, TYPE_OUT, false, false)\
         else if (!flip_x && flip_y)\
            CONVERT_MACRO(CONVERT_FUNC, TYPE_IN, TYPE_OUT, false, true)\
         else if (flip_x && !flip_y)\
            CONVERT_MACRO(CONVERT_FUNC, TYPE_IN, TYPE_OUT, true, false)\
         else if (flip_x && flip_y)\
            CONVERT_MACRO(CONVERT_FUNC, TYPE_IN, TYPE_OUT, true, true);\
      }

   #define CONVERT(CONVERT_FUNC, TYPE_IN, TYPE_OUT)\
      if (!swap_xy)\
         CONVERT_CHOOSE(CONVERT_NOSWAP, CONVERT_FUNC, TYPE_IN, TYPE_OUT)\
      else\
         CONVERT_CHOOSE(CONVERT_SWAP, CONVERT_FUNC, TYPE_IN, TYPE_OUT)

   switch (conversion_type)
   {
      case VCT_PASS8888:
         CONVERT(pix_convert_pass8888, uint32_t, uint32_t);
         break;
      case VCT_PASS1555:
         CONVERT(pix_convert_pass1555, uint16_t, uint16_t);
         break;
      case VCT_PASSPAL:
         CONVERT(pix_convert_passpal, uint16_t, uint32_t);
         break;
      case VCT_PALTO565:
#if defined(_XBOX360)
         if (swap_xy)
         {
            x360_rotate565((const UINT16 *)input, (UINT16 *)output,
               pitch, output_pitch_bytes / sizeof(UINT16), x0, y0, w, h,
               palette, flip_x, flip_y);
            break;
         }
#endif
         CONVERT(pix_convert_palto565, uint16_t, uint16_t);
         break;
   }

#if X360_MAME_PROFILE
   if (conversion_type == VCT_PALTO565 || conversion_type == VCT_PASSPAL)
   {
      round4p_profile.palette_ticks += round4p_ticks() - r4p_convert_start;
      round4p_profile.palette_calls++;
      if (conversion_type == VCT_PALTO565)
         round4p_profile.palto565_frames++;
   }
#endif

   #undef CONVERT
   #undef CONVERT_CHOOSE
   #undef CONVERT_SWAP
   #undef CONVERT_NOSWAP
}

static void frame_convert(struct mame_display *display)
{
   frame_convert_raw(
      (char*)display->game_bitmap->base,
      (char*)video_buffer,
      display->game_bitmap->rowpixels,
      vis_width * video_stride_out,
      &display->game_visible_area,
      video_conversion_type,
      video_palette,
      video_flip_x,
      video_flip_y,
      video_swap_xy);
}

#if defined(_XBOX360) && X360_GPU_INDEXED_PALETTE
/* Fastest low-latency path for ordinary 16-bit indexed games.
 *
 * No extra frame is queued.  The CURRENT MAME bitmap is copied row-for-row
 * into the permanently CPU-mapped Xenos game texture.  This copy is required
 * because MAME owns its bitmap, but it avoids the much heavier per-pixel
 * palette[*src] + RGB888->RGB565 conversion.  Palette conversion is performed
 * by Xenos when the current frame is drawn.
 *
 * We deliberately restrict this first GPU-palette implementation to frames
 * that need no CPU flip/transpose.  Vertical/rotated titles and any frontend
 * effect other than the driver's effect 0 automatically fall back to the
 * proven Round3G CPU path. */
static int x360_gpu_indexed_present(struct mame_display *display)
{
#if X360_MAME_PROFILE
   UINT64 r4p_prepare_start = round4p_ticks();
   UINT64 r4p_copy_start;
#endif
   struct salvia_x360_indexed_framebuffer fb;
   struct salvia_x360_indexed_palette pal;
   struct mame_bitmap *bitmap;
   const struct rectangle *va;
   const UINT16 *src;
   UINT8 *dst;
   unsigned y;
   unsigned row_bytes;

   if (!display || !video_cb || !environ_cb ||
       video_conversion_type != VCT_PALTO565 ||
       video_flip_x || video_flip_y || video_swap_xy)
   {
      R4P_INC(fallback_frames);
      return 0;
   }

   bitmap = display->game_bitmap;
   if (!bitmap || !bitmap->base || !video_palette ||
       display->game_palette_entries == 0 ||
       display->game_palette_entries > 65536u)
   {
      R4P_INC(fallback_frames);
      return 0;
   }

   /* Upload only dirty palette entries after the first full upload. */
   if (!x360_gpu_palette_ready || (display->changed_flags & GAME_PALETTE_CHANGED))
   {
      memset(&pal, 0, sizeof(pal));
      pal.colors = (const uint32_t *)video_palette;
      pal.dirty = x360_gpu_palette_ready ? (const uint32_t *)display->game_palette_dirty : NULL;
      pal.entries = display->game_palette_entries;
      pal.full_update = x360_gpu_palette_ready ? 0 : 1;

      if (!environ_cb(SALVIA_ENVIRONMENT_X360_SET_INDEXED_PALETTE, &pal))
      {
         x360_gpu_palette_ready = 0;
#if X360_MAME_PROFILE
         round4p_profile.fallback_frames++;
         round4p_profile.gpu_prepare_ticks += round4p_ticks() - r4p_prepare_start;
         round4p_profile.gpu_prepare_calls++;
#endif
         return 0;
      }
      x360_gpu_palette_ready = 1;
   }

   memset(&fb, 0, sizeof(fb));
   fb.width = vis_width;
   fb.height = vis_height;
#if X360_MAME_PROFILE
   {
      UINT64 direct_start = round4p_ticks();
      round4p_profile.direct_calls++;
#endif
   if (!environ_cb(SALVIA_ENVIRONMENT_X360_GET_INDEXED_FRAMEBUFFER, &fb) ||
       !fb.data || fb.pitch < (size_t)vis_width * sizeof(UINT16))
   {
#if X360_MAME_PROFILE
      round4p_profile.direct_ticks += round4p_ticks() - direct_start;
      round4p_profile.fallback_frames++;
      round4p_profile.gpu_prepare_ticks += round4p_ticks() - r4p_prepare_start;
      round4p_profile.gpu_prepare_calls++;
#endif
      return 0;
   }
#if X360_MAME_PROFILE
      round4p_profile.direct_ticks += round4p_ticks() - direct_start;
      round4p_profile.direct_success++;
   }
#endif

   va = &display->game_visible_area;
   src = (const UINT16 *)bitmap->base +
         va->min_y * bitmap->rowpixels + va->min_x;
   dst = (UINT8 *)fb.data;
   row_bytes = vis_width * sizeof(UINT16);
#if X360_MAME_PROFILE
   r4p_copy_start = round4p_ticks();
#endif

   /* The common horizontal case is a straight cache-friendly row copy. */
   if ((unsigned)bitmap->rowpixels == vis_width && fb.pitch == row_bytes)
   {
      memcpy(dst, src, (size_t)row_bytes * vis_height);
   }
   else
   {
      for (y = 0; y < vis_height; y++)
         memcpy(dst + (size_t)y * fb.pitch,
                src + (size_t)y * bitmap->rowpixels,
                row_bytes);
   }
#if X360_MAME_PROFILE
   round4p_profile.indexed_copy_ticks += round4p_ticks() - r4p_copy_start;
#endif

   if (!x360_gpu_palette_logged && log_cb)
   {
      log_cb(RETRO_LOG_INFO, LOGPRE
         "X360 GPU indexed-palette path active: raw 16-bit indices -> Xenos palette shader, same-frame/no queue.\n");
      x360_gpu_palette_logged = 1;
   }

   raiden2_debug_output(fb.data,vis_width,vis_height,(unsigned)fb.pitch,2,1,video_palette);
#if X360_MAME_PROFILE
   round4p_profile.gpu_indexed_frames++;
   round4p_profile.gpu_prepare_ticks += round4p_ticks() - r4p_prepare_start;
   round4p_profile.gpu_prepare_calls++;
   {
      UINT64 callback_start = round4p_ticks();
      video_cb(fb.data, vis_width, vis_height, fb.pitch);
      round4p_profile.video_callback_ticks += round4p_ticks() - callback_start;
      round4p_profile.video_callback_calls++;
   }
#else
   video_cb(fb.data, vis_width, vis_height, fb.pitch);
#endif
   return 1;
}
#endif

#if defined(_XBOX360) && X360_VIDEO_WORKER
static DWORD WINAPI x360_video_worker_proc(LPVOID userdata)
{
   (void)userdata;

   for (;;)
   {
      WaitForSingleObject(x360_video_wake_event, INFINITE);
      if (x360_video_stop)
         break;

#if X360_MAME_PROFILE
      {
         UINT64 worker_start = round4p_ticks();
#endif
      frame_convert_raw(
         (char*)x360_video_job.input,
         (char*)x360_video_job.output,
         x360_video_job.rowpixels,
         x360_video_job.out_pitch,
         &x360_video_job.visible_area,
         x360_video_job.conversion_type,
         x360_video_job.palette,
         x360_video_job.flip_x ? true : false,
         x360_video_job.flip_y ? true : false,
         x360_video_job.swap_xy ? true : false);
#if X360_MAME_PROFILE
         round4p_profile.video_worker_ticks += round4p_ticks() - worker_start;
      }
#endif

      SetEvent(x360_video_done_event);
   }

   return 0;
}

static void x360_video_wait(void)
{
   if (x360_video_pending && x360_video_done_event)
   {
#if X360_MAME_PROFILE
      UINT64 wait_start = round4p_ticks();
#endif
      WaitForSingleObject(x360_video_done_event, INFINITE);
#if X360_MAME_PROFILE
      round4p_profile.video_wait_ticks += round4p_ticks() - wait_start;
#endif
      x360_video_pending = 0;
   }
}

static void x360_video_worker_stop(void)
{
   if (x360_video_thread)
   {
      x360_video_wait();
      x360_video_stop = 1;
      SetEvent(x360_video_wake_event);
      WaitForSingleObject(x360_video_thread, INFINITE);
      CloseHandle(x360_video_thread);
   }

   if (x360_video_wake_event)
      CloseHandle(x360_video_wake_event);
   if (x360_video_done_event)
      CloseHandle(x360_video_done_event);

   x360_video_thread = NULL;
   x360_video_wake_event = NULL;
   x360_video_done_event = NULL;
   x360_video_stop = 0;
   x360_video_pending = 0;
}

static int x360_video_worker_start(void)
{
   DWORD thread_id = 0;

   x360_video_worker_stop();
   x360_video_wake_event = CreateEvent(NULL, FALSE, FALSE, NULL);
   x360_video_done_event = CreateEvent(NULL, FALSE, FALSE, NULL);
   if (!x360_video_wake_event || !x360_video_done_event)
      goto fail;

   x360_video_stop = 0;
   x360_video_thread = CreateThread(NULL, 0, x360_video_worker_proc, NULL, 0, &thread_id);
   if (!x360_video_thread)
      goto fail;

   XSetThreadProcessor(x360_video_thread, X360_VIDEO_HW_THREAD);
   if (log_cb)
      log_cb(RETRO_LOG_INFO, LOGPRE "X360 low-latency video worker enabled on hardware thread %d (same-frame, no frame queue).\n", X360_VIDEO_HW_THREAD);
   return 1;

fail:
   if (log_cb)
      log_cb(RETRO_LOG_WARN, LOGPRE "X360 low-latency video worker unavailable; using synchronous conversion.\n");
   x360_video_worker_stop();
   return 0;
}

#if X360_DIRECT_FB
static enum retro_pixel_format x360_expected_output_format(void)
{
   switch (video_conversion_type)
   {
      case VCT_PASS8888:
      case VCT_PASSPAL:
         return RETRO_PIXEL_FORMAT_XRGB8888;
      case VCT_PASS1555:
         return RETRO_PIXEL_FORMAT_0RGB1555;
      case VCT_PALTO565:
      default:
         return RETRO_PIXEL_FORMAT_RGB565;
   }
}

/* Ask Salvia for the CURRENT Xbox game-texture surface.  When accepted, the
 * conversion worker writes straight into the surface that SDL_Flip/Xenos will
 * consume.  This is the standard libretro zero-copy software-framebuffer API,
 * not a private frontend callback. */
static int x360_get_direct_framebuffer(struct retro_framebuffer *fb)
{
   enum retro_pixel_format expected = x360_expected_output_format();
   static int logged_once = 0;
#if X360_MAME_PROFILE
   UINT64 direct_start = round4p_ticks();
   round4p_profile.direct_calls++;
#endif

   if (!fb || !environ_cb)
      return 0;

   memset(fb, 0, sizeof(*fb));
   fb->width = vis_width;
   fb->height = vis_height;
   fb->access_flags = RETRO_MEMORY_ACCESS_WRITE;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER, fb))
   {
#if X360_MAME_PROFILE
      round4p_profile.direct_ticks += round4p_ticks() - direct_start;
#endif
      return 0;
   }

   if (!fb->data || fb->width != vis_width || fb->height != vis_height ||
       fb->format != expected || fb->pitch < (size_t)vis_width * video_stride_out ||
       (fb->pitch % video_stride_out) != 0)
   {
#if X360_MAME_PROFILE
      round4p_profile.direct_ticks += round4p_ticks() - direct_start;
#endif
      return 0;
   }

   if (!logged_once && log_cb)
   {
      log_cb(RETRO_LOG_INFO, LOGPRE
         "X360 direct framebuffer active: current-frame conversion writes straight to Salvia/Xenos texture surface.\n");
      logged_once = 1;
   }
#if X360_MAME_PROFILE
   round4p_profile.direct_ticks += round4p_ticks() - direct_start;
   round4p_profile.direct_success++;
#endif
   return 1;
}
#endif

/* Queue the CURRENT completed bitmap.  There is deliberately no snapshot
 * allocation/copy: retro_run cannot advance to the next emulated frame until
 * x360_video_wait() has completed, so bitmap/palette lifetime is bounded to
 * this same frame. */
static int x360_video_submit_current(struct mame_display *display)
{
   struct mame_bitmap *bitmap = display->game_bitmap;

   if (!x360_video_thread || !bitmap || !bitmap->base || x360_video_pending)
      return 0;

   x360_video_job.input = (const UINT8 *)bitmap->base;
   x360_video_job.rowpixels = bitmap->rowpixels;
   x360_video_job.visible_area = display->game_visible_area;
   x360_video_job.conversion_type = video_conversion_type;
   x360_video_job.out_width = vis_width;
   x360_video_job.out_height = vis_height;
   x360_video_job.out_pitch = vis_width * video_stride_out;
   x360_video_job.output = video_buffer;
   x360_staged_texture = NULL;
   x360_staged_pitch = 0;
#if X360_DIRECT_FB
   {
      struct retro_framebuffer fb;
      if (x360_get_direct_framebuffer(&fb))
      {
         if (video_conversion_type == VCT_PALTO565 &&
             video_buffer && Machine && Machine->gamedrv &&
             !strcmp(Machine->gamedrv->name, "raiden2"))
         {
            /* Convert in cached RAM, then stream full rows to the texture.
               Frontend hardware rotation does not require video_swap_xy. */
            x360_staged_texture = fb.data;
            x360_staged_pitch = (unsigned)fb.pitch;
         }
         else
         {
            x360_video_job.output = fb.data;
            x360_video_job.out_pitch = (unsigned)fb.pitch;
         }
      }
   }
#endif
   x360_video_job.flip_x = video_flip_x ? 1 : 0;
   x360_video_job.flip_y = video_flip_y ? 1 : 0;
   x360_video_job.swap_xy = video_swap_xy ? 1 : 0;
   x360_video_job.palette = video_palette;

   x360_video_pending = 1;
   SetEvent(x360_video_wake_event);
   return 1;
}

/* Wait for and present the CURRENT frame.  This is intentionally same-frame
 * synchronization: no N-1 frame is retained for the next retro_run(). */
static int x360_video_present_current(struct mame_display *display)
{
   if (!x360_video_submit_current(display))
      return 0;

   x360_video_wait();
   if (x360_staged_texture)
   {
      R4P_BEGIN(copy_start);
      x360_copy565_rows(x360_staged_texture, x360_staged_pitch,
         x360_video_job.output, x360_video_job.out_pitch,
         x360_video_job.out_width * 2, x360_video_job.out_height);
      R4P_ADD(staged_copy_ticks, copy_start);
      R4P_INC(staged_frames);
      x360_video_job.output = x360_staged_texture;
      x360_video_job.out_pitch = x360_staged_pitch;
   }
   raiden2_debug_output(x360_video_job.output,x360_video_job.out_width,
      x360_video_job.out_height,x360_video_job.out_pitch,video_stride_out,0,NULL);
#if X360_MAME_PROFILE
   if (video_cb)
   {
      UINT64 callback_start = round4p_ticks();
      video_cb(x360_video_job.output,
               x360_video_job.out_width,
               x360_video_job.out_height,
               x360_video_job.out_pitch);
      round4p_profile.video_callback_ticks += round4p_ticks() - callback_start;
      round4p_profile.video_callback_calls++;
   }
#else
   if (video_cb)
      video_cb(x360_video_job.output, x360_video_job.out_width,
               x360_video_job.out_height, x360_video_job.out_pitch);
#endif
   return 1;
}
#endif

extern bool retro_audio_buff_underrun;
extern bool retro_audio_buff_active;
extern unsigned retro_audio_buff_occupancy;

const int frameskip_table[12][12] =
   { { 0,0,0,0,0,0,0,0,0,0,0,0 },
     { 0,0,0,0,0,0,0,0,0,0,0,1 },
     { 0,0,0,0,0,1,0,0,0,0,0,1 },
     { 0,0,0,1,0,0,0,1,0,0,0,1 },
     { 0,0,1,0,0,1,0,0,1,0,0,1 },
     { 0,1,0,0,1,0,1,0,0,1,0,1 },
     { 0,1,0,1,0,1,0,1,0,1,0,1 },
     { 0,1,0,1,1,0,1,0,1,1,0,1 },
     { 0,1,1,0,1,1,0,1,1,0,1,1 },
     { 0,1,1,1,0,1,1,1,0,1,1,1 },
     { 0,1,1,1,1,1,0,1,1,1,1,1 },
     { 0,1,1,1,1,1,1,1,1,1,1,1 } };

UINT8 frameskip_counter = 0;

int osd_skip_this_frame(void)
{
	bool skip_frame = 0;

	if (pause_action)  return 0;  /* dont skip pause action hack (rendering mame info screens or you wont see them and not know to press a key) */

/*auto frame skip options */
	if(options.frameskip >0 && options.frameskip >= 12)
	{
		if ( retro_audio_buff_active)
		{
			switch ( options.frameskip)
			{
				case 12: /* auto */
					skip_frame = retro_audio_buff_underrun ? 1 : 0;
				break;
				case 13: /* aggressive */
					skip_frame = (retro_audio_buff_occupancy < 33)  ? 1 : 0;
				break;
				case 14: /* max */
					skip_frame = (retro_audio_buff_occupancy < 50)  ? 1 : 0;
				break;
				default:
					skip_frame = options.frameskip;
				break;
			}
		}
	}
	else /*manual frameskip */
	 skip_frame = frameskip_table[options.frameskip][frameskip_counter];

	return skip_frame;
}

void osd_update_video_and_audio(struct mame_display *display)
{
#if X360_MAME_PROFILE
   UINT64 r4p_osd_start = round4p_ticks();
#endif
   RETRO_PERFORMANCE_INIT(perf_cb, update_video_and_audio);
   RETRO_PERFORMANCE_START(perf_cb, update_video_and_audio);

   if(display->changed_flags &
      ( GAME_BITMAP_CHANGED | GAME_PALETTE_CHANGED
      | GAME_VISIBLE_AREA_CHANGED | VECTOR_PIXELS_CHANGED))
   {
      /* Reinit video output on TATE mode toggle */
      if (options.tate_mode != tate_mode)
      {
         mame2003_video_reinit();
         display->changed_flags |= GAME_VISIBLE_AREA_CHANGED;
         tate_mode = options.tate_mode;
      }

      /* Update the visible area */
      if (display->changed_flags & GAME_VISIBLE_AREA_CHANGED)
         mame2003_video_update_visible_area(display);

      /* Update the palette */
      if (display->changed_flags & GAME_PALETTE_CHANGED)
         video_palette = display->game_palette;

      /* Update the game bitmap */
      if (video_cb)
      {
         if (!osd_skip_this_frame())
         {
#if defined(_XBOX360) && X360_GPU_INDEXED_PALETTE
            if (x360_gpu_indexed_present(display))
            {
               /* Presented current frame directly; no CPU PALTO565 conversion. */
            }
            else
#endif
            if (video_do_bypass)
            {
               unsigned min_y = display->game_visible_area.min_y;
               unsigned min_x = display->game_visible_area.min_x;
               unsigned pitch = display->game_bitmap->rowpixels * video_stride_out;
               char *base = &((char*)display->game_bitmap->base)[min_y*pitch + min_x*video_stride_out];
               raiden2_debug_output(base,vis_width,vis_height,pitch,video_stride_out,0,NULL);
               video_cb(base, vis_width, vis_height, pitch);
            }
            else
            {
#if defined(_XBOX360) && X360_VIDEO_WORKER
               if (x360_video_thread)
               {
                  /* Convert and present frame N inside the same retro_run().
                   * Audio output can still execute concurrently on HW thread 3,
                   * but video never queues N behind N-1, so Round2's fixed
                   * one-frame display latency is removed. */
                  if (!x360_video_present_current(display))
                  {
                     frame_convert(display);
                     raiden2_debug_output(video_buffer,vis_width,vis_height,vis_width*video_stride_out,video_stride_out,0,NULL);
                     video_cb(video_buffer, vis_width, vis_height, vis_width * video_stride_out);
                  }
               }
               else
#endif
               {
                  frame_convert(display);
                  raiden2_debug_output(video_buffer,vis_width,vis_height,vis_width*video_stride_out,video_stride_out,0,NULL);
                  video_cb(video_buffer, vis_width, vis_height, vis_width * video_stride_out);
               }
            }
         }
         else
         {
#if defined(_XBOX360) && X360_VIDEO_WORKER
            /* Same-frame worker never owns a frame across retro_run(), so a
             * skipped frame is a normal libretro frame-dupe. */
            if (!video_do_bypass && x360_video_thread)
               x360_video_wait();
#endif
            video_cb(NULL, vis_width, vis_height, vis_width * video_stride_out);
            raiden2_debug_output(NULL,0,0,0,0,0,NULL);
         }
      }
   }

   /* Update LED indicators state */
   if (led_state_cb && display->changed_flags & LED_STATE_CHANGED)
   {
      static unsigned long prev_led_state = 0;
      unsigned long o = prev_led_state;
      unsigned long n = display->led_state;
      int led;
      for(led=0;led<MAX_LED;led++)
      {
         if((o & 0x01) != (n & 0x01))
         {
            led_state_cb(led,n&0x01);
         }
         o >>= 1; n >>= 1;
      }
      prev_led_state = display->led_state;
   }

   gotFrame = 1;

  
   RETRO_PERFORMANCE_STOP(perf_cb, update_video_and_audio);
#if X360_MAME_PROFILE
   round4p_profile.osd_ticks += round4p_ticks() - r4p_osd_start;
   round4p_profile.osd_calls++;
#endif
}

struct mame_bitmap *osd_override_snapshot(struct mame_bitmap *bitmap, struct rectangle *bounds)
{
   return NULL;
}
