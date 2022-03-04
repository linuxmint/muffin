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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_TEXTURE_H__
#define __COGL_TEXTURE_H__

/* We forward declare the CoglTexture type here to avoid some circular
 * dependency issues with the following headers.
 */
#if defined(__COGL_H_INSIDE__) && !defined(COGL_ENABLE_MUTTER_API) && \
  !defined(COGL_GIR_SCANNING)
/* For the public C api we typedef interface types as void to avoid needing
 * lots of casting in code and instead we will rely on runtime type checking
 * for these objects. */
typedef void CoglTexture;
#else
typedef struct _CoglTexture CoglTexture;
#define COGL_TEXTURE(X) ((CoglTexture *)X)
#endif

#include <cogl/cogl-types.h>
#include <cogl/cogl-macros.h>
#include <cogl/cogl-defines.h>
#include <cogl/cogl-pixel-buffer.h>
#include <cogl/cogl-pixel-format.h>
#include <cogl/cogl-bitmap.h>

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * SECTION:cogl-texture
 * @short_description: Functions for creating and manipulating textures
 *
 * Cogl allows creating and manipulating textures using a uniform
 * API that tries to hide all the various complexities of creating,
 * loading and manipulating textures.
 */

#define COGL_TEXTURE_MAX_WASTE  127

/**
 * cogl_texture_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_texture_get_gtype (void);

/**
 * COGL_TEXTURE_ERROR:
 *
 * #GError domain for texture errors.
 *
 * Since: 1.8
 * Stability: Unstable
 */
#define COGL_TEXTURE_ERROR (cogl_texture_error_quark ())

/**
 * CoglTextureError:
 * @COGL_TEXTURE_ERROR_SIZE: Unsupported size
 * @COGL_TEXTURE_ERROR_FORMAT: Unsupported format
 * @COGL_TEXTURE_ERROR_TYPE: A primitive texture type that is
 *   unsupported by the driver was used
 *
 * Error codes that can be thrown when allocating textures.
 *
 * Since: 1.8
 * Stability: Unstable
 */
typedef enum
{
  COGL_TEXTURE_ERROR_SIZE,
  COGL_TEXTURE_ERROR_FORMAT,
  COGL_TEXTURE_ERROR_BAD_PARAMETER,
  COGL_TEXTURE_ERROR_TYPE
} CoglTextureError;

COGL_EXPORT
uint32_t cogl_texture_error_quark (void);

/**
 * cogl_is_texture:
 * @object: A #CoglObject pointer
 *
 * Gets whether the given object references a texture object.
 *
 * Return value: %TRUE if the @object references a texture, and
 *   %FALSE otherwise
 */
COGL_EXPORT gboolean
cogl_is_texture (void *object);

/**
 * CoglTextureComponents:
 * @COGL_TEXTURE_COMPONENTS_A: Only the alpha component
 * @COGL_TEXTURE_COMPONENTS_RG: Red and green components. Note that
 *   this can only be used if the %COGL_FEATURE_ID_TEXTURE_RG feature
 *   is advertised.
 * @COGL_TEXTURE_COMPONENTS_RGB: Red, green and blue components
 * @COGL_TEXTURE_COMPONENTS_RGBA: Red, green, blue and alpha components
 * @COGL_TEXTURE_COMPONENTS_DEPTH: Only a depth component
 *
 * See cogl_texture_set_components().
 *
 * Since: 1.18
 */
typedef enum _CoglTextureComponents
{
  COGL_TEXTURE_COMPONENTS_A = 1,
  COGL_TEXTURE_COMPONENTS_RG,
  COGL_TEXTURE_COMPONENTS_RGB,
  COGL_TEXTURE_COMPONENTS_RGBA,
  COGL_TEXTURE_COMPONENTS_DEPTH
} CoglTextureComponents;

