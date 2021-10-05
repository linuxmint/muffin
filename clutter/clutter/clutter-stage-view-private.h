/*
 * Copyright (C) 2019 Red Hat Inc.
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

#ifndef __CLUTTER_STAGE_VIEW_PRIVATE_H__
#define __CLUTTER_STAGE_VIEW_PRIVATE_H__

#include "clutter/clutter-stage-view.h"

void clutter_stage_view_after_paint (ClutterStageView *view);

gboolean clutter_stage_view_is_dirty_viewport (ClutterStageView *view);

void clutter_stage_view_set_dirty_viewport (ClutterStageView *view,
                                            gboolean          dirty);

gboolean clutter_stage_view_is_dirty_projection (ClutterStageView *view);

void clutter_stage_view_set_dirty_projection (ClutterStageView *view,
                                              gboolean          dirty);

void clutter_stage_view_add_redraw_clip (ClutterStageView            *view,
                                         const cairo_rectangle_int_t *clip);

gboolean clutter_stage_view_has_full_redraw_clip (ClutterStageView *view);

gboolean clutter_stage_view_has_redraw_clip (ClutterStageView *view);

const cairo_region_t * clutter_stage_view_peek_redraw_clip (ClutterStageView *view);

cairo_region_t * clutter_stage_view_take_redraw_clip (ClutterStageView *view);

#endif /* __CLUTTER_STAGE_VIEW_PRIVATE_H__ */
