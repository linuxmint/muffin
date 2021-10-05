/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009 Intel Corporation.
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
 */

#include "cogl-config.h"

#include "cogl-private.h"
#include "cogl-util.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-private.h"
#include "cogl-texture-driver.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl-journal-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-gtype-private.h"
#include "driver/gl/cogl-texture-2d-gl-private.h"
#ifdef COGL_HAS_EGL_SUPPORT
#include "winsys/cogl-winsys-egl-private.h"
#endif

#include <string.h>
#include <math.h>

#ifdef COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT
#include "cogl-wayland-server.h"
#endif

static void _cogl_texture_2d_free (CoglTexture2D *tex_2d);

COGL_TEXTURE_DEFINE (Texture2D, texture_2d);
COGL_GTYPE_DEFINE_CLASS (Texture2D, texture_2d,
                         COGL_GTYPE_IMPLEMENT_INTERFACE (texture));

static const CoglTextureVtable cogl_texture_2d_vtable;

typedef struct _CoglTexture2DManualRepeatData
{
  CoglTexture2D *tex_2d;
  CoglMetaTextureCallback callback;
  void *user_data;
} CoglTexture2DManualRepeatData;

static void
_cogl_texture_2d_free (CoglTexture2D *tex_2d)
{
  CoglContext *ctx = COGL_TEXTURE (tex_2d)->context;

  ctx->driver_vtable->texture_2d_free (tex_2d);

  /* Chain up */
  _cogl_texture_free (COGL_TEXTURE (tex_2d));
}

void
_cogl_texture_2d_set_auto_mipmap (CoglTexture *tex,
                                  gboolean value)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  tex_2d->auto_mipmap = value;
}

CoglTexture2D *
_cogl_texture_2d_create_base (CoglContext *ctx,
                              int width,
                              int height,
                              CoglPixelFormat internal_format,
                              CoglTextureLoader *loader)
{
  CoglTexture2D *tex_2d = g_new (CoglTexture2D, 1);
  CoglTexture *tex = COGL_TEXTURE (tex_2d);

  _cogl_texture_init (tex, ctx, width, height, internal_format, loader,
                      &cogl_texture_2d_vtable);

  tex_2d->mipmaps_dirty = TRUE;
  tex_2d->auto_mipmap = TRUE;
  tex_2d->is_get_data_supported = TRUE;

  tex_2d->gl_target = GL_TEXTURE_2D;

  ctx->driver_vtable->texture_2d_init (tex_2d);

  return _cogl_texture_2d_object_new (tex_2d);
}

CoglTexture2D *
cogl_texture_2d_new_with_size (CoglContext *ctx,
                               int width,
                               int height)
{
  CoglTextureLoader *loader;

  g_return_val_if_fail (width >= 1, NULL);
  g_return_val_if_fail (height >= 1, NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_SIZED;
  loader->src.sized.width = width;
  loader->src.sized.height = height;

  return _cogl_texture_2d_create_base (ctx, width, height,
                                       COGL_PIXEL_FORMAT_RGBA_8888_PRE, loader);
}

static gboolean
_cogl_texture_2d_allocate (CoglTexture *tex,
                           GError **error)
{
  CoglContext *ctx = tex->context;

  return ctx->driver_vtable->texture_2d_allocate (tex, error);
}

CoglTexture2D *
_cogl_texture_2d_new_from_bitmap (CoglBitmap *bmp,
                                  gboolean can_convert_in_place)
{
  CoglTextureLoader *loader;

  g_return_val_if_fail (bmp != NULL, NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_BITMAP;
  loader->src.bitmap.bitmap = cogl_object_ref (bmp);
  loader->src.bitmap.can_convert_in_place = can_convert_in_place;

  return  _cogl_texture_2d_create_base (_cogl_bitmap_get_context (bmp),
                                        cogl_bitmap_get_width (bmp),
                                        cogl_bitmap_get_height (bmp),
                                        cogl_bitmap_get_format (bmp),
                                        loader);
}

CoglTexture2D *
cogl_texture_2d_new_from_bitmap (CoglBitmap *bmp)
{
  return _cogl_texture_2d_new_from_bitmap (bmp,
                                           FALSE); /* can't convert in place */
}

CoglTexture2D *
cogl_texture_2d_new_from_file (CoglContext *ctx,
                               const char *filename,
                               GError **error)
{
  CoglBitmap *bmp;
  CoglTexture2D *tex_2d = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  bmp = _cogl_bitmap_from_file (ctx, filename, error);
  if (bmp == NULL)
    return NULL;

  tex_2d = _cogl_texture_2d_new_from_bitmap (bmp,
                                             TRUE); /* can convert in-place */

  cogl_object_unref (bmp);

  return tex_2d;
}

CoglTexture2D *
cogl_texture_2d_new_from_data (CoglContext *ctx,
                               int width,
                               int height,
                               CoglPixelFormat format,
                               int rowstride,
                               const uint8_t *data,
                               GError **error)
{
  CoglBitmap *bmp;
  CoglTexture2D *tex_2d;

  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, NULL);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, NULL);
  g_return_val_if_fail (data != NULL, NULL);

  /* Rowstride from width if not given */
  if (rowstride == 0)
    rowstride = width * cogl_pixel_format_get_bytes_per_pixel (format, 0);

  /* Wrap the data into a bitmap */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width, height,
                                  format,
                                  rowstride,
                                  (uint8_t *) data);

  tex_2d = cogl_texture_2d_new_from_bitmap (bmp);

  cogl_object_unref (bmp);

  if (tex_2d &&
      !cogl_texture_allocate (COGL_TEXTURE (tex_2d), error))
    {
      cogl_object_unref (tex_2d);
      return NULL;
    }

  return tex_2d;
}

