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

#include "cogl-config.h"

#include <string.h>

#include "cogl-private.h"
#include "cogl-context-private.h"
#include "cogl-feature-private.h"
#include "cogl-renderer-private.h"
#include "driver/gl/cogl-util-gl-private.h"
#include "driver/gl/cogl-framebuffer-gl-private.h"
#include "driver/gl/cogl-texture-2d-gl-private.h"
#include "driver/gl/cogl-attribute-gl-private.h"
#include "driver/gl/cogl-clip-stack-gl-private.h"
#include "driver/gl/cogl-buffer-gl-private.h"

static gboolean
_cogl_driver_gl_real_context_init (CoglContext *context)
{

  _cogl_driver_gl_context_init (context);

  if ((context->driver == COGL_DRIVER_GL3))
    {
      GLuint vertex_array;

      /* In a forward compatible context, GL 3 doesn't support rendering
       * using the default vertex array object. Cogl doesn't use vertex
       * array objects yet so for now we just create a dummy array
       * object that we will use as our own default object. Eventually
       * it could be good to attach the vertex array objects to
       * CoglPrimitives */
      context->glGenVertexArrays (1, &vertex_array);
      context->glBindVertexArray (vertex_array);
    }

  /* As far as I can tell, GL_POINT_SPRITE doesn't have any effect
     unless GL_COORD_REPLACE is enabled for an individual layer.
     Therefore it seems like it should be ok to just leave it enabled
     all the time instead of having to have a set property on each
     pipeline to track whether any layers have point sprite coords
     enabled. We don't need to do this for GL3 or GLES2 because point
     sprites are handled using a builtin varying in the shader. */
  if (context->driver == COGL_DRIVER_GL)
    GE (context, glEnable (GL_POINT_SPRITE));

  /* There's no enable for this in GLES2, it's always on */
  if (context->driver == COGL_DRIVER_GL ||
      context->driver == COGL_DRIVER_GL3)
    GE (context, glEnable (GL_PROGRAM_POINT_SIZE) );

  return TRUE;
}

static gboolean
_cogl_driver_pixel_format_from_gl_internal (CoglContext *context,
                                            GLenum gl_int_format,
                                            CoglPixelFormat *out_format)
{
  /* It doesn't really matter we convert to exact same
     format (some have no cogl match anyway) since format
     is re-matched against cogl when getting or setting
     texture image data.
  */

  switch (gl_int_format)
    {
    case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8:
    case GL_ALPHA12: case GL_ALPHA16:
      /* Cogl only supports one single-component texture so if we have
       * ended up with a red texture then it is probably being used as
       * a component-alpha texture */
    case GL_RED:

      *out_format = COGL_PIXEL_FORMAT_A_8;
      return TRUE;

    case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8:
    case GL_LUMINANCE12: case GL_LUMINANCE16:

      *out_format = COGL_PIXEL_FORMAT_G_8;
      return TRUE;

    case GL_RG:
      *out_format = COGL_PIXEL_FORMAT_RG_88;
      return TRUE;

    case GL_RGB: case GL_RGB4: case GL_RGB5: case GL_RGB8:
    case GL_RGB10: case GL_RGB12: case GL_RGB16: case GL_R3_G3_B2:

      *out_format = COGL_PIXEL_FORMAT_RGB_888;
      return TRUE;

    case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case GL_RGB5_A1:
    case GL_RGBA8: case GL_RGB10_A2: case GL_RGBA12: case GL_RGBA16:

      *out_format = COGL_PIXEL_FORMAT_RGBA_8888;
      return TRUE;
    }

  return FALSE;
}

