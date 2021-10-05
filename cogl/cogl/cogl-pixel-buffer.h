/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010 Intel Corporation.
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
 *
 * Authors:
 *   Damien Lespiau <damien.lespiau@intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_PIXEL_BUFFER_H__
#define __COGL_PIXEL_BUFFER_H__

/* XXX: We forward declare CoglPixelBuffer here to allow for circular
 * dependencies between some headers */
typedef struct _CoglPixelBuffer CoglPixelBuffer;

#include <cogl/cogl-types.h>
#include <cogl/cogl-context.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define COGL_PIXEL_BUFFER(buffer) ((CoglPixelBuffer *)(buffer))

/**
 * CoglPixelBuffer: (skip)
 */

/**
 * cogl_pixel_buffer_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_pixel_buffer_get_gtype (void);

/**
 * cogl_pixel_buffer_new:
 * @context: A #CoglContext
 * @size: The number of bytes to allocate for the pixel data.
 * @data: An optional pointer to vertex data to upload immediately
 *
 * Declares a new #CoglPixelBuffer of @size bytes to contain arrays of
 * pixels. Once declared, data can be set using cogl_buffer_set_data()
 * or by mapping it into the application's address space using
 * cogl_buffer_map().
 *
 * If @data isn't %NULL then @size bytes will be read from @data and
 * immediately copied into the new buffer.
 *
 * Return value: (transfer full): a newly allocated #CoglPixelBuffer
 *
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT CoglPixelBuffer *
cogl_pixel_buffer_new (CoglContext *context,
                       size_t size,
                       const void *data);

/**
 * cogl_is_pixel_buffer:
 * @object: a #CoglObject to test
 *
 * Checks whether @object is a pixel buffer.
 *
 * Return value: %TRUE if the @object is a pixel buffer, and %FALSE
 *   otherwise
 *
 * Since: 1.2
 * Stability: Unstable
 */
COGL_EXPORT gboolean
cogl_is_pixel_buffer (void *object);

G_END_DECLS

#endif /* __COGL_PIXEL_BUFFER_H__ */
