/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2011,2012 Intel Corporation.
 * Copyright (C) 2010 Red Hat, Inc.
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
 *  Matthew Allum  <mallum@openedhand.com>
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include "cogl-context-private.h"
#include "cogl-texture.h"
#include "cogl-util.h"
#include "cogl-texture-2d.h"
#include "cogl-texture-2d-private.h"
#include "cogl-primitive-texture.h"
#include "cogl-texture-2d-sliced-private.h"
#include "cogl-private.h"
#include "cogl-object.h"
#include "cogl-bitmap-private.h"
#include "cogl-atlas-texture-private.h"
#include "cogl-error-private.h"
#include "cogl-texture-rectangle.h"
#include "cogl-sub-texture.h"
#include "cogl-texture-2d-gl.h"

#include "deprecated/cogl-auto-texture.h"

static CoglTexture *
_cogl_texture_new_from_bitmap (CoglBitmap *bitmap,
                               CoglTextureFlags flags,
                               CoglPixelFormat internal_format,
                               CoglBool can_convert_in_place,
                               CoglError **error);

static void
set_auto_mipmap_cb (CoglTexture *sub_texture,
                    const float *sub_texture_coords,
                    const float *meta_coords,
                    void *user_data)
{
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (sub_texture),
                                          FALSE);
}

CoglTexture *
cogl_texture_new_with_size (unsigned int width,
			    unsigned int height,
                            CoglTextureFlags flags,
			    CoglPixelFormat internal_format)
{
  CoglTexture *tex;
  CoglError *skip_error = NULL;

  _COGL_GET_CONTEXT (ctx, NULL);

  if ((_cogl_util_is_pot (width) && _cogl_util_is_pot (height)) ||
      (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT_BASIC) &&
       cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT_MIPMAP)))
    {
      /* First try creating a fast-path non-sliced texture */
      tex = COGL_TEXTURE (cogl_texture_2d_new_with_size (ctx, width, height));

      _cogl_texture_set_internal_format (tex, internal_format);

      if (!cogl_texture_allocate (tex, &skip_error))
        {
          cogl_error_free (skip_error);
          skip_error = NULL;
          cogl_object_unref (tex);
          tex = NULL;
        }
    }
  else
    tex = NULL;

  if (!tex)
    {
      /* If it fails resort to sliced textures */
      int max_waste = flags & COGL_TEXTURE_NO_SLICING ? -1 : COGL_TEXTURE_MAX_WASTE;
      tex = COGL_TEXTURE (cogl_texture_2d_sliced_new_with_size (ctx,
                                                                width,
                                                                height,
                                                                max_waste));

      _cogl_texture_set_internal_format (tex, internal_format);
    }

  /* NB: This api existed before Cogl introduced lazy allocation of
   * textures and so we maintain its original synchronous allocation
   * semantics and return NULL if allocation fails... */
  if (!cogl_texture_allocate (tex, &skip_error))
    {
      cogl_error_free (skip_error);
      cogl_object_unref (tex);
      return NULL;
    }

  if (tex &&
      flags & COGL_TEXTURE_NO_AUTO_MIPMAP)
    {
      cogl_meta_texture_foreach_in_region (COGL_META_TEXTURE (tex),
                                           0, 0, 1, 1,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           set_auto_mipmap_cb,
                                           NULL);
    }

  return tex;
}

static CoglTexture *
_cogl_texture_new_from_data (CoglContext *ctx,
                             int width,
                             int height,
                             CoglTextureFlags flags,
                             CoglPixelFormat format,
                             CoglPixelFormat internal_format,
                             int rowstride,
                             const uint8_t *data,
                             CoglError **error)
{
  CoglBitmap *bmp;
  CoglTexture *tex;

  _COGL_RETURN_VAL_IF_FAIL (format != COGL_PIXEL_FORMAT_ANY, NULL);
  _COGL_RETURN_VAL_IF_FAIL (data != NULL, NULL);

  /* Rowstride from width if not given */
  if (rowstride == 0)
    rowstride = width * _cogl_pixel_format_get_bytes_per_pixel (format);

  /* Wrap the data into a bitmap */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width, height,
                                  format,
                                  rowstride,
                                  (uint8_t *) data);

  tex = _cogl_texture_new_from_bitmap (bmp,
                                       flags,
                                       internal_format,
                                       FALSE, /* can't convert in place */
                                       error);

  cogl_object_unref (bmp);

  return tex;
}