#if defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base)
/* NB: The reason we require the width, height and format to be passed
 * even though they may seem redundant is because GLES 1/2 don't
 * provide a way to query these properties. */
CoglTexture2D *
cogl_egl_texture_2d_new_from_image (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat format,
                                    EGLImageKHR image,
                                    CoglEglImageFlags flags,
                                    GError **error)
{
  CoglTextureLoader *loader;
  CoglTexture2D *tex;

  g_return_val_if_fail (_cogl_context_get_winsys (ctx)->constraints &
                        COGL_RENDERER_CONSTRAINT_USES_EGL,
                        NULL);

  g_return_val_if_fail (_cogl_has_private_feature
                        (ctx,
                        COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE),
                        NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE;
  loader->src.egl_image.image = image;
  loader->src.egl_image.width = width;
  loader->src.egl_image.height = height;
  loader->src.egl_image.format = format;
  loader->src.egl_image.flags = flags;

  tex = _cogl_texture_2d_create_base (ctx, width, height, format, loader);

  if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
    {
      cogl_object_unref (tex);
      return NULL;
    }

  return tex;
}
#endif /* defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base) */

void
_cogl_texture_2d_externally_modified (CoglTexture *texture)
{
  if (!cogl_is_texture_2d (texture))
    return;

  COGL_TEXTURE_2D (texture)->mipmaps_dirty = TRUE;
}

void
_cogl_texture_2d_copy_from_framebuffer (CoglTexture2D *tex_2d,
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

  /* Assert that the storage for this texture has been allocated */
  cogl_texture_allocate (tex, NULL); /* (abort on error) */

  ctx->driver_vtable->texture_2d_copy_from_framebuffer (tex_2d,
                                                        src_x,
                                                        src_y,
                                                        width,
                                                        height,
                                                        src_fb,
                                                        dst_x,
                                                        dst_y,
                                                        level);

  tex_2d->mipmaps_dirty = TRUE;
}

static int
_cogl_texture_2d_get_max_waste (CoglTexture *tex)
{
  return -1;
}

static gboolean
_cogl_texture_2d_is_sliced (CoglTexture *tex)
{
  return FALSE;
}

static gboolean
_cogl_texture_2d_can_hardware_repeat (CoglTexture *tex)
{
  return TRUE;
}

static void
_cogl_texture_2d_transform_coords_to_gl (CoglTexture *tex,
                                         float *s,
                                         float *t)
{
  /* The texture coordinates map directly so we don't need to do
     anything */
}

static CoglTransformResult
_cogl_texture_2d_transform_quad_coords_to_gl (CoglTexture *tex,
                                              float *coords)
{
  /* The texture coordinates map directly so we don't need to do
     anything other than check for repeats */

  int i;

  for (i = 0; i < 4; i++)
    if (coords[i] < 0.0f || coords[i] > 1.0f)
      {
        /* Repeat is needed */
        return (_cogl_texture_2d_can_hardware_repeat (tex) ?
                COGL_TRANSFORM_HARDWARE_REPEAT :
                COGL_TRANSFORM_SOFTWARE_REPEAT);
      }

  /* No repeat is needed */
  return COGL_TRANSFORM_NO_REPEAT;
}

static gboolean
_cogl_texture_2d_get_gl_texture (CoglTexture *tex,
                                 GLuint *out_gl_handle,
                                 GLenum *out_gl_target)
{
  CoglContext *ctx = tex->context;
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  if (ctx->driver_vtable->texture_2d_get_gl_handle)
    {
      GLuint handle;

      if (out_gl_target)
        *out_gl_target = tex_2d->gl_target;

      handle = ctx->driver_vtable->texture_2d_get_gl_handle (tex_2d);

      if (out_gl_handle)
        *out_gl_handle = handle;

      return handle ? TRUE : FALSE;
    }
  else
    return FALSE;
}

static void
_cogl_texture_2d_pre_paint (CoglTexture *tex, CoglTexturePrePaintFlags flags)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  /* Only update if the mipmaps are dirty */
  if ((flags & COGL_TEXTURE_NEEDS_MIPMAP) &&
      tex_2d->auto_mipmap && tex_2d->mipmaps_dirty)
    {
      CoglContext *ctx = tex->context;

      /* Since we are about to ask the GPU to generate mipmaps of tex, we
       * better make sure tex is up-to-date.
       */
      _cogl_texture_flush_journal_rendering (tex);

      ctx->driver_vtable->texture_2d_generate_mipmap (tex_2d);

      tex_2d->mipmaps_dirty = FALSE;
    }
}

