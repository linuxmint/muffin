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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_BITMAP_H__
#define __COGL_BITMAP_H__

/* XXX: We forward declare CoglBitmap here to allow for circular
 * dependencies between some headers */
typedef struct _CoglBitmap CoglBitmap;

#include <cogl/cogl-types.h>
#include <cogl/cogl-buffer.h>
#include <cogl/cogl-context.h>
#include <cogl/cogl-pixel-buffer.h>
#include <cogl/cogl-pixel-format.h>

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * cogl_bitmap_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_bitmap_get_gtype (void);

/**
 * SECTION:cogl-bitmap
 * @short_description: Functions for loading images
 *
 * Cogl allows loading image data into memory as CoglBitmaps without
 * loading them immediately into GPU textures.
 *
 * #CoglBitmap is available since Cogl 1.0
 */


/**
 * cogl_bitmap_new_from_file:
 * @filename: the file to load.
 * @error: a #GError or %NULL.
 *
 * Loads an image file from disk. This function can be safely called from
 * within a thread.
 *
 * Return value: (transfer full): a #CoglBitmap to the new loaded
 *               image data, or %NULL if loading the image failed.
 *
 * Since: 1.0
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_from_file (const char *filename,
                           GError **error);

/**
 * cogl_bitmap_new_from_buffer: (skip)
 * @buffer: A #CoglBuffer containing image data
 * @format: The #CoglPixelFormat defining the format of the image data
 *          in the given @buffer.
 * @width: The width of the image data in the given @buffer.
 * @height: The height of the image data in the given @buffer.
 * @rowstride: The rowstride in bytes of the image data in the given @buffer.
 * @offset: The offset into the given @buffer to the first pixel that
 *          should be considered part of the #CoglBitmap.
 *
 * Wraps some image data that has been uploaded into a #CoglBuffer as
 * a #CoglBitmap. The data is not copied in this process.
 *
 * Return value: (transfer full): a #CoglBitmap encapsulating the given @buffer.
 *
 * Since: 1.8
 * Stability: unstable
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_from_buffer (CoglBuffer *buffer,
                             CoglPixelFormat format,
                             int width,
                             int height,
                             int rowstride,
                             int offset);

/**
 * cogl_bitmap_new_with_size: (skip)
 * @context: A #CoglContext
 * @width: width of the bitmap in pixels
 * @height: height of the bitmap in pixels
 * @format: the format of the pixels the array will store
 *
 * Creates a new #CoglBitmap with the given width, height and format.
 * The initial contents of the bitmap are undefined.
 *
 * The data for the bitmap will be stored in a newly created
 * #CoglPixelBuffer. You can get a pointer to the pixel buffer using
 * cogl_bitmap_get_buffer(). The #CoglBuffer API can then be
 * used to fill the bitmap with data.
 *
 * <note>Cogl will try its best to provide a hardware array you can
 * map, write into and effectively do a zero copy upload when creating
 * a texture from it with cogl_texture_new_from_bitmap(). For various
 * reasons, such arrays are likely to have a stride larger than width
 * * bytes_per_pixel. The user must take the stride into account when
 * writing into it. The stride can be retrieved with
 * cogl_bitmap_get_rowstride().</note>
 *
 * Return value: (transfer full): a #CoglPixelBuffer representing the
 *               newly created array or %NULL on failure
 *
 * Since: 1.10
 * Stability: Unstable
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_with_size (CoglContext *context,
                           unsigned int width,
                           unsigned int height,
                           CoglPixelFormat format);

/**
 * cogl_bitmap_new_for_data: (skip)
 * @context: A #CoglContext
 * @width: The width of the bitmap.
 * @height: The height of the bitmap.
 * @format: The format of the pixel data.
 * @rowstride: The rowstride of the bitmap (the number of bytes from
 *   the start of one row of the bitmap to the next).
 * @data: A pointer to the data. The bitmap will take ownership of this data.
 *
 * Creates a bitmap using some existing data. The data is not copied
 * so the application must keep the buffer alive for the lifetime of
 * the #CoglBitmap. This can be used for example with
 * cogl_framebuffer_read_pixels_into_bitmap() to read data directly
 * into an application buffer with the specified rowstride.
 *
 * Return value: (transfer full): A new #CoglBitmap.
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_for_data (CoglContext *context,
                          int width,
                          int height,
                          CoglPixelFormat format,
                          int rowstride,
                          uint8_t *data);

/**
 * cogl_bitmap_get_format:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the #CoglPixelFormat that the data for the bitmap is in.
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT CoglPixelFormat
cogl_bitmap_get_format (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_width:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the width of the bitmap
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT int
cogl_bitmap_get_width (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_height:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the height of the bitmap
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT int
cogl_bitmap_get_height (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_rowstride:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the rowstride of the bitmap. This is the number of
 *   bytes between the address of start of one row to the address of the
 *   next row in the image.
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT int
cogl_bitmap_get_rowstride (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_buffer: (skip)
 * @bitmap: A #CoglBitmap
 *
 * Return value: (transfer none): the #CoglPixelBuffer that this
 *   buffer uses for storage. Note that if the bitmap was created with
 *   cogl_bitmap_new_from_file() then it will not actually be using a
 *   pixel buffer and this function will return %NULL.
 * Stability: unstable
 * Since: 1.10
 */
COGL_EXPORT CoglPixelBuffer *
cogl_bitmap_get_buffer (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_size_from_file:
 * @filename: the file to check
 * @width: (out): return location for the bitmap width, or %NULL
 * @height: (out): return location for the bitmap height, or %NULL
 *
 * Parses an image file enough to extract the width and height
 * of the bitmap.
 *
 * Return value: %TRUE if the image was successfully parsed
 *
 * Since: 1.0
 */
COGL_EXPORT gboolean
cogl_bitmap_get_size_from_file (const char *filename,
                                int *width,
                                int *height);

/**
 * cogl_is_bitmap:
 * @object: a #CoglObject pointer
 *
 * Checks whether @object is a #CoglBitmap
 *
 * Return value: %TRUE if the passed @object represents a bitmap,
 *   and %FALSE otherwise
 *
 * Since: 1.0
 */
COGL_EXPORT gboolean
cogl_is_bitmap (void *object);

/**
 * COGL_BITMAP_ERROR:
 *
 * #GError domain for bitmap errors.
 *
 * Since: 1.4
 */
#define COGL_BITMAP_ERROR (cogl_bitmap_error_quark ())

/**
 * CoglBitmapError:
 * @COGL_BITMAP_ERROR_FAILED: Generic failure code, something went
 *   wrong.
 * @COGL_BITMAP_ERROR_UNKNOWN_TYPE: Unknown image type.
 * @COGL_BITMAP_ERROR_CORRUPT_IMAGE: An image file was broken somehow.
 *
 * Error codes that can be thrown when performing bitmap
 * operations. Note that gdk_pixbuf_new_from_file() can also throw
 * errors directly from the underlying image loading library. For
 * example, if GdkPixbuf is used then errors #GdkPixbufError<!-- -->s
 * will be used directly.
 *
 * Since: 1.4
 */
typedef enum
{
  COGL_BITMAP_ERROR_FAILED,
  COGL_BITMAP_ERROR_UNKNOWN_TYPE,
  COGL_BITMAP_ERROR_CORRUPT_IMAGE
} CoglBitmapError;

COGL_EXPORT
uint32_t cogl_bitmap_error_quark (void);

G_END_DECLS

#endif /* __COGL_BITMAP_H__ */
