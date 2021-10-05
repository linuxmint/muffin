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
 *
 * Authors:
 *  Matthew Allum  <mallum@openedhand.com>
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include "cogl-private.h"
#include "cogl-util.h"
#include "cogl-bitmap.h"
#include "cogl-bitmap-private.h"
#include "cogl-texture-private.h"
#include "cogl-pipeline.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "driver/gl/cogl-pipeline-opengl-private.h"
#include "driver/gl/cogl-util-gl-private.h"
#include "driver/gl/cogl-texture-gl-private.h"
#include "driver/gl/cogl-bitmap-gl-private.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_MAX_3D_TEXTURE_SIZE_OES
#define GL_MAX_3D_TEXTURE_SIZE_OES 0x8073
#endif

/* This extension isn't available for GLES 1.1 so these won't be
   defined */
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif

static GLuint
_cogl_texture_driver_gen (CoglContext *ctx,
                          GLenum gl_target,
                          CoglPixelFormat internal_format)
{
  GLuint tex;

  GE (ctx, glGenTextures (1, &tex));

  _cogl_bind_gl_texture_transient (gl_target, tex);

  switch (gl_target)
    {
    case GL_TEXTURE_2D:
    case GL_TEXTURE_3D:
      /* GL_TEXTURE_MAG_FILTER defaults to GL_LINEAR, no need to set it */
      GE( ctx, glTexParameteri (gl_target,
                                GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR) );
      break;

    default:
      g_assert_not_reached();
    }

  return tex;
}

static void
prep_gl_for_pixels_upload_full (CoglContext *ctx,
                                int pixels_rowstride,
                                int pixels_src_x,
                                int pixels_src_y,
                                int pixels_bpp)
{
  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_UNPACK_SUBIMAGE))
    {
      GE( ctx, glPixelStorei (GL_UNPACK_ROW_LENGTH,
                              pixels_rowstride / pixels_bpp) );

      GE( ctx, glPixelStorei (GL_UNPACK_SKIP_PIXELS, pixels_src_x) );
      GE( ctx, glPixelStorei (GL_UNPACK_SKIP_ROWS, pixels_src_y) );
    }
  else
    {
      g_assert (pixels_src_x == 0);
      g_assert (pixels_src_y == 0);
    }

  _cogl_texture_gl_prep_alignment_for_pixels_upload (ctx, pixels_rowstride);
}

static void
_cogl_texture_driver_prep_gl_for_pixels_upload (CoglContext *ctx,
                                                int pixels_rowstride,
                                                int pixels_bpp)
{
  prep_gl_for_pixels_upload_full (ctx,
                                  pixels_rowstride,
                                  0, 0, /* src_x/y */
                                  pixels_bpp);
}

static void
_cogl_texture_driver_prep_gl_for_pixels_download (CoglContext *ctx,
                                                  int pixels_rowstride,
                                                  int image_width,
                                                  int pixels_bpp)
{
  _cogl_texture_gl_prep_alignment_for_pixels_download (ctx,
                                                       pixels_bpp,
                                                       image_width,
                                                       pixels_rowstride);
}

static CoglBitmap *
prepare_bitmap_alignment_for_upload (CoglContext *ctx,
                                     CoglBitmap *src_bmp,
                                     GError **error)
{
  CoglPixelFormat format = cogl_bitmap_get_format (src_bmp);
  int bpp;
  int src_rowstride = cogl_bitmap_get_rowstride (src_bmp);
  int width = cogl_bitmap_get_width (src_bmp);
  int alignment = 1;

  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_UNPACK_SUBIMAGE) ||
      src_rowstride == 0)
    return cogl_object_ref (src_bmp);

  /* Work out the alignment of the source rowstride */
  alignment = 1 << (ffs (src_rowstride) - 1);
  alignment = MIN (alignment, 8);

  /* If the aligned data equals the rowstride then we can upload from
     the bitmap directly using GL_UNPACK_ALIGNMENT */
  if (((width * bpp + alignment - 1) & ~(alignment - 1)) == src_rowstride)
    return cogl_object_ref (src_bmp);
  /* Otherwise we need to copy the bitmap to pack the alignment
     because GLES has no GL_ROW_LENGTH */
  else
    return _cogl_bitmap_copy (src_bmp, error);
}