CoglTexture *
cogl_texture_new_from_data (int width,
                            int height,
                            CoglTextureFlags flags,
                            CoglPixelFormat format,
                            CoglPixelFormat internal_format,
                            int rowstride,
                            const uint8_t *data)
{
  CoglError *ignore_error = NULL;
  CoglTexture *tex;

  _COGL_GET_CONTEXT (ctx, NULL);

  tex = _cogl_texture_new_from_data (ctx,
                                     width, height,
                                     flags,
                                     format, internal_format,
                                     rowstride,
                                     data,
                                     &ignore_error);
  if (!tex)
    cogl_error_free (ignore_error);
  return tex;
}

static CoglTexture *
_cogl_texture_new_from_bitmap (CoglBitmap *bitmap,
                               CoglTextureFlags flags,
                               CoglPixelFormat internal_format,
                               CoglBool can_convert_in_place,
                               CoglError **error)
{
  CoglContext *ctx = _cogl_bitmap_get_context (bitmap);
  CoglTexture *tex;
  CoglError *internal_error = NULL;

  if (!flags &&
      !COGL_DEBUG_ENABLED (COGL_DEBUG_DISABLE_ATLAS))
    {
      /* First try putting the texture in the atlas */
      CoglAtlasTexture *atlas_tex =
        _cogl_atlas_texture_new_from_bitmap (bitmap,
                                             can_convert_in_place);

      _cogl_texture_set_internal_format (COGL_TEXTURE (atlas_tex),
                                         internal_format);

      if (cogl_texture_allocate (COGL_TEXTURE (atlas_tex), &internal_error))
        return COGL_TEXTURE (atlas_tex);

      cogl_error_free (internal_error);
      internal_error = NULL;
      cogl_object_unref (atlas_tex);
    }

  /* If that doesn't work try a fast path 2D texture */
  if ((_cogl_util_is_pot (bitmap->width) &&
       _cogl_util_is_pot (bitmap->height)) ||
      (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT_BASIC) &&
       cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT_MIPMAP)))
    {
      tex = COGL_TEXTURE (_cogl_texture_2d_new_from_bitmap (bitmap,
                                                            can_convert_in_place));

      _cogl_texture_set_internal_format (tex, internal_format);

      if (!cogl_texture_allocate (tex, &internal_error))
        {
          cogl_error_free (internal_error);
          internal_error = NULL;
          cogl_object_unref (tex);
          tex = NULL;
        }
    }
  else
    tex = NULL;

  if (!tex)
    {
      /* Otherwise create a sliced texture */
      int max_waste = flags & COGL_TEXTURE_NO_SLICING ? -1 : COGL_TEXTURE_MAX_WASTE;
      tex = COGL_TEXTURE (_cogl_texture_2d_sliced_new_from_bitmap (bitmap,
                                                             max_waste,
                                                             can_convert_in_place));

      _cogl_texture_set_internal_format (tex, internal_format);

      if (!cogl_texture_allocate (tex, error))
        {
          cogl_object_unref (tex);
          tex = NULL;
        }
    }

  if (tex &&
      flags & COGL_TEXTURE_NO_AUTO_MIPMAP)
    {
      cogl_meta_texture_foreach_in_region (COGL_META_TEXTURE (tex),
                                           0, 0, 1, 1,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
                                           set_auto_mipmap_cb,
                                           NULL);
    }

  return tex;
}