/**
 * cogl_texture_set_components:
 * @texture: a #CoglTexture pointer.
 *
 * Affects the internal storage format for this texture by specifying
 * what components will be required for sampling later.
 *
 * This api affects how data is uploaded to the GPU since unused
 * components can potentially be discarded from source data.
 *
 * For textures created by the ‘_with_size’ constructors the default
 * is %COGL_TEXTURE_COMPONENTS_RGBA. The other constructors which take
 * a %CoglBitmap or a data pointer default to the same components as
 * the pixel format of the data.
 *
 * Note that the %COGL_TEXTURE_COMPONENTS_RG format is not available
 * on all drivers. The availability can be determined by checking for
 * the %COGL_FEATURE_ID_TEXTURE_RG feature. If this format is used on
 * a driver where it is not available then %COGL_TEXTURE_ERROR_FORMAT
 * will be raised when the texture is allocated. Even if the feature
 * is not available then %COGL_PIXEL_FORMAT_RG_88 can still be used as
 * an image format as long as %COGL_TEXTURE_COMPONENTS_RG isn't used
 * as the texture's components.
 *
 * Since: 1.18
 */
COGL_EXPORT void
cogl_texture_set_components (CoglTexture *texture,
                             CoglTextureComponents components);

/**
 * cogl_texture_get_components:
 * @texture: a #CoglTexture pointer.
 *
 * Queries what components the given @texture stores internally as set
 * via cogl_texture_set_components().
 *
 * For textures created by the ‘_with_size’ constructors the default
 * is %COGL_TEXTURE_COMPONENTS_RGBA. The other constructors which take
 * a %CoglBitmap or a data pointer default to the same components as
 * the pixel format of the data.
 *
 * Since: 1.18
 */
COGL_EXPORT CoglTextureComponents
cogl_texture_get_components (CoglTexture *texture);

/**
 * cogl_texture_set_premultiplied:
 * @texture: a #CoglTexture pointer.
 * @premultiplied: Whether any internally stored red, green or blue
 *                 components are pre-multiplied by an alpha
 *                 component.
 *
 * Affects the internal storage format for this texture by specifying
 * whether red, green and blue color components should be stored as
 * pre-multiplied alpha values.
 *
 * This api affects how data is uploaded to the GPU since Cogl will
 * convert source data to have premultiplied or unpremultiplied
 * components according to this state.
 *
 * For example if you create a texture via
 * cogl_texture_2d_new_with_size() and then upload data via
 * cogl_texture_set_data() passing a source format of
 * %COGL_PIXEL_FORMAT_RGBA_8888 then Cogl will internally multiply the
 * red, green and blue components of the source data by the alpha
 * component, for each pixel so that the internally stored data has
 * pre-multiplied alpha components. If you instead upload data that
 * already has pre-multiplied components by passing
 * %COGL_PIXEL_FORMAT_RGBA_8888_PRE as the source format to
 * cogl_texture_set_data() then the data can be uploaded without being
 * converted.
 *
 * By default the @premultipled state is @TRUE.
 *
 * Since: 1.18
 */
COGL_EXPORT void
cogl_texture_set_premultiplied (CoglTexture *texture,
                                gboolean premultiplied);

/**
 * cogl_texture_get_premultiplied:
 * @texture: a #CoglTexture pointer.
 *
 * Queries the pre-multiplied alpha status for internally stored red,
 * green and blue components for the given @texture as set by
 * cogl_texture_set_premultiplied().
 *
 * By default the pre-multipled state is @TRUE.
 *
 * Return value: %TRUE if red, green and blue components are
 *               internally stored pre-multiplied by the alpha
 *               value or %FALSE if not.
 * Since: 1.18
 */
COGL_EXPORT gboolean
cogl_texture_get_premultiplied (CoglTexture *texture);

/**
 * cogl_texture_get_width:
 * @texture: a #CoglTexture pointer.
 *
 * Queries the width of a cogl texture.
 *
 * Return value: the width of the GPU side texture in pixels
 */
COGL_EXPORT unsigned int
cogl_texture_get_width (CoglTexture *texture);

/**
 * cogl_texture_get_height:
 * @texture: a #CoglTexture pointer.
 *
 * Queries the height of a cogl texture.
 *
 * Return value: the height of the GPU side texture in pixels
 */
COGL_EXPORT unsigned int
cogl_texture_get_height (CoglTexture *texture);

