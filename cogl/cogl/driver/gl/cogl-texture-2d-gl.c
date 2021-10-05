/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2011,2012 Intel Corporation.
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
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include <string.h>

#include "cogl-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-private.h"
#include "driver/gl/cogl-texture-2d-gl-private.h"
#include "driver/gl/cogl-texture-gl-private.h"
#include "driver/gl/cogl-pipeline-opengl-private.h"
#include "driver/gl/cogl-util-gl-private.h"

#if defined (COGL_HAS_EGL_SUPPORT)

/* We need this define from GLES2, but can't include the header
   as its type definitions may conflict with the GL ones
 */
#ifndef GL_OES_EGL_image_external
#define GL_OES_EGL_image_external 1
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#define GL_TEXTURE_BINDING_EXTERNAL_OES   0x8D67
#define GL_REQUIRED_TEXTURE_IMAGE_UNITS_OES 0x8D68
#define GL_SAMPLER_EXTERNAL_OES           0x8D66
#endif /* GL_OES_EGL_image_external */

#endif /* defined (COGL_HAS_EGL_SUPPORT) */

void
_cogl_texture_2d_gl_free (CoglTexture2D *tex_2d)
{
  if (tex_2d->gl_texture)
    _cogl_delete_gl_texture (tex_2d->gl_texture);

#if defined (COGL_HAS_EGL_SUPPORT)
  g_clear_pointer (&tex_2d->egl_image_external.user_data,
                   tex_2d->egl_image_external.destroy);
#endif
}

gboolean
_cogl_texture_2d_gl_can_create (CoglContext *ctx,
                                int width,
                                int height,
                                CoglPixelFormat internal_format)
{
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  /* We only support single plane formats for now */
  if (cogl_pixel_format_get_n_planes (internal_format) != 1)
    return FALSE;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          &gl_format,
                                          &gl_type);

  /* Check that the driver can create a texture with that size */
  if (!ctx->texture_driver->size_supported (ctx,
                                            GL_TEXTURE_2D,
                                            gl_intformat,
                                            gl_format,
                                            gl_type,
                                            width,
                                            height))
    return FALSE;

  return TRUE;
}

void
_cogl_texture_2d_gl_init (CoglTexture2D *tex_2d)
{
  tex_2d->gl_texture = 0;

  /* We default to GL_LINEAR for both filters */
  tex_2d->gl_legacy_texobj_min_filter = GL_LINEAR;
  tex_2d->gl_legacy_texobj_mag_filter = GL_LINEAR;

  /* Wrap mode not yet set */
  tex_2d->gl_legacy_texobj_wrap_mode_s = GL_FALSE;
  tex_2d->gl_legacy_texobj_wrap_mode_t = GL_FALSE;

  tex_2d->egl_image_external.user_data = NULL;
  tex_2d->egl_image_external.destroy = NULL;
}

static gboolean
allocate_with_size (CoglTexture2D *tex_2d,
                    CoglTextureLoader *loader,
                    GError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglPixelFormat internal_format;
  int width = loader->src.sized.width;
  int height = loader->src.sized.height;
  CoglContext *ctx = tex->context;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;
  GLenum gl_texture;

  internal_format =
    _cogl_texture_determine_internal_format (tex, COGL_PIXEL_FORMAT_ANY);

  if (!_cogl_texture_2d_gl_can_create (ctx,
                                       width,
                                       height,
                                       internal_format))
    {
      g_set_error_literal (error, COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_SIZE,
                           "Failed to create texture 2d due to size/format"
                           " constraints");
      return FALSE;
    }

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          &gl_format,
                                          &gl_type);

  gl_texture = ctx->texture_driver->gen (ctx, GL_TEXTURE_2D, internal_format);

  tex_2d->gl_internal_format = gl_intformat;

  _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                   gl_texture);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glTexImage2D (GL_TEXTURE_2D, 0, gl_intformat,
                     width, height, 0, gl_format, gl_type, NULL);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    {
      GE( ctx, glDeleteTextures (1, &gl_texture) );
      return FALSE;
    }

  tex_2d->gl_texture = gl_texture;
  tex_2d->gl_internal_format = gl_intformat;

  tex_2d->internal_format = internal_format;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