static CoglPixelFormat
_cogl_driver_pixel_format_to_gl (CoglContext     *context,
                                 CoglPixelFormat  format,
                                 GLenum          *out_glintformat,
                                 GLenum          *out_glformat,
                                 GLenum          *out_gltype)
{
  CoglPixelFormat required_format;
  GLenum glintformat = 0;
  GLenum glformat = 0;
  GLenum gltype = 0;

  required_format = format;

  /* Find GL equivalents */
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
      /* If the driver doesn't natively support alpha textures then we
       * will use a red component texture with a swizzle to implement
       * the texture */
      if (_cogl_has_private_feature
          (context, COGL_PRIVATE_FEATURE_ALPHA_TEXTURES) == 0)
        {
          glintformat = GL_RED;
          glformat = GL_RED;
        }
      else
        {
          glintformat = GL_ALPHA;
          glformat = GL_ALPHA;
        }
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_G_8:
      glintformat = GL_LUMINANCE;
      glformat = GL_LUMINANCE;
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_RG_88:
      if (cogl_has_feature (context, COGL_FEATURE_ID_TEXTURE_RG))
        {
          glintformat = GL_RG;
          glformat = GL_RG;
        }
      else
        {
          /* If red-green textures aren't supported then we'll use RGB
           * as an internal format. Note this should only end up
           * mattering for downloading the data because Cogl will
           * refuse to allocate a texture with RG components if RG
           * textures aren't supported */
          glintformat = GL_RGB;
          glformat = GL_RGB;
          required_format = COGL_PIXEL_FORMAT_RGB_888;
        }
      gltype = GL_UNSIGNED_BYTE;
      break;

    case COGL_PIXEL_FORMAT_RGB_888:
      glintformat = GL_RGB;
      glformat = GL_RGB;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_BGR_888:
      glintformat = GL_RGB;
      glformat = GL_BGR;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      glintformat = GL_RGBA;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_BYTE;
      break;
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      glintformat = GL_RGBA;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_BYTE;
      break;

      /* The following two types of channel ordering
       * have no GL equivalent unless defined using
       * system word byte ordering */
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
      glintformat = GL_RGBA;
      glformat = GL_BGRA;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gltype = GL_UNSIGNED_INT_8_8_8_8;
#else
      gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
      break;

    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
      glintformat = GL_RGBA;
      glformat = GL_RGBA;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gltype = GL_UNSIGNED_INT_8_8_8_8;
#else
      gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
#endif
      break;

    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
      glintformat = GL_RGBA;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_INT_10_10_10_2;
      break;

    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
      glintformat = GL_RGBA;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_INT_10_10_10_2;
      break;

    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
      glintformat = GL_RGBA;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
      break;

    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      glintformat = GL_RGBA;
      glformat = GL_BGRA;
      gltype = GL_UNSIGNED_INT_2_10_10_10_REV;
      break;

      /* The following three types of channel ordering
       * are always defined using system word byte
       * ordering (even according to GLES spec) */
    case COGL_PIXEL_FORMAT_RGB_565:
      glintformat = GL_RGB;
      glformat = GL_RGB;
      gltype = GL_UNSIGNED_SHORT_5_6_5;
      break;
    case COGL_PIXEL_FORMAT_RGBA_4444:
    case COGL_PIXEL_FORMAT_RGBA_4444_PRE:
      glintformat = GL_RGBA;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_SHORT_4_4_4_4;
      break;
    case COGL_PIXEL_FORMAT_RGBA_5551:
    case COGL_PIXEL_FORMAT_RGBA_5551_PRE:
      glintformat = GL_RGBA;
      glformat = GL_RGBA;
      gltype = GL_UNSIGNED_SHORT_5_5_5_1;
      break;

    case COGL_PIXEL_FORMAT_DEPTH_16:
      glintformat = GL_DEPTH_COMPONENT16;
      glformat = GL_DEPTH_COMPONENT;
      gltype = GL_UNSIGNED_SHORT;
      break;
    case COGL_PIXEL_FORMAT_DEPTH_32:
      glintformat = GL_DEPTH_COMPONENT32;
      glformat = GL_DEPTH_COMPONENT;
      gltype = GL_UNSIGNED_INT;
      break;

    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
      glintformat = GL_DEPTH_STENCIL;
      glformat = GL_DEPTH_STENCIL;
      gltype = GL_UNSIGNED_INT_24_8;
      break;

    case COGL_PIXEL_FORMAT_ANY:
    case COGL_PIXEL_FORMAT_YUV:
      g_assert_not_reached ();
      break;
    }

  /* All of the pixel formats are handled above so if this hits then
     we've been given an invalid pixel format */
  g_assert (glformat != 0);

  if (out_glintformat != NULL)
    *out_glintformat = glintformat;
  if (out_glformat != NULL)
    *out_glformat = glformat;
  if (out_gltype != NULL)
    *out_gltype = gltype;

  return required_format;
}

