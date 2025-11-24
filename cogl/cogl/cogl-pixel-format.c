/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#include "cogl-config.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "cogl-pixel-format.h"

/* An entry to map CoglPixelFormats to their respective properties */
typedef struct _CoglPixelFormatInfo
{
  CoglPixelFormat cogl_format;
  const char *format_str;
  int aligned;                     /* Aligned components? (-1 if n/a) */
  uint8_t n_planes;

  /* Per-plane information */
  uint8_t bpp[COGL_PIXEL_FORMAT_MAX_PLANES];  /* Bytes per pixel              */
} CoglPixelFormatInfo;

static const CoglPixelFormatInfo format_info_table[] = {
  {
    .cogl_format = COGL_PIXEL_FORMAT_ANY,
    .format_str = "ANY",
    .n_planes = 0,
    .aligned = -1,
    .bpp = { 0 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_A_8,
    .format_str = "A_8",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 1 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGB_565,
    .format_str = "RGB_565",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_4444,
    .format_str = "RGBA_4444",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_5551,
    .format_str = "RGBA_5551",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_YUV,
    .format_str = "YUV",
    .n_planes = 1,
    .aligned = -1,
    .bpp = { 0 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_G_8,
    .format_str = "G_8",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 1 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RG_88,
    .format_str = "RG_88",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGB_888,
    .format_str = "RGB_888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 3 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGR_888,
    .format_str = "BGR_888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 3 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_8888,
    .format_str = "RGBA_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_8888,
    .format_str = "BGRA_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_8888,
    .format_str = "ARGB_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_8888,
    .format_str = "ABGR_8888",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_1010102,
    .format_str = "RGBA_1010102",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_1010102,
    .format_str = "BGRA_1010102",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_2101010,
    .format_str = "ARGB_2101010",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_2101010,
    .format_str = "ABGR_2101010",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE,
    .format_str = "RGBA_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_8888_PRE,
    .format_str = "BGRA_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_8888_PRE,
    .format_str = "ARGB_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_8888_PRE,
    .format_str = "ABGR_8888_PRE",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_4444_PRE,
    .format_str = "RGBA_4444_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_5551_PRE,
    .format_str = "RGBA_5551_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_1010102_PRE,
    .format_str = "RGBA_1010102_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_1010102_PRE,
    .format_str = "BGRA_1010102_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_2101010_PRE,
    .format_str = "ARGB_2101010_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_2101010_PRE,
    .format_str = "ABGR_2101010_PRE",
    .n_planes = 1,
    .aligned = 0,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_FP_16161616,
    .format_str = "RGBA_FP_16161616",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_FP_16161616,
    .format_str = "BGRA_FP_16161616",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_FP_16161616,
    .format_str = "ARGB_FP_16161616",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_FP_16161616,
    .format_str = "ABGR_FP_16161616",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE,
    .format_str = "RGBA_FP_16161616_PRE",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE,
    .format_str = "BGRA_FP_16161616_PRE",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE,
    .format_str = "ARGB_FP_16161616_PRE",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE,
    .format_str = "ABGR_FP_16161616_PRE",
    .n_planes = 1,
    .bpp = { 8 },
    .aligned = 1
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_DEPTH_16,
    .format_str = "DEPTH_16",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 2 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_DEPTH_32,
    .format_str = "DEPTH_32",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
  {
    .cogl_format = COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8,
    .format_str = "DEPTH_24_STENCIL_8",
    .n_planes = 1,
    .aligned = 1,
    .bpp = { 4 },
  },
};

int
cogl_pixel_format_get_bytes_per_pixel (CoglPixelFormat format,
                                       int             plane)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        {
          g_return_val_if_fail (plane < format_info_table[i].n_planes, 0);

          return format_info_table[i].bpp[plane];
        }
    }

  g_assert_not_reached ();
}

/* Note: this also refers to the mapping defined above for
 * cogl_pixel_format_get_bytes_per_pixel() */
gboolean
_cogl_pixel_format_is_endian_dependant (CoglPixelFormat format)
{
  int aligned = -1;
  size_t i;

  /* NB: currently checking whether the format components are aligned
   * or not determines whether the format is endian dependent or not.
   * In the future though we might consider adding formats with
   * aligned components that are also endian independant. */

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        {
          aligned = format_info_table[i].aligned;
          break;
        }
    }

  g_return_val_if_fail (aligned != -1, FALSE);

  return aligned;
}

int
cogl_pixel_format_get_n_planes (CoglPixelFormat format)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        return format_info_table[i].n_planes;
    }

  g_assert_not_reached ();
}

const char *
cogl_pixel_format_to_string (CoglPixelFormat format)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (format_info_table); i++)
    {
      if (format_info_table[i].cogl_format == format)
        return format_info_table[i].format_str;
    }

  g_assert_not_reached ();
}
