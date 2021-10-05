/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#include "cogl-util.h"
#include "cogl-debug.h"
#include "cogl-private.h"
#include "cogl-bitmap-private.h"
#include "cogl-buffer-private.h"
#include "cogl-pixel-buffer.h"
#include "cogl-context-private.h"
#include "cogl-gtype-private.h"

#include <string.h>

static void _cogl_bitmap_free (CoglBitmap *bmp);

COGL_OBJECT_DEFINE (Bitmap, bitmap);
COGL_GTYPE_DEFINE_CLASS (Bitmap, bitmap);

static void
_cogl_bitmap_free (CoglBitmap *bmp)
{
  g_assert (!bmp->mapped);
  g_assert (!bmp->bound);

  if (bmp->shared_bmp)
    cogl_object_unref (bmp->shared_bmp);

  if (bmp->buffer)
    cogl_object_unref (bmp->buffer);

  g_slice_free (CoglBitmap, bmp);
}

gboolean
_cogl_bitmap_convert_premult_status (CoglBitmap *bmp,
                                     CoglPixelFormat dst_format,
                                     GError **error)
{
  /* Do we need to unpremultiply? */
  if ((bmp->format & COGL_PREMULT_BIT) > 0 &&
      (dst_format & COGL_PREMULT_BIT) == 0 &&
      COGL_PIXEL_FORMAT_CAN_HAVE_PREMULT (dst_format))
    return _cogl_bitmap_unpremult (bmp, error);

  /* Do we need to premultiply? */
  if ((bmp->format & COGL_PREMULT_BIT) == 0 &&
      COGL_PIXEL_FORMAT_CAN_HAVE_PREMULT (bmp->format) &&
      (dst_format & COGL_PREMULT_BIT) > 0)
    /* Try premultiplying using imaging library */
    return _cogl_bitmap_premult (bmp, error);

  return TRUE;
}

CoglBitmap *
_cogl_bitmap_copy (CoglBitmap *src_bmp,
                   GError **error)
{
  CoglBitmap *dst_bmp;
  CoglPixelFormat src_format = cogl_bitmap_get_format (src_bmp);
  int width = cogl_bitmap_get_width (src_bmp);
  int height = cogl_bitmap_get_height (src_bmp);

  dst_bmp =
    _cogl_bitmap_new_with_malloc_buffer (src_bmp->context,
                                         width, height,
                                         src_format,
                                         error);
  if (!dst_bmp)
    return NULL;

  if (!_cogl_bitmap_copy_subregion (src_bmp,
                                    dst_bmp,
                                    0, 0, /* src_x/y */
                                    0, 0, /* dst_x/y */
                                    width, height,
                                    error))
    {
      cogl_object_unref (dst_bmp);
      return NULL;
    }

  return dst_bmp;
}

gboolean
_cogl_bitmap_copy_subregion (CoglBitmap *src,
			     CoglBitmap *dst,
			     int src_x,
			     int src_y,
			     int dst_x,
			     int dst_y,
			     int width,
			     int height,
                             GError **error)
{
  uint8_t *srcdata;
  uint8_t *dstdata;
  int bpp;
  int line;
  gboolean succeeded = FALSE;

  /* Intended only for fast copies when format is equal! */
  g_return_val_if_fail ((src->format & ~COGL_PREMULT_BIT) ==
                        (dst->format & ~COGL_PREMULT_BIT),
                        FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (src->format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (src->format, 0);

  if ((srcdata = _cogl_bitmap_map (src, COGL_BUFFER_ACCESS_READ, 0, error)))
    {
      if ((dstdata =
           _cogl_bitmap_map (dst, COGL_BUFFER_ACCESS_WRITE, 0, error)))
        {
          srcdata += src_y * src->rowstride + src_x * bpp;
          dstdata += dst_y * dst->rowstride + dst_x * bpp;

          for (line = 0; line < height; ++line)
            {
              memcpy (dstdata, srcdata, width * bpp);
              srcdata += src->rowstride;
              dstdata += dst->rowstride;
            }

          succeeded = TRUE;

          _cogl_bitmap_unmap (dst);
        }

      _cogl_bitmap_unmap (src);
    }

  return succeeded;
}

gboolean
cogl_bitmap_get_size_from_file (const char *filename,
                                int        *width,
                                int        *height)
{
  return _cogl_bitmap_get_size_from_file (filename, width, height);
}

CoglBitmap *
cogl_bitmap_new_for_data (CoglContext *context,
                          int width,
                          int height,
                          CoglPixelFormat format,
                          int rowstride,
                          uint8_t *data)
{
  CoglBitmap *bmp;

  g_return_val_if_fail (cogl_is_context (context), NULL);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, NULL);

  /* Rowstride from width if not given */
  if (rowstride == 0)
    rowstride = width * cogl_pixel_format_get_bytes_per_pixel (format, 0);

  bmp = g_slice_new (CoglBitmap);
  bmp->context = context;
  bmp->format = format;
  bmp->width = width;
  bmp->height = height;
  bmp->rowstride = rowstride;
  bmp->data = data;
  bmp->mapped = FALSE;
  bmp->bound = FALSE;
  bmp->shared_bmp = NULL;
  bmp->buffer = NULL;

  return _cogl_bitmap_object_new (bmp);
}

CoglBitmap *
_cogl_bitmap_new_with_malloc_buffer (CoglContext *context,
                                     unsigned int width,
                                     unsigned int height,
                                     CoglPixelFormat format,
                                     GError **error)
{
  static CoglUserDataKey bitmap_free_key;
  int bpp;
  int rowstride;
  uint8_t *data;
  CoglBitmap *bitmap;

  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, NULL);

  /* Try to malloc the data */
  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);
  rowstride = ((width * bpp) + 3) & ~3;
  data = g_try_malloc (rowstride * height);

  if (!data)
    {
      g_set_error_literal (error, COGL_SYSTEM_ERROR,
                           COGL_SYSTEM_ERROR_NO_MEMORY,
                           "Failed to allocate memory for bitmap");
      return NULL;
    }

  /* Now create the bitmap */
  bitmap = cogl_bitmap_new_for_data (context,
                                     width, height,
                                     format,
                                     rowstride,
                                     data);
  cogl_object_set_user_data (COGL_OBJECT (bitmap),
                             &bitmap_free_key,
                             data,
                             g_free);

  return bitmap;
}