static gboolean
allocate_from_bitmap (CoglTexture2D *tex_2d,
                      CoglTextureLoader *loader,
                      GError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglBitmap *bmp = loader->src.bitmap.bitmap;
  CoglContext *ctx = _cogl_bitmap_get_context (bmp);
  CoglPixelFormat internal_format;
  int width = cogl_bitmap_get_width (bmp);
  int height = cogl_bitmap_get_height (bmp);
  gboolean can_convert_in_place = loader->src.bitmap.can_convert_in_place;
  CoglBitmap *upload_bmp;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  internal_format =
    _cogl_texture_determine_internal_format (tex, cogl_bitmap_get_format (bmp));

  if (!_cogl_texture_2d_gl_can_create (ctx,
                                       width,
                                       height,
                                       internal_format))
    {
      g_set_error_literal (error, COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_SIZE,
                           "Failed to create texture 2d due to size/format"
                           " constraints");
      return FALSE;
    }

  upload_bmp = _cogl_bitmap_convert_for_upload (bmp,
                                                internal_format,
                                                can_convert_in_place,
                                                error);
  if (upload_bmp == NULL)
    return FALSE;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          cogl_bitmap_get_format (upload_bmp),
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);
  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          NULL,
                                          NULL);

  tex_2d->gl_texture =
    ctx->texture_driver->gen (ctx, GL_TEXTURE_2D, internal_format);
  if (!ctx->texture_driver->upload_to_gl (ctx,
                                          GL_TEXTURE_2D,
                                          tex_2d->gl_texture,
                                          upload_bmp,
                                          gl_intformat,
                                          gl_format,
                                          gl_type,
                                          error))
    {
      cogl_object_unref (upload_bmp);
      return FALSE;
    }

  tex_2d->gl_internal_format = gl_intformat;

  cogl_object_unref (upload_bmp);

  tex_2d->internal_format = internal_format;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

#if defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base)
static gboolean
allocate_from_egl_image (CoglTexture2D *tex_2d,
                         CoglTextureLoader *loader,
                         GError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglPixelFormat internal_format = loader->src.egl_image.format;

  tex_2d->gl_texture =
    ctx->texture_driver->gen (ctx, GL_TEXTURE_2D, internal_format);
  _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                   tex_2d->gl_texture);
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glEGLImageTargetTexture2D (GL_TEXTURE_2D, loader->src.egl_image.image);
  if (_cogl_gl_util_get_error (ctx) != GL_NO_ERROR)
    {
      g_set_error_literal (error,
                           COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_BAD_PARAMETER,
                           "Could not create a CoglTexture2D from a given "
                           "EGLImage");
      GE( ctx, glDeleteTextures (1, &tex_2d->gl_texture) );
      return FALSE;
    }

  tex_2d->internal_format = internal_format;
  tex_2d->is_get_data_supported =
    !(loader->src.egl_image.flags & COGL_EGL_IMAGE_FLAG_NO_GET_DATA);

  _cogl_texture_set_allocated (tex,
                               internal_format,
                               loader->src.egl_image.width,
                               loader->src.egl_image.height);

  return TRUE;
}
#endif

