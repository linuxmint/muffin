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
#include "cogl-buffer-gl-private.h"
#include "cogl-bitmap-gl-private.h"

uint8_t *
_cogl_bitmap_gl_bind (CoglBitmap *bitmap,
                      CoglBufferAccess access,
                      CoglBufferMapHint hints,
                      GError **error)
{
  uint8_t *ptr;
  GError *internal_error = NULL;

  g_return_val_if_fail (access & (COGL_BUFFER_ACCESS_READ |
                                  COGL_BUFFER_ACCESS_WRITE),
                        NULL);

  /* Divert to another bitmap if this data is shared */
  if (bitmap->shared_bmp)
    return _cogl_bitmap_gl_bind (bitmap->shared_bmp, access, hints, error);

  g_return_val_if_fail (!bitmap->bound, NULL);

  /* If the bitmap wasn't created from a buffer then the
     implementation of bind is the same as map */
  if (bitmap->buffer == NULL)
    {
      uint8_t *data = _cogl_bitmap_map (bitmap, access, hints, error);
      if (data)
        bitmap->bound = TRUE;
      return data;
    }

  if (access == COGL_BUFFER_ACCESS_READ)
    ptr = _cogl_buffer_gl_bind (bitmap->buffer,
                                COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK,
                                &internal_error);
  else if (access == COGL_BUFFER_ACCESS_WRITE)
    ptr = _cogl_buffer_gl_bind (bitmap->buffer,
                                COGL_BUFFER_BIND_TARGET_PIXEL_PACK,
                                &internal_error);
  else
    {
      ptr = NULL;
      g_assert_not_reached ();
      return NULL;
    }

  /* NB: _cogl_buffer_gl_bind() may return NULL in non-error
   * conditions so we have to explicitly check internal_error to see
   * if an exception was thrown */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      return NULL;
    }

  bitmap->bound = TRUE;

  /* The data pointer actually stores the offset */
  return ptr + GPOINTER_TO_INT (bitmap->data);
}

void
_cogl_bitmap_gl_unbind (CoglBitmap *bitmap)
{
  /* Divert to another bitmap if this data is shared */
  if (bitmap->shared_bmp)
    {
      _cogl_bitmap_gl_unbind (bitmap->shared_bmp);
      return;
    }

  g_assert (bitmap->bound);
  bitmap->bound = FALSE;

  /* If the bitmap wasn't created from a pixel array then the
     implementation of unbind is the same as unmap */
  if (bitmap->buffer)
    _cogl_buffer_gl_unbind (bitmap->buffer);
  else
    _cogl_bitmap_unmap (bitmap);
}
