/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Cogl
 *
 * Copyright 2010 Red Hat, Inc.
 * Copyright 2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <clutter/clutter.h>
#include "cogl-utils.h"
#include <gdk/gdk.h>

/* Based on gnome-shell/src/st/st-private.c:_st_create_texture_material.c */

/**
 * meta_create_texture_pipeline:
 * @src_texture: (allow-none): texture to use initially for the layer
 *
 * Creates a pipeline with a single layer. Using a common template
 * makes it easier for Cogl to share a shader for different uses in
 * Muffin.
 *
 * Return value: (transfer full): a newly created #CoglPipeline
 */
CoglPipeline *
meta_create_texture_pipeline (CoglTexture *src_texture)
{
  static CoglPipeline *texture_pipeline_template = NULL;
  CoglPipeline *pipeline;

  /* The only state used in the pipeline that would affect the shader
     generation is the texture type on the layer. Therefore we create
     a template pipeline which sets this state and all texture
     pipelines are created as a copy of this. That way Cogl can find
     the shader state for the pipeline more quickly by looking at the
     pipeline ancestry instead of resorting to the shader cache. */
  if (G_UNLIKELY (texture_pipeline_template == NULL))
    {
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());
      texture_pipeline_template = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_null_texture (texture_pipeline_template,
                                            0, /* layer */
                                            COGL_TEXTURE_TYPE_2D);
    }

  pipeline = cogl_pipeline_copy (texture_pipeline_template);

  if (src_texture != NULL)
    cogl_pipeline_set_layer_texture (pipeline, 0, src_texture);

  return pipeline;
}

static gboolean is_pot(int x)
{
  return x > 0 && (x & (x - 1)) == 0;
}

/**
 * meta_create_texture:
 * @width: width of the texture to create
 * @height: height of the texture to create
 * @components; components to store in the texture (color or alpha)
 * @flags: flags that affect the allocation behavior
 *
 * Creates a texture of the given size with the specified components
 * for use as a frame buffer object.
 *
 * If non-power-of-two textures are not supported on the system, then
 * the texture will be created as a texture rectangle; in this case,
 * hardware repeating isn't possible, and texture coordinates are also
 * different, but Cogl hides these issues from the application, except from
 * GLSL shaders. Since GLSL is never (or at least almost never)
 * present on such a system, this is not typically an issue.
 *
 * If %META_TEXTURE_ALLOW_SLICING is present in @flags, and the texture
 * is larger than the texture size limits of the system, then the texture
 * will be created as a sliced texture. This also will cause problems
 * with using the texture with GLSL, and is more likely to be an issue
 * since all GL implementations have texture size limits, and they can
 * be as small as 2048x2048 on reasonably current systems.
 */
CoglTexture *
meta_create_texture (int                   width,
                     int                   height,
                     CoglTextureComponents components,
                     MetaTextureFlags      flags)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *ctx = clutter_backend_get_cogl_context (backend);
  CoglTexture *texture;

  gboolean should_use_rectangle = FALSE;

  if (!(is_pot (width) && is_pot (height)) &&
      !cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT))
    {
      if (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_RECTANGLE))
        should_use_rectangle = TRUE;
      else
        g_error ("Cannot create texture. Support for GL_ARB_texture_non_power_of_two or "
                 "ARB_texture_rectangle is required");
    }

  if (should_use_rectangle)
    texture = COGL_TEXTURE (cogl_texture_rectangle_new_with_size (ctx, width, height));
  else
    texture = COGL_TEXTURE (cogl_texture_2d_new_with_size (ctx, width, height));
  cogl_texture_set_components (texture, components);

  if ((flags & META_TEXTURE_ALLOW_SLICING) != 0)
    {
      /* To find out if we need to slice the texture, we have to go ahead and force storage
       * to be allocated
       */
      CoglError *catch_error = NULL;
      if (!cogl_texture_allocate (texture, &catch_error))
        {
          cogl_error_free (catch_error);
          cogl_object_unref (texture);
          texture = COGL_TEXTURE (cogl_texture_2d_sliced_new_with_size (ctx, width, height, COGL_TEXTURE_MAX_WASTE));
          cogl_texture_set_components (texture, components);
        }
    }

  return texture;
}

/********************************************************************************************/
/********************************* CoglTexture2d wrapper ************************************/

