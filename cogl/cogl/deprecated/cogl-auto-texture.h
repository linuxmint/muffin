/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2014 Intel Corporation.
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

#ifndef __COGL_AUTO_TEXTURE_H__
#define __COGL_AUTO_TEXTURE_H__

G_BEGIN_DECLS

#include <cogl/cogl-texture.h>

/**
 * cogl_texture_new_with_size:
 * @width: width of texture in pixels.
 * @height: height of texture in pixels.
 * @flags: Optional flags for the texture, or %COGL_TEXTURE_NONE
 * @internal_format: the #CoglPixelFormat to use for the GPU storage of the
 *    texture.
 *
 * Creates a new #CoglTexture with the specified dimensions and pixel format.
 *
 * Return value: (transfer full): A newly created #CoglTexture or %NULL on failure
 *
 * Since: 0.8
 * Deprecated: 1.18: Use specific constructors such as
 *                   cogl_texture_2d_new_with_size()
 */
COGL_DEPRECATED_FOR (cogl_texture_2d_new_with_size__OR__cogl_texture_2d_sliced_new_with_size)
COGL_EXPORT CoglTexture *
cogl_texture_new_with_size (unsigned int width,
                            unsigned int height,
                            CoglTextureFlags flags,
                            CoglPixelFormat internal_format);

/**
 * cogl_texture_new_from_file:
 * @filename: the file to load
 * @flags: Optional flags for the texture, or %COGL_TEXTURE_NONE
 * @internal_format: the #CoglPixelFormat to use for the GPU storage of the
 *    texture. If %COGL_PIXEL_FORMAT_ANY is given then a premultiplied
 *    format similar to the format of the source data will be used. The
 *    default blending equations of Cogl expect premultiplied color data;
 *    the main use of passing a non-premultiplied format here is if you
 *    have non-premultiplied source data and are going to adjust the blend
 *    mode (see cogl_material_set_blend()) or use the data for something
 *    other than straight blending.
 * @error: return location for a #GError or %NULL
 *
 * Creates a #CoglTexture from an image file.
 *
 * Return value: (transfer full): A newly created #CoglTexture or
 *               %NULL on failure
 *
 * Since: 0.8
 * Deprecated: 1.18: Use specific constructors such as
 *                   cogl_texture_2d_new_from_file()
 */
COGL_DEPRECATED_FOR (cogl_texture_2d_new_from_file__OR__cogl_texture_2d_sliced_new_from_file)
COGL_EXPORT CoglTexture *
cogl_texture_new_from_file (const char       *filename,
                            CoglTextureFlags   flags,
                            CoglPixelFormat    internal_format,
                            GError           **error);

/**
 * cogl_texture_new_from_data:
 * @width: width of texture in pixels
 * @height: height of texture in pixels
 * @flags: Optional flags for the texture, or %COGL_TEXTURE_NONE
 * @format: the #CoglPixelFormat the buffer is stored in in RAM
 * @internal_format: the #CoglPixelFormat that will be used for storing
 *    the buffer on the GPU. If COGL_PIXEL_FORMAT_ANY is given then a
 *    premultiplied format similar to the format of the source data will
 *    be used. The default blending equations of Cogl expect premultiplied
 *    color data; the main use of passing a non-premultiplied format here
 *    is if you have non-premultiplied source data and are going to adjust
 *    the blend mode (see cogl_material_set_blend()) or use the data for
 *    something other than straight blending.
 * @rowstride: the memory offset in bytes between the starts of
 *    scanlines in @data
 * @data: (array): pointer the memory region where the source buffer resides
 *
 * Creates a new #CoglTexture based on data residing in memory.
 *
 * Return value: (transfer full): A newly created #CoglTexture or
 *               %NULL on failure
 *
 * Since: 0.8
 * Deprecated: 1.18: Use specific constructors such as
 *                   cogl_texture_2d_new_from_data()
 */
COGL_DEPRECATED_FOR (cogl_texture_2d_new_from_data__OR__cogl_texture_2d_sliced_new_from_data)
COGL_EXPORT CoglTexture *
cogl_texture_new_from_data (int width,
                            int height,
                            CoglTextureFlags flags,
                            CoglPixelFormat format,
                            CoglPixelFormat internal_format,
                            int rowstride,
                            const uint8_t *data);

/**
 * cogl_texture_new_from_bitmap:
 * @bitmap: A #CoglBitmap pointer
 * @flags: Optional flags for the texture, or %COGL_TEXTURE_NONE
 * @internal_format: the #CoglPixelFormat to use for the GPU storage of the
 * texture
 *
 * Creates a #CoglTexture from a #CoglBitmap.
 *
 * Return value: (transfer full): A newly created #CoglTexture or
 *               %NULL on failure
 *
 * Since: 1.0
 * Deprecated: 1.18: Use specific constructors such as
 *                   cogl_texture_2d_new_from_bitmap()
 */
COGL_DEPRECATED_FOR (cogl_texture_2d_new_from_bitmap__OR__cogl_texture_2d_sliced_new_from_bitmap)
COGL_EXPORT CoglTexture *
cogl_texture_new_from_bitmap (CoglBitmap *bitmap,
                              CoglTextureFlags flags,
                              CoglPixelFormat internal_format);

/**
 * cogl_texture_new_from_sub_texture:
 * @full_texture: a #CoglTexture pointer
 * @sub_x: X coordinate of the top-left of the subregion
 * @sub_y: Y coordinate of the top-left of the subregion
 * @sub_width: Width in pixels of the subregion
 * @sub_height: Height in pixels of the subregion
 *
 * Creates a new texture which represents a subregion of another
 * texture. The GL resources will be shared so that no new texture
 * data is actually allocated.
 *
 * Sub textures have undefined behaviour texture coordinates outside
 * of the range [0,1] are used.
 *
 * The sub texture will keep a reference to the full texture so you do
 * not need to keep one separately if you only want to use the sub
 * texture.
 *
 * Return value: (transfer full): A newly created #CoglTexture or
 *               %NULL on failure
 * Since: 1.2
 * Deprecated: 1.18: Use cogl_sub_texture_new()
 */
COGL_DEPRECATED_FOR (cogl_sub_texture_new)
COGL_EXPORT CoglTexture *
cogl_texture_new_from_sub_texture (CoglTexture *full_texture,
                                   int sub_x,
                                   int sub_y,
                                   int sub_width,
                                   int sub_height);

G_END_DECLS

#endif /* __COGL_AUTO_TEXTURE_H__ */