static gboolean
_cogl_texture_driver_upload_subregion_to_gl (CoglContext *ctx,
                                             CoglTexture *texture,
                                             int src_x,
                                             int src_y,
                                             int dst_x,
                                             int dst_y,
                                             int width,
                                             int height,
                                             int level,
                                             CoglBitmap *source_bmp,
				             GLuint source_gl_format,
				             GLuint source_gl_type,
                                             GError **error)
{
  GLenum gl_target;
  GLuint gl_handle;
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp;
  CoglBitmap *slice_bmp;
  int rowstride;
  gboolean status = TRUE;
  GError *internal_error = NULL;
  int level_width;
  int level_height;

  g_return_val_if_fail (source_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (source_format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (source_format, 0);

  cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

  /* If we have the GL_EXT_unpack_subimage extension then we can
     upload from subregions directly. Otherwise we may need to copy
     the bitmap */
  if (!_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_UNPACK_SUBIMAGE) &&
      (src_x != 0 || src_y != 0 ||
       width != cogl_bitmap_get_width (source_bmp) ||
       height != cogl_bitmap_get_height (source_bmp)))
    {
      slice_bmp =
        _cogl_bitmap_new_with_malloc_buffer (ctx,
                                             width, height,
                                             source_format,
                                             error);
      if (!slice_bmp)
        return FALSE;

      if (!_cogl_bitmap_copy_subregion (source_bmp,
                                        slice_bmp,
                                        src_x, src_y,
                                        0, 0, /* dst_x/y */
                                        width, height,
                                        error))
        {
          cogl_object_unref (slice_bmp);
          return FALSE;
        }

      src_x = src_y = 0;
    }
  else
    {
      slice_bmp = prepare_bitmap_alignment_for_upload (ctx, source_bmp, error);
      if (!slice_bmp)
        return FALSE;
    }

  rowstride = cogl_bitmap_get_rowstride (slice_bmp);

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx, rowstride, src_x, src_y, bpp);

  data = _cogl_bitmap_gl_bind (slice_bmp, COGL_BUFFER_ACCESS_READ, 0, &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successfull so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      cogl_object_unref (slice_bmp);
      return FALSE;
    }

  _cogl_bind_gl_texture_transient (gl_target, gl_handle);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  _cogl_texture_get_level_size (texture,
                                level,
                                &level_width,
                                &level_height,
                                NULL);

  if (level_width == width && level_height == height)
    {
      /* GL gets upset if you use glTexSubImage2D to define the
       * contents of a mipmap level so we make sure to use
       * glTexImage2D if we are uploading a full mipmap level.
       */
      ctx->glTexImage2D (gl_target,
                         level,
                         _cogl_texture_gl_get_format (texture),
                         width,
                         height,
                         0,
                         source_gl_format,
                         source_gl_type,
                         data);
    }
  else
    {
      /* GL gets upset if you use glTexSubImage2D to initialize the
       * contents of a mipmap level so if this is the first time
       * we've seen a request to upload to this level we call
       * glTexImage2D first to assert that the storage for this
       * level exists.
       */
      if (texture->max_level_set < level)
        {
          ctx->glTexImage2D (gl_target,
                             level,
                             _cogl_texture_gl_get_format (texture),
                             level_width,
                             level_height,
                             0,
                             source_gl_format,
                             source_gl_type,
                             NULL);
        }

      ctx->glTexSubImage2D (gl_target,
                            level,
                            dst_x, dst_y,
                            width, height,
                            source_gl_format,
                            source_gl_type,
                            data);
    }

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (slice_bmp);

  cogl_object_unref (slice_bmp);

  return status;
}