static gboolean
_cogl_get_gl_version (CoglContext *ctx,
                      int *major_out,
                      int *minor_out)
{
  const char *version_string;

  /* Get the OpenGL version number */
  if ((version_string = _cogl_context_get_gl_version (ctx)) == NULL)
    return FALSE;

  return _cogl_gl_util_parse_gl_version (version_string, major_out, minor_out);
}

static gboolean
check_gl_version (CoglContext *ctx,
                  char **gl_extensions,
                  GError **error)
{
  int major, minor;

  if (!_cogl_get_gl_version (ctx, &major, &minor))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_UNKNOWN_VERSION,
                   "The OpenGL version could not be determined");
      return FALSE;
    }

  /* We require GLSL 1.20, which is implied by OpenGL 2.1. */
  if (!COGL_CHECK_GL_VERSION (major, minor, 2, 1))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_INVALID_VERSION,
                   "OpenGL 2.1 or better is required");
      return FALSE;
    }

  return TRUE;
}

static gboolean
_cogl_driver_update_features (CoglContext *ctx,
                              GError **error)
{
  unsigned long private_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_N_PRIVATE_FEATURES)] = { 0 };
  char **gl_extensions;
  const char *glsl_version;
  int gl_major = 0, gl_minor = 0;
  int i;

  /* We have to special case getting the pointer to the glGetString*
     functions because we need to use them to determine what functions
     we can expect */
  ctx->glGetString =
    (void *) _cogl_renderer_get_proc_address (ctx->display->renderer,
                                              "glGetString",
                                              TRUE);
  ctx->glGetStringi =
    (void *) _cogl_renderer_get_proc_address (ctx->display->renderer,
                                              "glGetStringi",
                                              TRUE);
  ctx->glGetIntegerv =
    (void *) _cogl_renderer_get_proc_address (ctx->display->renderer,
                                              "glGetIntegerv",
                                              TRUE);

  gl_extensions = _cogl_context_get_gl_extensions (ctx);

  if (!check_gl_version (ctx, gl_extensions, error))
    return FALSE;

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_WINSYS)))
    {
      char *all_extensions = g_strjoinv (" ", gl_extensions);

      COGL_NOTE (WINSYS,
                 "Checking features\n"
                 "  GL_VENDOR: %s\n"
                 "  GL_RENDERER: %s\n"
                 "  GL_VERSION: %s\n"
                 "  GL_EXTENSIONS: %s",
                 ctx->glGetString (GL_VENDOR),
                 ctx->glGetString (GL_RENDERER),
                 _cogl_context_get_gl_version (ctx),
                 all_extensions);

      g_free (all_extensions);
    }

  _cogl_get_gl_version (ctx, &gl_major, &gl_minor);

  _cogl_gpu_info_init (ctx, &ctx->gpu);

  ctx->glsl_major = 1;
  ctx->glsl_minor = 2;
  ctx->glsl_version_to_use = 120;

  glsl_version = (char *)ctx->glGetString (GL_SHADING_LANGUAGE_VERSION);
  _cogl_gl_util_parse_gl_version (glsl_version,
                                  &ctx->glsl_major,
                                  &ctx->glsl_minor);

  COGL_FLAGS_SET (ctx->features,
                  COGL_FEATURE_ID_UNSIGNED_INT_INDICES, TRUE);

  _cogl_feature_check_ext_functions (ctx,
                                     gl_major,
                                     gl_minor,
                                     gl_extensions);

  if (_cogl_check_extension ("GL_MESA_pack_invert", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_MESA_PACK_INVERT, TRUE);

  if (!ctx->glGenRenderbuffers)
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_NO_SUITABLE_DRIVER_FOUND,
                   "Framebuffer objects are required to use the GL driver");
      return FALSE;
    }
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_QUERY_FRAMEBUFFER_BITS,
                  TRUE);

  if (ctx->glBlitFramebuffer)
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_BLIT_FRAMEBUFFER, TRUE);

  COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_PBOS, TRUE);

  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_MAP_BUFFER_FOR_READ, TRUE);
  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE, TRUE);

  if (ctx->glEGLImageTargetTexture2D)
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE, TRUE);

  if (_cogl_check_extension ("GL_EXT_packed_depth_stencil", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_EXT_PACKED_DEPTH_STENCIL, TRUE);

  if (ctx->glGenSamplers)
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS, TRUE);

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 3) ||
      _cogl_check_extension ("GL_ARB_texture_swizzle", gl_extensions) ||
      _cogl_check_extension ("GL_EXT_texture_swizzle", gl_extensions))
    COGL_FLAGS_SET (private_features,
                    COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE, TRUE);

  if (ctx->driver == COGL_DRIVER_GL)
    {
      /* Features which are not available in GL 3 */
      COGL_FLAGS_SET (private_features,
                      COGL_PRIVATE_FEATURE_ALPHA_TEXTURES, TRUE);
    }

  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_READ_PIXELS_ANY_FORMAT, TRUE);
  COGL_FLAGS_SET (private_features, COGL_PRIVATE_FEATURE_ANY_GL, TRUE);
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_FORMAT_CONVERSION, TRUE);
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_QUERY_TEXTURE_PARAMETERS, TRUE);
  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_TEXTURE_MAX_LEVEL, TRUE);

  if (ctx->glFenceSync)
    COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_FENCE, TRUE);

  if (COGL_CHECK_GL_VERSION (gl_major, gl_minor, 3, 0) ||
      _cogl_check_extension ("GL_ARB_texture_rg", gl_extensions))
    COGL_FLAGS_SET (ctx->features,
                    COGL_FEATURE_ID_TEXTURE_RG,
                    TRUE);

  /* Cache features */
  for (i = 0; i < G_N_ELEMENTS (private_features); i++)
    ctx->private_features[i] |= private_features[i];

  g_strfreev (gl_extensions);

  if (!COGL_FLAGS_GET (private_features, COGL_PRIVATE_FEATURE_ALPHA_TEXTURES) &&
      !COGL_FLAGS_GET (private_features, COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE))
    {
      g_set_error (error,
                   COGL_DRIVER_ERROR,
                   COGL_DRIVER_ERROR_NO_SUITABLE_DRIVER_FOUND,
                   "The GL_ARB_texture_swizzle extension is required "
                   "to use the GL3 driver");
      return FALSE;
    }

  return TRUE;
}

