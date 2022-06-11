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

#include "clutter-build-config.h"

#include "clutter-texture-content.h"

#include "clutter-actor-private.h"
#include "clutter-content-private.h"
#include "clutter-paint-node.h"

struct _ClutterTextureContent
{
  GObject parent_instance;

  CoglTexture *texture;
};

static void clutter_content_iface_init (ClutterContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterTextureContent, clutter_texture_content,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init))

static void
clutter_texture_content_finalize (GObject *gobject)
{
  ClutterTextureContent *texture_content = CLUTTER_TEXTURE_CONTENT (gobject);

  g_clear_pointer (&texture_content->texture, cogl_object_unref);

  G_OBJECT_CLASS (clutter_texture_content_parent_class)->finalize (gobject);
}

static void
clutter_texture_content_class_init (ClutterTextureContentClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = clutter_texture_content_finalize;
}

static void
clutter_texture_content_init (ClutterTextureContent *texture_content)
{
}

static void
clutter_texture_content_paint_content (ClutterContent      *content,
                                       ClutterActor        *actor,
                                       ClutterPaintNode    *root,
                                       ClutterPaintContext *paint_context)
{
  ClutterTextureContent *texture_content = CLUTTER_TEXTURE_CONTENT (content);
  ClutterPaintNode *node;

  node = clutter_actor_create_texture_paint_node (actor,
                                                  texture_content->texture);
  clutter_paint_node_set_static_name (node, "Texture Content");
  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);
}

static gboolean
clutter_texture_content_get_preferred_size (ClutterContent *content,
                                            float          *width,
                                            float          *height)
{
  ClutterTextureContent *texture_content = CLUTTER_TEXTURE_CONTENT (content);

  if (width != NULL)
    *width = cogl_texture_get_width (texture_content->texture);

  if (height != NULL)
    *height = cogl_texture_get_height (texture_content->texture);

  return TRUE;
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->get_preferred_size = clutter_texture_content_get_preferred_size;
  iface->paint_content = clutter_texture_content_paint_content;
}

/**
 * clutter_texture_content_new_from_texture:
 * @texture: a #CoglTexture
 * @clip: (nullable): A clipping rectangle
 *
 * Creates a new #ClutterTextureContent instance for @texture, taking an
 * internal reference to @texture.
 *
 * If you change the contents of the #CoglTexture you will need
 * to manually invalidate the @texture_content with clutter_content_invalidate()
 * in order to update the actors using @texture_content as their content.
 *
 * Return value: (transfer full): the newly created #ClutterTextureContent instance.
 *   Use g_object_unref() when done.
 */
ClutterContent *
clutter_texture_content_new_from_texture (CoglTexture           *texture,
                                          cairo_rectangle_int_t *clip)
{
  ClutterTextureContent *texture_content;

  g_return_val_if_fail (texture != NULL, NULL);

  texture_content = g_object_new (CLUTTER_TYPE_TEXTURE_CONTENT, NULL);

  if (clip)
    {
      texture_content->texture =
        cogl_texture_new_from_sub_texture (texture,
                                           clip->x,
                                           clip->y,
                                           clip->width,
                                           clip->height);
    }
  else
    {
      texture_content->texture = cogl_object_ref (texture);
    }

  return CLUTTER_CONTENT (texture_content);
}

/**
 * clutter_texture_content_get_texture:
 * @texture_content: a #ClutterTextureContent
 *
 * Retrieves a pointer to the #CoglTexture used by @texture_content.
 *
 * If you change the contents of the returned #CoglTexture you will need
 * to manually invalidate the @texture_content with clutter_content_invalidate()
 * in order to update the actors using @texture_content as their content.
 *
 * Return value: (transfer none): a pointer to the #CoglTexture
 */
CoglTexture *
clutter_texture_content_get_texture (ClutterTextureContent *texture_content)
{
  g_return_val_if_fail (CLUTTER_IS_TEXTURE_CONTENT (texture_content), NULL);

  return texture_content->texture;
}