CoglTexture *
cogl_texture_new_from_bitmap (CoglBitmap *bitmap,
                              CoglTextureFlags flags,
                              CoglPixelFormat internal_format)
{
  CoglError *ignore_error = NULL;
  CoglTexture *tex =
    _cogl_texture_new_from_bitmap (bitmap,
                                   flags,
                                   internal_format,
                                   FALSE, /* can't convert in-place */
                                   &ignore_error);
  if (!tex)
    cogl_error_free (ignore_error);
  return tex;
}

CoglTexture *
cogl_texture_new_from_file (const char        *filename,
                            CoglTextureFlags   flags,
                            CoglPixelFormat    internal_format,
                            CoglError           **error)
{
  CoglBitmap *bmp;
  CoglTexture *texture = NULL;

  _COGL_GET_CONTEXT (ctx, NULL);

  _COGL_RETURN_VAL_IF_FAIL (error == NULL || *error == NULL, NULL);

  bmp = cogl_bitmap_new_from_file (filename, error);
  if (bmp == NULL)
    return NULL;

  texture = _cogl_texture_new_from_bitmap (bmp, flags,
                                           internal_format,
                                           TRUE, /* can convert in-place */
                                           error);

  cogl_object_unref (bmp);

  return texture;
}

CoglTexture *
cogl_texture_new_from_foreign (GLuint           gl_handle,
			       GLenum           gl_target,
			       GLuint           width,
			       GLuint           height,
			       GLuint           x_pot_waste,
			       GLuint           y_pot_waste,
			       CoglPixelFormat  format)
{
  _COGL_GET_CONTEXT (ctx, NULL);

#ifdef HAVE_COGL_GL
  if (gl_target == GL_TEXTURE_RECTANGLE_ARB)
    {
      CoglTextureRectangle *texture_rectangle;
      CoglSubTexture *sub_texture;

      if (x_pot_waste != 0 || y_pot_waste != 0)
        {
          /* It shouldn't be necessary to have waste in this case since
           * the texture isn't limited to power of two sizes. */
          g_warning ("You can't create a foreign GL_TEXTURE_RECTANGLE cogl "
                     "texture with waste\n");
          return NULL;
        }

      texture_rectangle = cogl_texture_rectangle_new_from_foreign (ctx,
                                                                   gl_handle,
                                                                   width,
                                                                   height,
                                                                   format);
      _cogl_texture_set_internal_format (COGL_TEXTURE (texture_rectangle),
                                         format);

      /* CoglTextureRectangle textures work with non-normalized
       * coordinates, but the semantics for this function that people
       * depend on are that all returned texture works with normalized
       * coordinates so we wrap with a CoglSubTexture... */
      sub_texture = cogl_sub_texture_new (ctx,
                                          COGL_TEXTURE (texture_rectangle),
                                          0, 0, width, height);
      return COGL_TEXTURE (sub_texture);
    }
#endif

  if (x_pot_waste != 0 || y_pot_waste != 0)
    {
      CoglTexture *tex =
        COGL_TEXTURE (_cogl_texture_2d_sliced_new_from_foreign (ctx,
                                                                gl_handle,
                                                                gl_target,
                                                                width,
                                                                height,
                                                                x_pot_waste,
                                                                y_pot_waste,
                                                                format));
      _cogl_texture_set_internal_format (tex, format);

      cogl_texture_allocate (tex, NULL);
      return tex;
    }
  else
    {
      CoglTexture *tex =
        COGL_TEXTURE (cogl_texture_2d_gl_new_from_foreign (ctx,
                                                           gl_handle,
                                                           width,
                                                           height,
                                                           format));
      _cogl_texture_set_internal_format (tex, format);

      cogl_texture_allocate (tex, NULL);
      return tex;
    }
}

CoglTexture *
cogl_texture_new_from_sub_texture (CoglTexture *full_texture,
                                   int sub_x,
                                   int sub_y,
                                   int sub_width,
                                   int sub_height)
{
  _COGL_GET_CONTEXT (ctx, NULL);
  return COGL_TEXTURE (cogl_sub_texture_new (ctx,
                                             full_texture, sub_x, sub_y,
                                             sub_width, sub_height));
}