#if defined (COGL_HAS_EGL_SUPPORT)
static gboolean
allocate_custom_egl_image_external (CoglTexture2D *tex_2d,
                                    CoglTextureLoader *loader,
                                    GError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglPixelFormat external_format;
  CoglPixelFormat internal_format;

  external_format = loader->src.egl_image_external.format;
  internal_format = _cogl_texture_determine_internal_format (tex,
                                                             external_format);

  _cogl_gl_util_clear_gl_errors (ctx);

  GE (ctx, glActiveTexture (GL_TEXTURE0));
  GE (ctx, glGenTextures (1, &tex_2d->gl_texture));

  GE (ctx, glBindTexture (GL_TEXTURE_EXTERNAL_OES,
                          tex_2d->gl_texture));

  if (_cogl_gl_util_get_error (ctx) != GL_NO_ERROR)
    {
      g_set_error_literal (error,
                           COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_BAD_PARAMETER,
                           "Could not create a CoglTexture2D from a given "
                           "EGLImage");
      GE( ctx, glDeleteTextures (1, &tex_2d->gl_texture) );
      return FALSE;
    }

  GE (ctx, glTexParameteri(GL_TEXTURE_EXTERNAL_OES,
                           GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  GE (ctx, glTexParameteri(GL_TEXTURE_EXTERNAL_OES,
                           GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

  if (!loader->src.egl_image_external.alloc (tex_2d,
                                             tex_2d->egl_image_external.user_data,
                                             error))
    {
      GE (ctx, glBindTexture (GL_TEXTURE_EXTERNAL_OES, 0));
      GE (ctx, glDeleteTextures (1, &tex_2d->gl_texture));
      return FALSE;
    }

  GE (ctx, glBindTexture (GL_TEXTURE_EXTERNAL_OES, 0));

  tex_2d->internal_format = internal_format;
  tex_2d->gl_target = GL_TEXTURE_EXTERNAL_OES;
  tex_2d->is_get_data_supported = FALSE;

  return TRUE;
}

CoglTexture2D *
cogl_texture_2d_new_from_egl_image_external (CoglContext *ctx,
                                             int width,
                                             int height,
                                             CoglTexture2DEGLImageExternalAlloc alloc,
                                             gpointer user_data,
                                             GDestroyNotify destroy,
                                             GError **error)
{
  CoglTextureLoader *loader;
  CoglTexture2D *tex_2d;
  CoglPixelFormat internal_format = COGL_PIXEL_FORMAT_ANY;

  g_return_val_if_fail (_cogl_context_get_winsys (ctx)->constraints &
                        COGL_RENDERER_CONSTRAINT_USES_EGL,
                        NULL);

  g_return_val_if_fail (cogl_has_feature (ctx,
                                          COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL),
                        NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE_EXTERNAL;
  loader->src.egl_image_external.width = width;
  loader->src.egl_image_external.height = height;
  loader->src.egl_image_external.alloc = alloc;
  loader->src.egl_image_external.format = internal_format;

  tex_2d = _cogl_texture_2d_create_base (ctx, width, height,
                                         internal_format, loader);


  tex_2d->egl_image_external.user_data = user_data;
  tex_2d->egl_image_external.destroy = destroy;

  return tex_2d;
}
#endif /* defined (COGL_HAS_EGL_SUPPORT) */

gboolean
_cogl_texture_2d_gl_allocate (CoglTexture *tex,
                              GError **error)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglTextureLoader *loader = tex->loader;

  g_return_val_if_fail (loader, FALSE);

  switch (loader->src_type)
    {
    case COGL_TEXTURE_SOURCE_TYPE_SIZED:
      return allocate_with_size (tex_2d, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_BITMAP:
      return allocate_from_bitmap (tex_2d, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE:
#if defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base)
      return allocate_from_egl_image (tex_2d, loader, error);
#else
      g_return_val_if_reached (FALSE);
#endif
    case COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE_EXTERNAL:
#if defined (COGL_HAS_EGL_SUPPORT)
      return allocate_custom_egl_image_external (tex_2d, loader, error);
#else
      g_return_val_if_reached (FALSE);
#endif
    }

  g_return_val_if_reached (FALSE);
}

void
_cogl_texture_2d_gl_flush_legacy_texobj_filters (CoglTexture *tex,
                                                 GLenum min_filter,
                                                 GLenum mag_filter)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglContext *ctx = tex->context;

  if (min_filter == tex_2d->gl_legacy_texobj_min_filter
      && mag_filter == tex_2d->gl_legacy_texobj_mag_filter)
    return;

  /* Store new values */
  tex_2d->gl_legacy_texobj_min_filter = min_filter;
  tex_2d->gl_legacy_texobj_mag_filter = mag_filter;

  /* Apply new filters to the texture */
  _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                   tex_2d->gl_texture);
  GE( ctx, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter) );
  GE( ctx, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter) );
}

void
_cogl_texture_2d_gl_flush_legacy_texobj_wrap_modes (CoglTexture *tex,
                                                    GLenum wrap_mode_s,
                                                    GLenum wrap_mode_t)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglContext *ctx = tex->context;

  /* Only set the wrap mode if it's different from the current value
     to avoid too many GL calls. Texture 2D doesn't make use of the r
     coordinate so we can ignore its wrap mode */
  if (tex_2d->gl_legacy_texobj_wrap_mode_s != wrap_mode_s ||
      tex_2d->gl_legacy_texobj_wrap_mode_t != wrap_mode_t)
    {
      _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                       tex_2d->gl_texture);
      GE( ctx, glTexParameteri (GL_TEXTURE_2D,
                                GL_TEXTURE_WRAP_S,
                                wrap_mode_s) );
      GE( ctx, glTexParameteri (GL_TEXTURE_2D,
                                GL_TEXTURE_WRAP_T,
                                wrap_mode_t) );

      tex_2d->gl_legacy_texobj_wrap_mode_s = wrap_mode_s;
      tex_2d->gl_legacy_texobj_wrap_mode_t = wrap_mode_t;
    }
}

