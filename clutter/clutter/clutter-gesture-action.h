/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 * Copyright (C) 2011  Robert Bosch Car Multimedia GmbH.
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
 *   Tomeu Vizoso <tomeu.vizoso@collabora.co.uk>
 */

#ifndef __CLUTTER_GESTURE_ACTION_H__
#define __CLUTTER_GESTURE_ACTION_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-action.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_GESTURE_ACTION               (clutter_gesture_action_get_type ())
#define CLUTTER_GESTURE_ACTION(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_GESTURE_ACTION, ClutterGestureAction))
#define CLUTTER_IS_GESTURE_ACTION(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_GESTURE_ACTION))
#define CLUTTER_GESTURE_ACTION_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_GESTURE_ACTION, ClutterGestureActionClass))
#define CLUTTER_IS_GESTURE_ACTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_GESTURE_ACTION))
#define CLUTTER_GESTURE_ACTION_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_GESTURE_ACTION, ClutterGestureActionClass))

typedef struct _ClutterGestureAction              ClutterGestureAction;
typedef struct _ClutterGestureActionPrivate       ClutterGestureActionPrivate;
typedef struct _ClutterGestureActionClass         ClutterGestureActionClass;

/**
 * ClutterGestureAction:
 *
 * The #ClutterGestureAction structure contains
 * only private data and should be accessed using the provided API
 *
 * Since: 1.8
 */
struct _ClutterGestureAction
{
  /*< private >*/
  ClutterAction parent_instance;

  ClutterGestureActionPrivate *priv;
};

/**
 * ClutterGestureActionClass:
 * @gesture_begin: class handler for the #ClutterGestureAction::gesture-begin signal
 * @gesture_progress: class handler for the #ClutterGestureAction::gesture-progress signal
 * @gesture_end: class handler for the #ClutterGestureAction::gesture-end signal
 * @gesture_cancel: class handler for the #ClutterGestureAction::gesture-cancel signal
 * @gesture_prepare: virtual function called before emitting the
 *   #ClutterGestureAction::gesture-cancel signal
 *
 * The #ClutterGestureClass structure contains only
 * private data.
 *
 * Since: 1.8
 */
struct _ClutterGestureActionClass
{
  /*< private >*/
  ClutterActionClass parent_class;

  /*< public >*/
  gboolean (* gesture_begin)    (ClutterGestureAction  *action,
                                 ClutterActor          *actor);
  gboolean (* gesture_progress) (ClutterGestureAction  *action,
                                 ClutterActor          *actor);
  void     (* gesture_end)      (ClutterGestureAction  *action,
                                 ClutterActor          *actor);
  void     (* gesture_cancel)   (ClutterGestureAction  *action,
                                 ClutterActor          *actor);
  gboolean (* gesture_prepare)  (ClutterGestureAction  *action,
                                 ClutterActor          *actor);

  /*< private >*/
  void (* _clutter_gesture_action1) (void);
  void (* _clutter_gesture_action2) (void);
  void (* _clutter_gesture_action3) (void);
  void (* _clutter_gesture_action4) (void);
  void (* _clutter_gesture_action5) (void);
  void (* _clutter_gesture_action6) (void);
};

CLUTTER_EXPORT
GType clutter_gesture_action_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterAction *        clutter_gesture_action_new                      (void);

CLUTTER_EXPORT
gint                   clutter_gesture_action_get_n_touch_points   (ClutterGestureAction *action);
CLUTTER_EXPORT
void                   clutter_gesture_action_set_n_touch_points   (ClutterGestureAction *action,
                                                                    gint                  nb_points);
CLUTTER_EXPORT
void                   clutter_gesture_action_get_press_coords     (ClutterGestureAction *action,
                                                                    guint                 point,
                                                                    gfloat               *press_x,
                                                                    gfloat               *press_y);
CLUTTER_EXPORT
void                   clutter_gesture_action_get_motion_coords    (ClutterGestureAction *action,
                                                                    guint                 point,
                                                                    gfloat               *motion_x,
                                                                    gfloat               *motion_y);
CLUTTER_EXPORT
gfloat                 clutter_gesture_action_get_motion_delta     (ClutterGestureAction *action,
                                                                    guint                 point,
                                                                    gfloat               *delta_x,
                                                                    gfloat               *delta_y);
CLUTTER_EXPORT
void                   clutter_gesture_action_get_release_coords   (ClutterGestureAction *action,
                                                                    guint                 point,
                                                                    gfloat               *release_x,
                                                                    gfloat               *release_y);
CLUTTER_EXPORT
gfloat                 clutter_gesture_action_get_velocity         (ClutterGestureAction *action,
                                                                    guint                 point,
                                                                    gfloat               *velocity_x,
                                                                    gfloat               *velocity_y);

CLUTTER_EXPORT
guint                  clutter_gesture_action_get_n_current_points (ClutterGestureAction *action);

CLUTTER_EXPORT
ClutterEventSequence * clutter_gesture_action_get_sequence         (ClutterGestureAction *action,
                                                                    guint                 point);

CLUTTER_EXPORT
ClutterInputDevice *   clutter_gesture_action_get_device           (ClutterGestureAction *action,
                                                                    guint                 point);

CLUTTER_EXPORT
const ClutterEvent *   clutter_gesture_action_get_last_event       (ClutterGestureAction *action,
                                                                    guint                 point);

CLUTTER_EXPORT
void                   clutter_gesture_action_cancel               (ClutterGestureAction *action);

CLUTTER_EXPORT
void                            clutter_gesture_action_set_threshold_trigger_edge       (ClutterGestureAction      *action,
                                                                                         ClutterGestureTriggerEdge  edge);
CLUTTER_DEPRECATED_FOR(clutter_gesture_action_get_threshold_trigger_edge)
ClutterGestureTriggerEdge       clutter_gesture_action_get_threshold_trigger_egde       (ClutterGestureAction      *action);
CLUTTER_EXPORT
ClutterGestureTriggerEdge       clutter_gesture_action_get_threshold_trigger_edge       (ClutterGestureAction      *action);

CLUTTER_EXPORT
void                            clutter_gesture_action_set_threshold_trigger_distance   (ClutterGestureAction      *action,
                                                                                         float                      x,
                                                                                         float                      y);

CLUTTER_EXPORT
void                            clutter_gesture_action_get_threshold_trigger_distance   (ClutterGestureAction *action,
                                                                                         float                *x,
                                                                                         float                *y);

G_END_DECLS

#endif /* __CLUTTER_GESTURE_ACTION_H__ */
