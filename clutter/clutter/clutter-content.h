/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012  Intel Corporation.
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
 */

#ifndef __CLUTTER_CONTENT_H__
#define __CLUTTER_CONTENT_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_CONTENT (clutter_content_get_type ())

CLUTTER_EXPORT
G_DECLARE_INTERFACE (ClutterContent, clutter_content, CLUTTER, CONTENT, GObject)

/**
 * ClutterContentInterface:
 * @get_preferred_size: virtual function; should be overridden by subclasses
 *   of #ClutterContent that have a natural size
 * @paint_content: virtual function; called each time the content needs to
 *   paint itself
 * @attached: virtual function; called each time a #ClutterContent is attached
 *   to a #ClutterActor.
 * @detached: virtual function; called each time a #ClutterContent is detached
 *   from a #ClutterActor.
 * @invalidate: virtual function; called each time a #ClutterContent state
 *   is changed.
 *
 * The #ClutterContentInterface structure contains only
 * private data.
 *
 * Since: 1.10
 */
struct _ClutterContentInterface
{
  /*< private >*/
  GTypeInterface g_iface;

  /*< public >*/
  gboolean      (* get_preferred_size)  (ClutterContent   *content,
                                         gfloat           *width,
                                         gfloat           *height);
  void          (* paint_content)       (ClutterContent      *content,
                                         ClutterActor        *actor,
                                         ClutterPaintNode    *node,
                                         ClutterPaintContext *paint_context);

  void          (* attached)            (ClutterContent   *content,
                                         ClutterActor     *actor);
  void          (* detached)            (ClutterContent   *content,
                                         ClutterActor     *actor);

  void          (* invalidate)          (ClutterContent   *content);

  void          (* invalidate_size)     (ClutterContent   *content);
};

CLUTTER_EXPORT
gboolean        clutter_content_get_preferred_size      (ClutterContent *content,
                                                         gfloat         *width,
                                                         gfloat         *height);
CLUTTER_EXPORT
void            clutter_content_invalidate              (ClutterContent *content);

CLUTTER_EXPORT
void            clutter_content_invalidate_size         (ClutterContent *content);

G_END_DECLS

#endif /* __CLUTTER_CONTENT_H__ */
