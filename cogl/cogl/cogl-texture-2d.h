/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011,2013 Intel Corporation.
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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_TEXTURE_2D_H
#define __COGL_TEXTURE_2D_H

#include "cogl-context.h"
#include "cogl-bitmap.h"

#ifdef COGL_HAS_EGL_SUPPORT
#include "cogl-egl-defines.h"
#endif

G_BEGIN_DECLS

/**
 * SECTION:cogl-texture-2d
 * @short_description: Functions for creating and manipulating 2D textures
 *
 * These functions allow low-level 2D textures to be allocated. These
 * differ from sliced textures for example which may internally be
 * made up of multiple 2D textures, or atlas textures where Cogl must
 * internally modify user texture coordinates before they can be used
 * by the GPU.
 */

typedef struct _CoglTexture2D CoglTexture2D;
#define COGL_TEXTURE_2D(X) ((CoglTexture2D *)X)

typedef enum _CoglEglImageFlags
{
  COGL_EGL_IMAGE_FLAG_NONE = 0,
  COGL_EGL_IMAGE_FLAG_NO_GET_DATA = 1 << 0,
} CoglEglImageFlags;

/**
 * cogl_texture_2d_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_texture_2d_get_gtype (void);

/**
 * cogl_is_texture_2d:
 * @object: A #CoglObject
 *
 * Gets whether the given object references an existing #CoglTexture2D
 * object.
 *
 * Return value: %TRUE if the object references a #CoglTexture2D,
 *   %FALSE otherwise
 */
COGL_EXPORT gboolean
cogl_is_texture_2d (void *object);

/**
 * cogl_texture_2d_new_with_size: (skip)
 * @ctx: A #CoglContext
 * @width: Width of the texture to allocate
 * @height: Height of the texture to allocate
 *
 * Creates a low-level #CoglTexture2D texture with a given @width and
 * @height that your GPU can texture from directly.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or preferably let Cogl
 * automatically allocate storage lazily when it may know more about
 * how the texture is being used and can optimize how it is allocated.
 *
 * The texture is still configurable until it has been allocated so
 * for example you can influence the internal format of the texture
 * using cogl_texture_set_components() and
 * cogl_texture_set_premultiplied().
 *
 * Returns: (transfer full): A new #CoglTexture2D object with no storage yet allocated.
 *
 * Since: 2.0
 */
COGL_EXPORT CoglTexture2D *
cogl_texture_2d_new_with_size (CoglContext *ctx,
                               int width,
                               int height);

/**
 * cogl_texture_2d_new_from_file: (skip)
 * @ctx: A #CoglContext
 * @filename: the file to load
 * @error: A #GError to catch exceptional errors or %NULL
 *
 * Creates a low-level #CoglTexture2D texture from an image file.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or preferably let Cogl
 * automatically allocate storage lazily when it may know more about
 * how the texture is being used and can optimize how it is allocated.
 *
 * The texture is still configurable until it has been allocated so
 * for example you can influence the internal format of the texture
 * using cogl_texture_set_components() and
 * cogl_texture_set_premultiplied().
 *
 * Return value: (transfer full): A newly created #CoglTexture2D or %NULL on failure
 *               and @error will be updated.
 *
 * Since: 1.16
 */
COGL_EXPORT CoglTexture2D *
cogl_texture_2d_new_from_file (CoglContext *ctx,
                               const char *filename,
                               GError **error);

/**
 * cogl_texture_2d_new_from_data: (skip)
 * @ctx: A #CoglContext
 * @width: width of texture in pixels
 * @height: height of texture in pixels
 * @format: the #CoglPixelFormat the buffer is stored in in RAM
 * @rowstride: the memory offset in bytes between the starts of
 *    scanlines in @data. A value of 0 will make Cogl automatically
 *    calculate @rowstride from @width and @format.
 * @data: pointer the memory region where the source buffer resides
 * @error: A #GError for exceptions
 *
 * Creates a low-level #CoglTexture2D texture based on data residing
 * in memory.
 *
 * <note>This api will always immediately allocate GPU memory for the
 * texture and upload the given data so that the @data pointer does
 * not need to remain valid once this function returns. This means it
 * is not possible to configure the texture before it is allocated. If
 * you do need to configure the texture before allocation (to specify
 * constraints on the internal format for example) then you can
 * instead create a #CoglBitmap for your data and use
 * cogl_texture_2d_new_from_bitmap() or use
 * cogl_texture_2d_new_with_size() and then upload data using
 * cogl_texture_set_data()</note>
 *
 * Returns: (transfer full): A newly allocated #CoglTexture2D, or if
 *          the size is not supported (because it is too large or a
 *          non-power-of-two size that the hardware doesn't support)
 *          it will return %NULL and set @error.
 *
 * Since: 2.0
 */
COGL_EXPORT CoglTexture2D *
cogl_texture_2d_new_from_data (CoglContext *ctx,
                               int width,
                               int height,
                               CoglPixelFormat format,
                               int rowstride,
                               const uint8_t *data,
                               GError **error);

/**
 * cogl_texture_2d_new_from_bitmap:
 * @bitmap: A #CoglBitmap
 *
 * Creates a low-level #CoglTexture2D texture based on data residing
 * in a #CoglBitmap.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or preferably let Cogl
 * automatically allocate storage lazily when it may know more about
 * how the texture is being used and can optimize how it is allocated.
 *
 * The texture is still configurable until it has been allocated so
 * for example you can influence the internal format of the texture
 * using cogl_texture_set_components() and
 * cogl_texture_set_premultiplied().
 *
 * Returns: (transfer full): A newly allocated #CoglTexture2D
 *
 * Since: 2.0
 * Stability: unstable
 */
COGL_EXPORT CoglTexture2D *
cogl_texture_2d_new_from_bitmap (CoglBitmap *bitmap);

/**
 * cogl_egl_texture_2d_new_from_image: (skip)
 */
#if defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base)
/* NB: The reason we require the width, height and format to be passed
 * even though they may seem redundant is because GLES 1/2 don't
 * provide a way to query these properties. */
COGL_EXPORT CoglTexture2D *
cogl_egl_texture_2d_new_from_image (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat format,
                                    EGLImageKHR image,
                                    CoglEglImageFlags flags,
                                    GError **error);

typedef gboolean (*CoglTexture2DEGLImageExternalAlloc) (CoglTexture2D *tex_2d,
                                                        gpointer user_data,
                                                        GError **error);

/**
 * cogl_texture_2d_new_from_egl_image_external: (skip)
 */
COGL_EXPORT CoglTexture2D *
cogl_texture_2d_new_from_egl_image_external (CoglContext *ctx,
                                             int width,
                                             int height,
                                             CoglTexture2DEGLImageExternalAlloc alloc,
                                             gpointer user_data,
                                             GDestroyNotify destroy,
                                             GError **error);

COGL_EXPORT void
cogl_texture_2d_egl_image_external_bind (CoglTexture2D *tex_2d);

COGL_EXPORT void
cogl_texture_2d_egl_image_external_alloc_finish (CoglTexture2D *tex_2d,
						 void *user_data,
						 GDestroyNotify destroy);
#endif

G_END_DECLS

#endif /* __COGL_TEXTURE_2D_H */
