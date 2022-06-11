/*
 * Clutter.
 *
 * An OpenGL based 'interactive image' library.
 *
 * Copyright (C) 2012  Intel Corporation.
 * Copyright (C) 2021  Robert Mader.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *   Robert Mader <robert.mader@posteo.de>
 */

#ifndef CLUTTER_TEXTURE_CONTENT_H
#define CLUTTER_TEXTURE_CONTENT_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <cogl/cogl.h>
#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_TEXTURE_CONTENT (clutter_texture_content_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterTextureContent, clutter_texture_content,
                      CLUTTER, TEXTURE_CONTENT, GObject)

CLUTTER_EXPORT
ClutterContent * clutter_texture_content_new_from_texture (CoglTexture           *texture,
                                                           cairo_rectangle_int_t *clip);

CLUTTER_EXPORT
CoglTexture * clutter_texture_content_get_texture (ClutterTextureContent *texture_content);

G_END_DECLS

#endif /* CLUTTER_TEXTURE_CONTENT_H */
