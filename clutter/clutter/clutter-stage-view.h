/*
 * Copyright (C) 2016 Red Hat Inc.
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

#ifndef __CLUTTER_STAGE_VIEW_H__
#define __CLUTTER_STAGE_VIEW_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <cairo.h>
#include <glib-object.h>
#include <cogl/cogl.h>

#include "clutter-macros.h"

#define CLUTTER_TYPE_STAGE_VIEW (clutter_stage_view_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterStageView, clutter_stage_view,
                          CLUTTER, STAGE_VIEW,
                          GObject)

struct _ClutterStageViewClass
{
  GObjectClass parent_class;

  void (* setup_offscreen_blit_pipeline) (ClutterStageView *view,
                                          CoglPipeline     *pipeline);

  void (* get_offscreen_transformation_matrix) (ClutterStageView *view,
                                                CoglMatrix       *matrix);
};

CLUTTER_EXPORT
void clutter_stage_view_get_layout (ClutterStageView      *view,
                                    cairo_rectangle_int_t *rect);

CLUTTER_EXPORT
CoglFramebuffer *clutter_stage_view_get_framebuffer (ClutterStageView *view);
CLUTTER_EXPORT
CoglFramebuffer *clutter_stage_view_get_onscreen (ClutterStageView *view);
CLUTTER_EXPORT
void             clutter_stage_view_invalidate_offscreen_blit_pipeline (ClutterStageView *view);

CLUTTER_EXPORT
void             clutter_stage_view_transform_to_onscreen (ClutterStageView *view,
                                                           gfloat           *x,
                                                           gfloat           *y);

CLUTTER_EXPORT
float clutter_stage_view_get_scale (ClutterStageView *view);

CLUTTER_EXPORT
void clutter_stage_view_get_offscreen_transformation_matrix (ClutterStageView *view,
                                                             CoglMatrix       *matrix);

#endif /* __CLUTTER_STAGE_VIEW_H__ */
