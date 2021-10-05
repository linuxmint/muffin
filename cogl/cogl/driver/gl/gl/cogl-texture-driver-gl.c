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

#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
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
      /* In case automatic mipmap generation gets disabled for this
       * texture but a minification filter depending on mipmap
       * interpolation is selected then we initialize the max mipmap
       * level to 0 so OpenGL will consider the texture storage to be
       * "complete".
       */
#ifdef HAVE_COGL_GL
      if (_cogl_has_private_feature
          (ctx, COGL_PRIVATE_FEATURE_TEXTURE_MAX_LEVEL))
        GE( ctx, glTexParameteri (gl_target, GL_TEXTURE_MAX_LEVEL, 0));
#endif

      /* GL_TEXTURE_MAG_FILTER defaults to GL_LINEAR, no need to set it */
      GE( ctx, glTexParameteri (gl_target,
                                GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR) );
      break;

    case GL_TEXTURE_RECTANGLE_ARB:
      /* Texture rectangles already default to GL_LINEAR so nothing
         needs to be done */
      break;

    default:
      g_assert_not_reached();
    }

  /* If the driver doesn't support alpha textures directly then we'll
   * fake them by setting the swizzle parameters */
  if (internal_format == COGL_PIXEL_FORMAT_A_8 &&
      !_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_ALPHA_TEXTURES) &&
      _cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE))
    {
      static const GLint red_swizzle[] = { GL_ZERO, GL_ZERO, GL_ZERO, GL_RED };

      GE( ctx, glTexParameteriv (gl_target,
                                 GL_TEXTURE_SWIZZLE_RGBA,
                                 red_swizzle) );
    }

  return tex;
}

/* OpenGL - unlike GLES - can upload a sub region of pixel data from a larger
 * source buffer */
static void
prep_gl_for_pixels_upload_full (CoglContext *ctx,
                                int pixels_rowstride,
                                int image_height,
                                int pixels_src_x,
                                int pixels_src_y,
                                int pixels_bpp)
{
  GE( ctx, glPixelStorei (GL_UNPACK_ROW_LENGTH,
                          pixels_rowstride / pixels_bpp) );

  GE( ctx, glPixelStorei (GL_UNPACK_SKIP_PIXELS, pixels_src_x) );
  GE( ctx, glPixelStorei (GL_UNPACK_SKIP_ROWS, pixels_src_y) );

  _cogl_texture_gl_prep_alignment_for_pixels_upload (ctx, pixels_rowstride);
}

/* OpenGL - unlike GLES - can download pixel data into a sub region of
 * a larger destination buffer */
static void
prep_gl_for_pixels_download_full (CoglContext *ctx,
                                  int image_width,
                                  int pixels_rowstride,
                                  int image_height,
                                  int pixels_src_x,
                                  int pixels_src_y,
                                  int pixels_bpp)
{
  GE( ctx, glPixelStorei (GL_PACK_ROW_LENGTH, pixels_rowstride / pixels_bpp) );

  GE( ctx, glPixelStorei (GL_PACK_SKIP_PIXELS, pixels_src_x) );
  GE( ctx, glPixelStorei (GL_PACK_SKIP_ROWS, pixels_src_y) );

  _cogl_texture_gl_prep_alignment_for_pixels_download (ctx,
                                                       pixels_bpp,
                                                       image_width,
                                                       pixels_rowstride);
}

static void
_cogl_texture_driver_prep_gl_for_pixels_download (CoglContext *ctx,
                                                      int image_width,
                                                  int pixels_rowstride,
                                                  int pixels_bpp)
{
  prep_gl_for_pixels_download_full (ctx,
                                    pixels_rowstride,
                                    image_width,
                                    0 /* image height */,
                                    0, 0, /* pixels_src_x/y */
                                    pixels_bpp);
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
                                             CoglBitmap  *source_bmp,
				             GLuint source_gl_format,
				             GLuint source_gl_type,
                                             GError **error)
{
  GLenum gl_target;
  GLuint gl_handle;
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp;
  gboolean status = TRUE;
  GError *internal_error = NULL;
  int level_width;
  int level_height;

  g_return_val_if_fail (source_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (source_format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (source_format, 0);
  cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

  data = _cogl_bitmap_gl_bind (source_bmp, COGL_BUFFER_ACCESS_READ, 0, &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successfull so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      return FALSE;
    }

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  0,
                                  src_x,
                                  src_y,
                                  bpp);

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
      /* GL gets upset if you use glTexSubImage2D to initialize the
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

  _cogl_bitmap_gl_unbind (source_bmp);

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
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp;
  gboolean status = TRUE;
  GError *internal_error = NULL;

  g_return_val_if_fail (source_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (source_format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (source_format, 0);

  data = _cogl_bitmap_gl_bind (source_bmp,
                               COGL_BUFFER_ACCESS_READ,
                               0, /* hints */
                               &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successful so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      return FALSE;
    }

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  0, 0, 0, bpp);

  _cogl_bind_gl_texture_transient (gl_target, gl_handle);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glTexImage2D (gl_target, 0,
                     internal_gl_format,
                     cogl_bitmap_get_width (source_bmp),
                     cogl_bitmap_get_height (source_bmp),
                     0,
                     source_gl_format,
                     source_gl_type,
                     data);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (source_bmp);

  return status;
}

static gboolean
_cogl_texture_driver_gl_get_tex_image (CoglContext *ctx,
                                       GLenum gl_target,
                                       GLenum dest_gl_format,
                                       GLenum dest_gl_type,
                                       uint8_t *dest)
{
  GE (ctx, glGetTexImage (gl_target,
                          0, /* level */
                          dest_gl_format,
                          dest_gl_type,
                          (GLvoid *)dest));
  return TRUE;
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
  GLenum proxy_target;
  GLint new_width = 0;

  if (gl_target == GL_TEXTURE_2D)
    proxy_target = GL_PROXY_TEXTURE_2D;
#ifdef HAVE_COGL_GL
  else if (gl_target == GL_TEXTURE_RECTANGLE_ARB)
    proxy_target = GL_PROXY_TEXTURE_RECTANGLE_ARB;
#endif
  else
    /* Unknown target, assume it's not supported */
    return FALSE;

  /* Proxy texture allows for a quick check for supported size */
  GE( ctx, glTexImage2D (proxy_target, 0, gl_intformat,
                         width, height, 0 /* border */,
                         gl_format, gl_type, NULL) );

  GE( ctx, glGetTexLevelParameteriv (proxy_target, 0,
                                     GL_TEXTURE_WIDTH, &new_width) );

  return new_width != 0;
}

static CoglPixelFormat
_cogl_texture_driver_find_best_gl_get_data_format
                                            (CoglContext *context,
                                             CoglPixelFormat format,
                                             GLenum *closest_gl_format,
                                             GLenum *closest_gl_type)
{
  return context->driver_vtable->pixel_format_to_gl (context,
                                                     format,
                                                     NULL, /* don't need */
                                                     closest_gl_format,
                                                     closest_gl_type);
}

const CoglTextureDriver
_cogl_texture_driver_gl =
  {
    _cogl_texture_driver_gen,
    _cogl_texture_driver_upload_subregion_to_gl,
    _cogl_texture_driver_upload_to_gl,
    _cogl_texture_driver_prep_gl_for_pixels_download,
    _cogl_texture_driver_gl_get_tex_image,
    _cogl_texture_driver_size_supported,
    _cogl_texture_driver_find_best_gl_get_data_format
  };