void
_cogl_texture_2d_gl_copy_from_framebuffer (CoglTexture2D *tex_2d,
                                           int src_x,
                                           int src_y,
                                           int width,
                                           int height,
                                           CoglFramebuffer *src_fb,
                                           int dst_x,
                                           int dst_y,
                                           int level)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;

  /* Make sure the current framebuffers are bound, though we don't need to
   * flush the clip state here since we aren't going to draw to the
   * framebuffer. */
  _cogl_framebuffer_flush_state (ctx->current_draw_buffer,
                                 src_fb,
                                 COGL_FRAMEBUFFER_STATE_ALL &
                                 ~COGL_FRAMEBUFFER_STATE_CLIP);

  _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                   tex_2d->gl_texture);

  ctx->glCopyTexSubImage2D (GL_TEXTURE_2D,
                            0, /* level */
                            dst_x, dst_y,
                            src_x, src_y,
                            width, height);
}

unsigned int
_cogl_texture_2d_gl_get_gl_handle (CoglTexture2D *tex_2d)
{
    return tex_2d->gl_texture;
}

void
_cogl_texture_2d_gl_generate_mipmap (CoglTexture2D *tex_2d)
{
  _cogl_texture_gl_generate_mipmaps (COGL_TEXTURE (tex_2d));
}

gboolean
_cogl_texture_2d_gl_copy_from_bitmap (CoglTexture2D *tex_2d,
                                      int src_x,
                                      int src_y,
                                      int width,
                                      int height,
                                      CoglBitmap *bmp,
                                      int dst_x,
                                      int dst_y,
                                      int level,
                                      GError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglBitmap *upload_bmp;
  CoglPixelFormat upload_format;
  GLenum gl_format;
  GLenum gl_type;
  gboolean status = TRUE;

  upload_bmp =
    _cogl_bitmap_convert_for_upload (bmp,
                                     _cogl_texture_get_format (tex),
                                     FALSE, /* can't convert in place */
                                     error);
  if (upload_bmp == NULL)
    return FALSE;

  upload_format = cogl_bitmap_get_format (upload_bmp);

  /* Only support single plane formats */
  if (upload_format == COGL_PIXEL_FORMAT_ANY ||
      cogl_pixel_format_get_n_planes (upload_format) != 1)
    return FALSE;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          upload_format,
                                          NULL, /* internal gl format */
                                          &gl_format,
                                          &gl_type);

  if (tex->max_level_set < level)
    cogl_texture_gl_set_max_level (tex, level);

  status = ctx->texture_driver->upload_subregion_to_gl (ctx,
                                                        tex,
                                                        src_x, src_y,
                                                        dst_x, dst_y,
                                                        width, height,
                                                        level,
                                                        upload_bmp,
                                                        gl_format,
                                                        gl_type,
                                                        error);

  cogl_object_unref (upload_bmp);

  return status;
}

gboolean
_cogl_texture_2d_gl_is_get_data_supported (CoglTexture2D *tex_2d)
{
  return tex_2d->is_get_data_supported;
}

void
_cogl_texture_2d_gl_get_data (CoglTexture2D *tex_2d,
                              CoglPixelFormat format,
                              int rowstride,
                              uint8_t *data)
{
  CoglContext *ctx = COGL_TEXTURE (tex_2d)->context;
  uint8_t bpp;
  int width = COGL_TEXTURE (tex_2d)->width;
  GLenum gl_format;
  GLenum gl_type;

  g_return_if_fail (format != COGL_PIXEL_FORMAT_ANY);
  g_return_if_fail (cogl_pixel_format_get_n_planes (format) == 1);

  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          format,
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);

  ctx->texture_driver->prep_gl_for_pixels_download (ctx,
                                                    rowstride,
                                                    width,
                                                    bpp);

  _cogl_bind_gl_texture_transient (tex_2d->gl_target,
                                   tex_2d->gl_texture);

  ctx->texture_driver->gl_get_tex_image (ctx,
                                         tex_2d->gl_target,
                                         gl_format,
                                         gl_type,
                                         data);
}