static void
_cogl_texture_2d_ensure_non_quad_rendering (CoglTexture *tex)
{
  /* Nothing needs to be done */
}

static gboolean
_cogl_texture_2d_set_region (CoglTexture *tex,
                             int src_x,
                             int src_y,
                             int dst_x,
                             int dst_y,
                             int width,
                             int height,
                             int level,
                             CoglBitmap *bmp,
                             GError **error)
{
  CoglContext *ctx = tex->context;
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  if (!ctx->driver_vtable->texture_2d_copy_from_bitmap (tex_2d,
                                                        src_x,
                                                        src_y,
                                                        width,
                                                        height,
                                                        bmp,
                                                        dst_x,
                                                        dst_y,
                                                        level,
                                                        error))
    {
      return FALSE;
    }

  tex_2d->mipmaps_dirty = TRUE;

  return TRUE;
}

static gboolean
_cogl_texture_2d_is_get_data_supported (CoglTexture *tex)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglContext *ctx = tex->context;

  return ctx->driver_vtable->texture_2d_is_get_data_supported (tex_2d);
}

static gboolean
_cogl_texture_2d_get_data (CoglTexture *tex,
                           CoglPixelFormat format,
                           int rowstride,
                           uint8_t *data)
{
  CoglContext *ctx = tex->context;

  if (ctx->driver_vtable->texture_2d_get_data)
    {
      CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
      ctx->driver_vtable->texture_2d_get_data (tex_2d, format, rowstride, data);
      return TRUE;
    }
  else
    return FALSE;
}

static CoglPixelFormat
_cogl_texture_2d_get_format (CoglTexture *tex)
{
  return COGL_TEXTURE_2D (tex)->internal_format;
}

static GLenum
_cogl_texture_2d_get_gl_format (CoglTexture *tex)
{
  return COGL_TEXTURE_2D (tex)->gl_internal_format;
}

static const CoglTextureVtable
cogl_texture_2d_vtable =
  {
    TRUE, /* primitive */
    _cogl_texture_2d_allocate,
    _cogl_texture_2d_set_region,
    _cogl_texture_2d_is_get_data_supported,
    _cogl_texture_2d_get_data,
    NULL, /* foreach_sub_texture_in_region */
    _cogl_texture_2d_get_max_waste,
    _cogl_texture_2d_is_sliced,
    _cogl_texture_2d_can_hardware_repeat,
    _cogl_texture_2d_transform_coords_to_gl,
    _cogl_texture_2d_transform_quad_coords_to_gl,
    _cogl_texture_2d_get_gl_texture,
    _cogl_texture_2d_gl_flush_legacy_texobj_filters,
    _cogl_texture_2d_pre_paint,
    _cogl_texture_2d_ensure_non_quad_rendering,
    _cogl_texture_2d_gl_flush_legacy_texobj_wrap_modes,
    _cogl_texture_2d_get_format,
    _cogl_texture_2d_get_gl_format,
    _cogl_texture_2d_set_auto_mipmap
  };