/**
 * cogl_texture_get_max_waste:
 * @texture: a #CoglTexture pointer.
 *
 * Queries the maximum wasted (unused) pixels in one dimension of a GPU side
 * texture.
 *
 * Return value: the maximum waste
 */
COGL_EXPORT int
cogl_texture_get_max_waste (CoglTexture *texture);

/**
 * cogl_texture_is_sliced:
 * @texture: a #CoglTexture pointer.
 *
 * Queries if a texture is sliced (stored as multiple GPU side tecture
 * objects).
 *
 * Return value: %TRUE if the texture is sliced, %FALSE if the texture
 *   is stored as a single GPU texture
 */
COGL_EXPORT gboolean
cogl_texture_is_sliced (CoglTexture *texture);

/**
 * cogl_texture_get_gl_texture:
 * @texture: a #CoglTexture pointer.
 * @out_gl_handle: (out) (allow-none): pointer to return location for the
 *   textures GL handle, or %NULL.
 * @out_gl_target: (out) (allow-none): pointer to return location for the
 *   GL target type, or %NULL.
 *
 * Queries the GL handles for a GPU side texture through its #CoglTexture.
 *
 * If the texture is spliced the data for the first sub texture will be
 * queried.
 *
 * Return value: %TRUE if the handle was successfully retrieved, %FALSE
 *   if the handle was invalid
 */
COGL_EXPORT gboolean
cogl_texture_get_gl_texture (CoglTexture *texture,
                             unsigned int *out_gl_handle,
                             unsigned int *out_gl_target);

/**
 * cogl_texture_get_data:
 * @texture: a #CoglTexture pointer.
 * @format: the #CoglPixelFormat to store the texture as.
 * @rowstride: the rowstride of @data in bytes or pass 0 to calculate
 *             from the bytes-per-pixel of @format multiplied by the
 *             @texture width.
 * @data: (array) (nullable): memory location to write the @texture's contents,
 * or %NULL to only query the data size through the return value.
 *
 * Copies the pixel data from a cogl texture to system memory.
 *
 * <note>Don't pass the value of cogl_texture_get_rowstride() as the
 * @rowstride argument, the rowstride should be the rowstride you
 * want for the destination @data buffer not the rowstride of the
 * source texture</note>
 *
 * Return value: the size of the texture data in bytes
 */
COGL_EXPORT int
cogl_texture_get_data (CoglTexture *texture,
                       CoglPixelFormat format,
                       unsigned int rowstride,
                       uint8_t *data);

/**
 * cogl_texture_set_region:
 * @texture: a #CoglTexture.
 * @src_x: upper left coordinate to use from source data.
 * @src_y: upper left coordinate to use from source data.
 * @dst_x: upper left destination horizontal coordinate.
 * @dst_y: upper left destination vertical coordinate.
 * @dst_width: width of destination region to write. (Must be less
 *   than or equal to @width)
 * @dst_height: height of destination region to write. (Must be less
 *   than or equal to @height)
 * @width: width of source data buffer.
 * @height: height of source data buffer.
 * @format: the #CoglPixelFormat used in the source buffer.
 * @rowstride: rowstride of source buffer (computed from width if none
 * specified)
 * @data: (array): the actual pixel data.
 *
 * Sets the pixels in a rectangular subregion of @texture from an in-memory
 * buffer containing pixel data.
 *
 * <note>The region set can't be larger than the source @data</note>
 *
 * Return value: %TRUE if the subregion upload was successful, and
 *   %FALSE otherwise
 */
COGL_EXPORT gboolean
cogl_texture_set_region (CoglTexture *texture,
                         int src_x,
                         int src_y,
                         int dst_x,
                         int dst_y,
                         unsigned int dst_width,
                         unsigned int dst_height,
                         int width,
                         int height,
                         CoglPixelFormat format,
                         unsigned int rowstride,
                         const uint8_t *data);