const CoglDriverVtable
_cogl_driver_gl =
  {
    _cogl_driver_gl_real_context_init,
    _cogl_driver_gl_context_deinit,
    _cogl_driver_pixel_format_from_gl_internal,
    _cogl_driver_pixel_format_to_gl,
    _cogl_driver_update_features,
    _cogl_offscreen_gl_allocate,
    _cogl_offscreen_gl_free,
    _cogl_framebuffer_gl_flush_state,
    _cogl_framebuffer_gl_clear,
    _cogl_framebuffer_gl_query_bits,
    _cogl_framebuffer_gl_finish,
    _cogl_framebuffer_gl_flush,
    _cogl_framebuffer_gl_discard_buffers,
    _cogl_framebuffer_gl_draw_attributes,
    _cogl_framebuffer_gl_draw_indexed_attributes,
    _cogl_framebuffer_gl_read_pixels_into_bitmap,
    _cogl_texture_2d_gl_free,
    _cogl_texture_2d_gl_can_create,
    _cogl_texture_2d_gl_init,
    _cogl_texture_2d_gl_allocate,
    _cogl_texture_2d_gl_copy_from_framebuffer,
    _cogl_texture_2d_gl_get_gl_handle,
    _cogl_texture_2d_gl_generate_mipmap,
    _cogl_texture_2d_gl_copy_from_bitmap,
    _cogl_texture_2d_gl_is_get_data_supported,
    _cogl_texture_2d_gl_get_data,
    _cogl_gl_flush_attributes_state,
    _cogl_clip_stack_gl_flush,
    _cogl_buffer_gl_create,
    _cogl_buffer_gl_destroy,
    _cogl_buffer_gl_map_range,
    _cogl_buffer_gl_unmap,
    _cogl_buffer_gl_set_data,
  };