CoglBitmap *
_cogl_bitmap_new_shared (CoglBitmap              *shared_bmp,
                         CoglPixelFormat          format,
                         int                      width,
                         int                      height,
                         int                      rowstride)
{
  CoglBitmap *bmp;

  bmp = cogl_bitmap_new_for_data (shared_bmp->context,
                                  width, height,
                                  format,
                                  rowstride,
                                  NULL /* data */);

  bmp->shared_bmp = cogl_object_ref (shared_bmp);

  return bmp;
}

CoglBitmap *
cogl_bitmap_new_from_file (const char *filename,
                           GError **error)
{
  _COGL_GET_CONTEXT (ctx, NULL);

  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return _cogl_bitmap_from_file (ctx, filename, error);
}

CoglBitmap *
cogl_bitmap_new_from_buffer (CoglBuffer *buffer,
                             CoglPixelFormat format,
                             int width,
                             int height,
                             int rowstride,
                             int offset)
{
  CoglBitmap *bmp;

  g_return_val_if_fail (cogl_is_buffer (buffer), NULL);

  bmp = cogl_bitmap_new_for_data (buffer->context,
                                  width, height,
                                  format,
                                  rowstride,
                                  NULL /* data */);

  bmp->buffer = cogl_object_ref (buffer);
  bmp->data = GINT_TO_POINTER (offset);

  return bmp;
}

CoglBitmap *
cogl_bitmap_new_with_size (CoglContext *context,
                           unsigned int width,
                           unsigned int height,
                           CoglPixelFormat format)
{
  CoglPixelBuffer *pixel_buffer;
  CoglBitmap *bitmap;
  unsigned int rowstride;

  /* creating a buffer to store "any" format does not make sense */
  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, NULL);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, NULL);

  /* for now we fallback to cogl_pixel_buffer_new, later, we could ask
   * libdrm a tiled buffer for instance */
  rowstride = width * cogl_pixel_format_get_bytes_per_pixel (format, 0);

  pixel_buffer =
    cogl_pixel_buffer_new (context,
                           height * rowstride,
                           NULL); /* data */

  g_return_val_if_fail (pixel_buffer != NULL, NULL);

  bitmap = cogl_bitmap_new_from_buffer (COGL_BUFFER (pixel_buffer),
                                        format,
                                        width, height,
                                        rowstride,
                                        0 /* offset */);

  cogl_object_unref (pixel_buffer);

  return bitmap;
}

CoglPixelFormat
cogl_bitmap_get_format (CoglBitmap *bitmap)
{
  return bitmap->format;
}

void
_cogl_bitmap_set_format (CoglBitmap *bitmap,
                         CoglPixelFormat format)
{
  bitmap->format = format;
}

int
cogl_bitmap_get_width (CoglBitmap *bitmap)
{
  return bitmap->width;
}

int
cogl_bitmap_get_height (CoglBitmap *bitmap)
{
  return bitmap->height;
}

int
cogl_bitmap_get_rowstride (CoglBitmap *bitmap)
{
  return bitmap->rowstride;
}

CoglPixelBuffer *
cogl_bitmap_get_buffer (CoglBitmap *bitmap)
{
  while (bitmap->shared_bmp)
    bitmap = bitmap->shared_bmp;

  return COGL_PIXEL_BUFFER (bitmap->buffer);
}

uint32_t
cogl_bitmap_error_quark (void)
{
  return g_quark_from_static_string ("cogl-bitmap-error-quark");
}

uint8_t *
_cogl_bitmap_map (CoglBitmap *bitmap,
                  CoglBufferAccess access,
                  CoglBufferMapHint hints,
                  GError **error)
{
  /* Divert to another bitmap if this data is shared */
  if (bitmap->shared_bmp)
    return _cogl_bitmap_map (bitmap->shared_bmp, access, hints, error);

  g_assert (!bitmap->mapped);

  if (bitmap->buffer)
    {
      uint8_t *data = _cogl_buffer_map (bitmap->buffer,
                                        access,
                                        hints,
                                        error);

      COGL_NOTE (BITMAP, "A pixel array is being mapped from a bitmap. This "
                 "usually means that some conversion on the pixel array is "
                 "needed so a sub-optimal format is being used.");

      if (data)
        {
          bitmap->mapped = TRUE;

          return data + GPOINTER_TO_INT (bitmap->data);
        }
      else
        return NULL;
    }
  else
    {
      bitmap->mapped = TRUE;

      return bitmap->data;
    }
}

void
_cogl_bitmap_unmap (CoglBitmap *bitmap)
{
  /* Divert to another bitmap if this data is shared */
  if (bitmap->shared_bmp)
    {
      _cogl_bitmap_unmap (bitmap->shared_bmp);
      return;
    }

  g_assert (bitmap->mapped);
  bitmap->mapped = FALSE;

  if (bitmap->buffer)
    cogl_buffer_unmap (bitmap->buffer);
}

CoglContext *
_cogl_bitmap_get_context (CoglBitmap *bitmap)
{
  return bitmap->context;
}
