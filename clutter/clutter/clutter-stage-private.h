/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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

#ifndef __CLUTTER_STAGE_PRIVATE_H__
#define __CLUTTER_STAGE_PRIVATE_H__

#include <clutter/clutter-stage-window.h>
#include <clutter/clutter-stage.h>
#include <clutter/clutter-input-device.h>
#include <clutter/clutter-private.h>

#include <cogl/cogl.h>

G_BEGIN_DECLS

typedef struct _ClutterStageQueueRedrawEntry ClutterStageQueueRedrawEntry;

/* stage */
ClutterStageWindow *_clutter_stage_get_default_window    (void);

void                clutter_stage_paint_view             (ClutterStage          *stage,
                                                          ClutterStageView      *view,
                                                          const cairo_region_t  *redraw_clip);

void                _clutter_stage_emit_after_paint      (ClutterStage          *stage);

CLUTTER_EXPORT
void                _clutter_stage_set_window            (ClutterStage          *stage,
                                                          ClutterStageWindow    *stage_window);
CLUTTER_EXPORT
ClutterStageWindow *_clutter_stage_get_window            (ClutterStage          *stage);
void                _clutter_stage_get_projection_matrix (ClutterStage          *stage,
                                                          CoglMatrix            *projection);
void                _clutter_stage_dirty_projection      (ClutterStage          *stage);
void                _clutter_stage_set_viewport          (ClutterStage          *stage,
                                                          float                  x,
                                                          float                  y,
                                                          float                  width,
                                                          float                  height);
void                _clutter_stage_get_viewport          (ClutterStage          *stage,
                                                          float                 *x,
                                                          float                 *y,
                                                          float                 *width,
                                                          float                 *height);
void                _clutter_stage_dirty_viewport        (ClutterStage          *stage);
void                _clutter_stage_maybe_setup_viewport  (ClutterStage          *stage,
                                                          ClutterStageView      *view);
void                _clutter_stage_maybe_relayout        (ClutterActor          *stage);
gboolean            _clutter_stage_needs_update          (ClutterStage          *stage);
gboolean            _clutter_stage_do_update             (ClutterStage          *stage);

CLUTTER_EXPORT
void     _clutter_stage_queue_event                       (ClutterStage *stage,
                                                           ClutterEvent *event,
                                                           gboolean      copy_event);
gboolean _clutter_stage_has_queued_events                 (ClutterStage *stage);
void     _clutter_stage_process_queued_events             (ClutterStage *stage);
void     _clutter_stage_update_input_devices              (ClutterStage *stage);
gint64    _clutter_stage_get_update_time                  (ClutterStage *stage);
void     _clutter_stage_clear_update_time                 (ClutterStage *stage);
gboolean _clutter_stage_has_full_redraw_queued            (ClutterStage *stage);
int64_t  _clutter_stage_get_next_presentation_time        (ClutterStage *stage);

void clutter_stage_log_pick (ClutterStage           *stage,
                             const graphene_point_t *vertices,
                             ClutterActor           *actor);

void clutter_stage_push_pick_clip (ClutterStage           *stage,
                                   const graphene_point_t *vertices);

void clutter_stage_pop_pick_clip (ClutterStage *stage);

ClutterActor *_clutter_stage_do_pick (ClutterStage    *stage,
                                      float            x,
                                      float            y,
                                      ClutterPickMode  mode);

ClutterPaintVolume *_clutter_stage_paint_volume_stack_allocate (ClutterStage *stage);
void                _clutter_stage_paint_volume_stack_free_all (ClutterStage *stage);

const ClutterPlane *_clutter_stage_get_clip (ClutterStage *stage);

ClutterStageQueueRedrawEntry *_clutter_stage_queue_actor_redraw            (ClutterStage                 *stage,
                                                                            ClutterStageQueueRedrawEntry *entry,
                                                                            ClutterActor                 *actor,
                                                                            const ClutterPaintVolume     *clip);
void                          _clutter_stage_queue_redraw_entry_invalidate (ClutterStageQueueRedrawEntry *entry);

void            _clutter_stage_add_pointer_drag_actor    (ClutterStage       *stage,
                                                          ClutterInputDevice *device,
                                                          ClutterActor       *actor);
ClutterActor *  _clutter_stage_get_pointer_drag_actor    (ClutterStage       *stage,
                                                          ClutterInputDevice *device);
void            _clutter_stage_remove_pointer_drag_actor (ClutterStage       *stage,
                                                          ClutterInputDevice *device);

void            _clutter_stage_add_touch_drag_actor    (ClutterStage         *stage,
                                                        ClutterEventSequence *sequence,
                                                        ClutterActor         *actor);
ClutterActor *  _clutter_stage_get_touch_drag_actor    (ClutterStage         *stage,
                                                        ClutterEventSequence *sequence);
void            _clutter_stage_remove_touch_drag_actor (ClutterStage         *stage,
                                                        ClutterEventSequence *sequence);

CLUTTER_EXPORT
ClutterStageState       _clutter_stage_get_state        (ClutterStage      *stage);
CLUTTER_EXPORT
gboolean                _clutter_stage_is_activated     (ClutterStage      *stage);
CLUTTER_EXPORT
gboolean                _clutter_stage_update_state     (ClutterStage      *stage,
                                                         ClutterStageState  unset_state,
                                                         ClutterStageState  set_state);

void                    _clutter_stage_set_scale_factor (ClutterStage      *stage,
                                                         int                factor);
gboolean                _clutter_stage_get_max_view_scale_factor_for_rect (ClutterStage    *stage,
                                                                           graphene_rect_t *rect,
                                                                           float           *view_scale);

void            _clutter_stage_presented                (ClutterStage      *stage,
                                                         CoglFrameEvent     frame_event,
                                                         ClutterFrameInfo  *frame_info);

GList *         _clutter_stage_peek_stage_views         (ClutterStage *stage);

void            clutter_stage_queue_actor_relayout      (ClutterStage *stage,
                                                         ClutterActor *actor);

G_END_DECLS

#endif /* __CLUTTER_STAGE_PRIVATE_H__ */
