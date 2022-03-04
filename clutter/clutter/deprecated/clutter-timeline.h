/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corp
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
 */

#ifndef __CLUTTER_TIMELINE_PRIVATE_H__
#define __CLUTTER_TIMELINE_PRIVATE_H__

#include <clutter/clutter-timeline.h>

G_BEGIN_DECLS

CLUTTER_DEPRECATED_FOR(clutter_timeline_new)
ClutterTimeline *               clutter_timeline_clone                  (ClutterTimeline          *timeline);

CLUTTER_DEPRECATED_FOR(clutter_timeline_set_repeat_count)
void                            clutter_timeline_set_loop               (ClutterTimeline          *timeline,
                                                                         gboolean                  loop);

CLUTTER_DEPRECATED_FOR(clutter_timeline_get_repeat_count)
gboolean                        clutter_timeline_get_loop               (ClutterTimeline          *timeline);

G_END_DECLS

#endif /* __CLUTTER_TIMELINE_PRIVATE_H__ */