/**
 * cogl_texture_set_data:
 * @texture a #CoglTexture.
 * @format: the #CoglPixelFormat used in the source @data buffer.
 * @rowstride: rowstride of the source @data buffer (computed from
 *             the texture width and @format if it equals 0)
 * @data: (array): the source data, pointing to the first top-left pixel to set
 * @level: The mipmap level to update (Normally 0 for the largest,
 *         base texture)
 * @error: A #GError to return exceptional errors
 *
 * Sets all the pixels for a given mipmap @level by copying the pixel
 * data pointed to by the @data argument into the given @texture.
 *
 * @data should point to the first pixel to copy corresponding
 * to the top left of the mipmap @level being set.
 *
 * If @rowstride equals 0 then it will be automatically calculated
 * from the width of the mipmap level and the bytes-per-pixel for the
 * given @format.
 *
 * A mipmap @level of 0 corresponds to the largest, base image of a
 * texture and @level 1 is half the width and height of level 0. If
 * dividing any dimension of the previous level by two results in a
 * fraction then round the number down (floor()), but clamp to 1
 * something like this:
 *
 * |[
 *  next_width = MAX (1, floor (prev_width));
 * ]|
 *
 * You can determine the number of mipmap levels for a given texture
 * like this:
 *
 * |[
 *  n_levels = 1 + floor (log2 (max_dimension));
 * ]|
 *
 * Where %max_dimension is the larger of cogl_texture_get_width() and
 * cogl_texture_get_height().
 *
 * It is an error to pass a @level number >= the number of levels that
 * @texture can have according to the above calculation.
 *
 * <note>Since the storage for a #CoglTexture is allocated lazily then
 * if the given @texture has not previously been allocated then this
 * api can return %FALSE and throw an exceptional @error if there is
 * not enough memory to allocate storage for @texture.</note>
 *
 * Return value: %TRUE if the data upload was successful, and
 *               %FALSE otherwise
 */
COGL_EXPORT gboolean
cogl_texture_set_data (CoglTexture *texture,
                       CoglPixelFormat format,
                       int rowstride,
                       const uint8_t *data,
                       int level,
                       GError **error);

/**
 * cogl_texture_set_region_from_bitmap:
 * @texture: a #CoglTexture pointer
 * @src_x: upper left coordinate to use from the source bitmap.
 * @src_y: upper left coordinate to use from the source bitmap
 * @dst_x: upper left destination horizontal coordinate.
 * @dst_y: upper left destination vertical coordinate.
 * @dst_width: width of destination region to write. (Must be less
 *   than or equal to the bitmap width)
 * @dst_height: height of destination region to write. (Must be less
 *   than or equal to the bitmap height)
 * @bitmap: The source bitmap to read from
 *
 * Copies a specified source region from @bitmap to the position
 * (@src_x, @src_y) of the given destination texture @handle.
 *
 * <note>The region updated can't be larger than the source
 * bitmap</note>
 *
 * Return value: %TRUE if the subregion upload was successful, and
 *   %FALSE otherwise
 *
 * Since: 1.8
 * Stability: unstable
 */
COGL_EXPORT gboolean
cogl_texture_set_region_from_bitmap (CoglTexture *texture,
                                     int src_x,
                                     int src_y,
                                     int dst_x,
                                     int dst_y,
                                     unsigned int dst_width,
                                     unsigned int dst_height,
                                     CoglBitmap *bitmap);

/**
 * cogl_texture_allocate:
 * @texture: A #CoglTexture
 * @error: A #GError to return exceptional errors or %NULL
 *
 * Explicitly allocates the storage for the given @texture which
 * allows you to be sure that there is enough memory for the
 * texture and if not then the error can be handled gracefully.
 *
 * <note>Normally applications don't need to use this api directly
 * since the texture will be implicitly allocated when data is set on
 * the texture, or if the texture is attached to a #CoglOffscreen
 * framebuffer and rendered too.</note>
 *
 * Return value: %TRUE if the texture was successfully allocated,
 *               otherwise %FALSE and @error will be updated if it
 *               wasn't %NULL.
 */
COGL_EXPORT gboolean
cogl_texture_allocate (CoglTexture *texture,
                       GError **error);

/**
 * cogl_texture_is_get_data_supported: (skip)
 */
COGL_EXPORT gboolean
cogl_texture_is_get_data_supported (CoglTexture *texture);

G_END_DECLS

#endif /* __COGL_TEXTURE_H__ */