static CoglContext *cogl_context = NULL;
static gboolean supports_npot = FALSE;

static gint screen_width = 0;
static gint screen_height = 0;

inline static gboolean
hardware_supports_npot_sizes (void)
{
    if (cogl_context != NULL)
        return supports_npot;

    ClutterBackend *backend = clutter_get_default_backend ();
    cogl_context = clutter_backend_get_cogl_context (backend);
    supports_npot = cogl_has_feature (cogl_context, COGL_FEATURE_ID_TEXTURE_NPOT);

    return supports_npot;
}

inline static void
clamp_sizes (gint *width,
             gint *height)
{
    if (screen_width == 0)
      {
        GdkScreen *screen = gdk_screen_get_default ();

        screen_width = gdk_screen_get_width (screen);
        screen_height = gdk_screen_get_height (screen);
      }

    *width = MIN (*width, screen_width * 2);
    *height = MIN (*height, screen_height * 2);
}

/**
 * meta_cogl_texture_new_from_data_wrapper: (skip)
 *
 * Decides whether to use the newer (apparently safer)
 * cogl_texture_2d_new_from_data or the older cogl_texture_new_from_data
 * depending on if the GPU supports it.
 */

CoglTexture *
meta_cogl_texture_new_from_data_wrapper                (int  width,
                                                        int  height,
                                           CoglTextureFlags  flags,
                                            CoglPixelFormat  format,
                                            CoglPixelFormat  internal_format,
                                                        int  rowstride,
                                              const uint8_t *data)
{
    CoglTexture *texture = NULL;

    clamp_sizes (&width, &height);

    if (hardware_supports_npot_sizes ())
      {
        texture = COGL_TEXTURE (cogl_texture_2d_new_from_data (cogl_context, width, height,
                                                               format,
#if COGL_VERSION < COGL_VERSION_ENCODE (1, 18, 0)
                                                               COGL_PIXEL_FORMAT_ANY,
#endif
                                                               rowstride,
                                                               data,
                                                               NULL));
      }
    else
      {
        texture = cogl_texture_new_from_data (width,
                                              height,
                                              flags,
                                              format,
                                              internal_format,
                                              rowstride,
                                              data);
      }

    return texture;
}

/**
 * meta_cogl_texture_new_from_file_wrapper: (skip)
 *
 * Decides whether to use the newer (apparently safer)
 * cogl_texture_2d_new_from_file or the older cogl_texture_new_from_file
 * depending on if the GPU supports it.
 */

CoglTexture *
meta_cogl_texture_new_from_file_wrapper         (const char *filename,
                                           CoglTextureFlags  flags,
                                            CoglPixelFormat  internal_format)
{
    CoglTexture *texture = NULL;

    if (hardware_supports_npot_sizes ())
      {
        texture = COGL_TEXTURE (cogl_texture_2d_new_from_file (cogl_context,
                                                               filename,
#if COGL_VERSION < COGL_VERSION_ENCODE (1, 18, 0)
                                                               COGL_PIXEL_FORMAT_ANY,
#endif
                                                               NULL));
      }
    else
      {
        texture = cogl_texture_new_from_file (filename,
                                              flags,
                                              internal_format,
                                              NULL);
      }

    return texture;
}

/**
 * meta_cogl_texture_new_with_size_wrapper: (skip)
 *
 * Decides whether to use the newer (apparently safer)
 * cogl_texture_2d_new_with_size or the older cogl_texture_new_with_size
 * depending on if the GPU supports it.
 */

CoglTexture *
meta_cogl_texture_new_with_size_wrapper           (int width,
                                                   int height,
                                      CoglTextureFlags flags,
                                       CoglPixelFormat internal_format)
{
    CoglTexture *texture = NULL;

    clamp_sizes (&width, &height);

    if (hardware_supports_npot_sizes ())
      {
        texture = COGL_TEXTURE (cogl_texture_2d_new_with_size (cogl_context,
                                                               width,
                                                               height
#if COGL_VERSION < COGL_VERSION_ENCODE (1, 18, 0)
                                                              ,CLUTTER_CAIRO_FORMAT_ARGB32
#endif
                                                              ));
      }
    else
      {
        texture = cogl_texture_new_with_size (width, height,
                                              flags,
                                              internal_format);
      }

    return texture;
}

