/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __META_SHAPED_TEXTURE_H__
#define __META_SHAPED_TEXTURE_H__

#include <X11/Xlib.h>

#include "clutter/clutter.h"
#include <meta/common.h>

G_BEGIN_DECLS

#define META_TYPE_SHAPED_TEXTURE (meta_shaped_texture_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaShapedTexture,
                      meta_shaped_texture,
                      META, SHAPED_TEXTURE,
                      GObject)


META_EXPORT
void meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *stex,
					     gboolean           create_mipmaps);

META_EXPORT
CoglTexture * meta_shaped_texture_get_texture (MetaShapedTexture *stex);

META_EXPORT
void meta_shaped_texture_set_mask_texture (MetaShapedTexture *stex,
                                           CoglTexture       *mask_texture);

META_EXPORT
void meta_shaped_texture_set_opaque_region (MetaShapedTexture *stex,
                                            cairo_region_t    *opaque_region);

META_EXPORT
cairo_surface_t * meta_shaped_texture_get_image (MetaShapedTexture     *stex,
                                                 cairo_rectangle_int_t *clip);

G_END_DECLS

#endif /* __META_SHAPED_TEXTURE_H__ */