static gboolean
_cogl_texture_driver_upload_to_gl (CoglContext *ctx,
                                   GLenum gl_target,
                                   GLuint gl_handle,
                                   CoglBitmap *source_bmp,
                                   GLint internal_gl_format,
                                   GLuint source_gl_format,
                                   GLuint source_gl_type,
                                   GError **error)
{
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp;
  int rowstride;
  int bmp_width = cogl_bitmap_get_width (source_bmp);
  int bmp_height = cogl_bitmap_get_height (source_bmp);
  CoglBitmap *bmp;
  uint8_t *data;
  GError *internal_error = NULL;
  gboolean status = TRUE;

  g_return_val_if_fail (source_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (source_format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (source_format, 0);

  bmp = prepare_bitmap_alignment_for_upload (ctx, source_bmp, error);
  if (!bmp)
    return FALSE;

  rowstride = cogl_bitmap_get_rowstride (bmp);

  /* Setup gl alignment to match rowstride and top-left corner */
  _cogl_texture_driver_prep_gl_for_pixels_upload (ctx, rowstride, bpp);

  _cogl_bind_gl_texture_transient (gl_target, gl_handle);

  data = _cogl_bitmap_gl_bind (bmp,
                               COGL_BUFFER_ACCESS_READ,
                               0, /* hints */
                               &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successful so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      cogl_object_unref (bmp);
      g_propagate_error (error, internal_error);
      return FALSE;
    }

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glTexImage2D (gl_target, 0,
                     internal_gl_format,
                     bmp_width, bmp_height,
                     0,
                     source_gl_format,
                     source_gl_type,
                     data);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (bmp);

  cogl_object_unref (bmp);

  return status;
}

/* NB: GLES doesn't support glGetTexImage2D, so cogl-texture will instead
 * fallback to a generic render + readpixels approach to downloading
 * texture data. (See _cogl_texture_draw_and_read() ) */
static gboolean
_cogl_texture_driver_gl_get_tex_image (CoglContext *ctx,
                                       GLenum gl_target,
                                       GLenum dest_gl_format,
                                       GLenum dest_gl_type,
                                       uint8_t *dest)
{
  return FALSE;
}

static gboolean
_cogl_texture_driver_size_supported (CoglContext *ctx,
                                     GLenum gl_target,
                                     GLenum gl_intformat,
                                     GLenum gl_format,
                                     GLenum gl_type,
                                     int width,
                                     int height)
{
  GLint max_size;

  /* GLES doesn't support a proxy texture target so let's at least
     check whether the size is greater than GL_MAX_TEXTURE_SIZE */
  GE( ctx, glGetIntegerv (GL_MAX_TEXTURE_SIZE, &max_size) );

  return width <= max_size && height <= max_size;
}

static CoglPixelFormat
_cogl_texture_driver_find_best_gl_get_data_format
                                            (CoglContext *context,
                                             CoglPixelFormat format,
                                             GLenum *closest_gl_format,
                                             GLenum *closest_gl_type)
{
  /* Find closest format that's supported by GL
     (Can't use _cogl_pixel_format_to_gl since available formats
      when reading pixels on GLES are severely limited) */
  *closest_gl_format = GL_RGBA;
  *closest_gl_type = GL_UNSIGNED_BYTE;
  return COGL_PIXEL_FORMAT_RGBA_8888;
}

const CoglTextureDriver
_cogl_texture_driver_gles =
  {
    _cogl_texture_driver_gen,
    _cogl_texture_driver_upload_subregion_to_gl,
    _cogl_texture_driver_upload_to_gl,
    _cogl_texture_driver_prep_gl_for_pixels_download,
    _cogl_texture_driver_gl_get_tex_image,
    _cogl_texture_driver_size_supported,
    _cogl_texture_driver_find_best_gl_get_data_format
  };
