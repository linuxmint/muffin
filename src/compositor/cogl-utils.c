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
#include "cogl-utils.h"

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
LOCAL_SYMBOL CoglHandle
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


/* Based on gnome-shell/src/st/st-private.c:_st_create_texture_material.c */

/**
 * meta_create_texture_material:
 * @src_texture: (allow-none): texture to use initially for the layer
 *
 * Creates a material with a single layer. Using a common template
 * allows sharing a shader for different uses in Muffin. To share the same
 * shader with all other materials that are just texture plus opacity
 * would require Cogl fixes.
 * (See http://bugzilla.clutter-project.org/show_bug.cgi?id=2425)
 *
 * Return value: (transfer full): a newly created Cogl material
 */
LOCAL_SYMBOL CoglHandle
meta_create_texture_material (CoglHandle src_texture)
{
  static CoglHandle texture_material_template = COGL_INVALID_HANDLE;
  CoglHandle material;

  /* We use a material that has a dummy texture as a base for all
     texture materials. The idea is that only the Cogl texture object
     would be different in the children so it is likely that Cogl will
     be able to share GL programs between all the textures. */
  if (G_UNLIKELY (texture_material_template == COGL_INVALID_HANDLE))
    {
      CoglHandle dummy_texture;

      dummy_texture = meta_create_color_texture_4ub (0xff, 0xff, 0xff, 0xff,
                                                     COGL_TEXTURE_NONE);

      texture_material_template = cogl_material_new ();
      cogl_material_set_layer (texture_material_template, 0, dummy_texture);
      cogl_handle_unref (dummy_texture);
    }

  material = cogl_material_copy (texture_material_template);

  if (src_texture != COGL_INVALID_HANDLE)
    cogl_material_set_layer (material, 0, src_texture);

  return material;
}
