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
#include <meta/errors.h>

#include <gdk/gdk.h>

/**
 * meta_create_color_texture_4ub:
 * @red:
 * @green:
 * @blue:
 * @alpha:
 * @flags: Optional flags for the texture, or %COGL_TEXTURE_NONE;
 *   %COGL_TEXTURE_NO_SLICING is useful if the texture will be
 *   repeated to create a constant color fill, since hardware
 *   repeat can't be used for a sliced texture.
 *
 * Creates a texture that is a single pixel with the specified
 * unpremultiplied color components.
 *
 * Return value: (transfer full): a newly created Cogl texture
 */
CoglTexture *
meta_create_color_texture_4ub (guint8           red,
                               guint8           green,
                               guint8           blue,
                               guint8           alpha,
                               CoglTextureFlags flags)
{
  CoglColor color;
  guint8 pixel[4];

  cogl_color_set_from_4ub (&color, red, green, blue, alpha);
  cogl_color_premultiply (&color);

  pixel[0] = cogl_color_get_red_byte (&color);
  pixel[1] = cogl_color_get_green_byte (&color);
  pixel[2] = cogl_color_get_blue_byte (&color);
  pixel[3] = cogl_color_get_alpha_byte (&color);

  return cogl_texture_new_from_data (1, 1,
                                     flags,
                                     COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                     COGL_PIXEL_FORMAT_ANY,
                                     4, pixel);
}

CoglPipeline *
meta_create_texture_pipeline (CoglTexture *src_texture)
{
  static CoglPipeline *texture_pipeline_template = NULL;
  CoglPipeline *pipeline;

  /* We use a pipeline that has a dummy texture as a base for all
     texture pipelines. The idea is that only the Cogl texture object
     would be different in the children so it is likely that Cogl will
     be able to share GL programs between all the textures. */
  if (G_UNLIKELY (texture_pipeline_template == NULL))
    {
      CoglTexture *dummy_texture;
      CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

      dummy_texture = meta_create_color_texture_4ub (0xff, 0xff, 0xff, 0xff,
                                                     COGL_TEXTURE_NONE);


      texture_pipeline_template = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_texture (texture_pipeline_template, 0, dummy_texture);
      cogl_object_unref (dummy_texture);
    }

  pipeline = cogl_pipeline_copy (texture_pipeline_template);

  if (src_texture != NULL)
    cogl_pipeline_set_layer_texture (pipeline, 0, src_texture);

  return pipeline;
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
        CoglError *error = NULL;

        texture = COGL_TEXTURE (cogl_texture_2d_new_from_data (cogl_context, width, height,
                                                               format,
                                                               rowstride,
                                                               data,
                                                               &error));
        if (error)
          {
            meta_verbose ("cogl_texture_2d_new_from_data failed: %s\n", error->message);
            cogl_error_free (error);
          }
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

CoglTexture *
meta_cogl_rectangle_new (int width,
                         int height,
                         CoglPixelFormat format,
                         int stride,
                         const uint8_t *data)
{
  CoglTexture *texture = COGL_TEXTURE (cogl_texture_rectangle_new_with_size (cogl_context, width, height));
  cogl_texture_set_components (texture, COGL_TEXTURE_COMPONENTS_A);
  cogl_texture_set_region (texture,
                           0, 0, /* src_x/y */
                           0, 0, /* dst_x/y */
                           width, height, /* dst_width/height */
                           width, height, /* width/height */
                           format,
                           stride, data);

  return texture;
}