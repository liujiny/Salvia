/* Copy active bytes only; texture pitch padding belongs to the frontend. */
static void x360_copy565_rows(void *dst, unsigned dst_pitch,
 const void *src, unsigned src_pitch, unsigned row_bytes, unsigned height)
{
 unsigned y;
 if (dst_pitch == row_bytes && src_pitch == row_bytes)
  memcpy(dst, src, (size_t)row_bytes * height);
 else
  for (y = 0; y < height; ++y)
   memcpy((UINT8 *)dst + y * dst_pitch,
          (const UINT8 *)src + y * src_pitch, row_bytes);
}

/* Host-native MAME bitmap/palette, not guest CPU memory. C89 compatible.
   Tile the transpose so adjacent columns reuse source cache lines instead
   of walking the entire source height for each output row. */
static void x360_rotate565(const UINT16 *input, UINT16 *output,
 int pitch, int out_stride, int x0, int y0, int w, int h,
 const UINT32 *palette, int flip_x, int flip_y)
{
 int bx, by, ox, oy, endx, endy, sx, sy;
 UINT32 color;
 for (by = 0; by < w; by += 16)
 {
  endy = by + 16 < w ? by + 16 : w;
  for (bx = 0; bx < h; bx += 16)
  {
   endx = bx + 16 < h ? bx + 16 : h;
   for (oy = by; oy < endy; ++oy)
   {
    sx = x0 + (flip_y ? w - 1 - oy : oy);
    for (ox = bx; ox < endx; ++ox)
    {
     sy = y0 + (flip_x ? h - 1 - ox : ox);
     color = palette[input[sy * pitch + sx]];
     output[oy * out_stride + ox] = (UINT16)
      (((color & 0xf80000) >> 8) | ((color & 0xfc00) >> 5) |
       ((color & 0xf8) >> 3));
    }
   }
  }
 }
}
