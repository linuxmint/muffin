/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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
 */

#include "cogl-config.h"

#include <strings.h>

#include "cogl-context-private.h"
#include "cogl-util.h"
#include "driver/gl/cogl-util-gl-private.h"
#include "driver/gl/cogl-texture-gl-private.h"
#include "driver/gl/cogl-pipeline-opengl-private.h"

static inline int
calculate_alignment (int rowstride)
{
  int alignment = 1 << (ffs (rowstride) - 1);

  return MIN (alignment, 8);
}

void
_cogl_texture_gl_prep_alignment_for_pixels_upload (CoglContext *ctx,
                                                   int pixels_rowstride)
{
  GE( ctx, glPixelStorei (GL_UNPACK_ALIGNMENT,
                          calculate_alignment (pixels_rowstride)) );
}

void
_cogl_texture_gl_prep_alignment_for_pixels_download (CoglContext *ctx,
                                                     int bpp,
                                                     int width,
                                                     int rowstride)
{
  int alignment;

  /* If no padding is needed then we can always use an alignment of 1.
   * We want to do this even though it is equivalent to the alignment
   * of the rowstride because the Intel driver in Mesa currently has
   * an optimisation when reading data into a PBO that only works if
   * the alignment is exactly 1.
   *
   * https://bugs.freedesktop.org/show_bug.cgi?id=46632
   */

  if (rowstride == bpp * width)
    alignment = 1;
  else
    alignment = calculate_alignment (rowstride);

  GE( ctx, glPixelStorei (GL_PACK_ALIGNMENT, alignment) );
}

void
_cogl_texture_gl_flush_legacy_texobj_wrap_modes (CoglTexture *texture,
                                                 unsigned int wrap_mode_s,
                                                 unsigned int wrap_mode_t)
{
  texture->vtable->gl_flush_legacy_texobj_wrap_modes (texture,
                                                      wrap_mode_s,
                                                      wrap_mode_t);
}

void
_cogl_texture_gl_flush_legacy_texobj_filters (CoglTexture *texture,
                                              unsigned int min_filter,
                                              unsigned int mag_filter)
{
  texture->vtable->gl_flush_legacy_texobj_filters (texture,
                                                   min_filter, mag_filter);
}

/* GL and GLES3 have this by default, but GLES2 does not except via extension.
 * So really it's probably always available. Even if we used it and it wasn't
 * available in some driver then there are no adverse consequences to the
 * command simply being ignored...
 */
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

void
cogl_texture_gl_set_max_level (CoglTexture *texture,
                               int max_level)
{
  CoglContext *ctx = texture->context;

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_TEXTURE_MAX_LEVEL))
    {
      GLuint gl_handle;
      GLenum gl_target;

      cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

      texture->max_level_set = max_level;

      _cogl_bind_gl_texture_transient (gl_target,
                                       gl_handle);

      GE( ctx, glTexParameteri (gl_target,
                                GL_TEXTURE_MAX_LEVEL, texture->max_level_set));
    }
}

void
_cogl_texture_gl_generate_mipmaps (CoglTexture *texture)
{
  CoglContext *ctx = texture->context;
  int n_levels = _cogl_texture_get_n_levels (texture);
  GLuint gl_handle;
  GLenum gl_target;

  if (texture->max_level_set != n_levels - 1)
    cogl_texture_gl_set_max_level (texture, n_levels - 1);

  cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

  _cogl_bind_gl_texture_transient (gl_target,
                                   gl_handle);
  GE( ctx, glGenerateMipmap (gl_target) );
}

GLenum
_cogl_texture_gl_get_format (CoglTexture *texture)
{
  return texture->vtable->get_gl_format (texture);
}
