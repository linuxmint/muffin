/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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

#ifndef __CLUTTER_EVENT_H__
#define __CLUTTER_EVENT_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-types.h>
#include <clutter/clutter-input-device.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_EVENT	        (clutter_event_get_type ())
#define CLUTTER_TYPE_EVENT_SEQUENCE	(clutter_event_sequence_get_type ())

/**
 * CLUTTER_PRIORITY_EVENTS:
 *
 * Priority for event handling.
 *
 * Since: 0.4
 */
#define CLUTTER_PRIORITY_EVENTS         (G_PRIORITY_DEFAULT)

/**
 * CLUTTER_CURRENT_TIME:
 *
 * Default value for "now".
 *
 * Since: 0.4
 */
#define CLUTTER_CURRENT_TIME            (0L)

/**
 * CLUTTER_EVENT_PROPAGATE:
 *
 * Continues the propagation of an event; this macro should be
 * used in event-related signals.
 *
 * Since: 1.10
 */
#define CLUTTER_EVENT_PROPAGATE         (FALSE)

/**
 * CLUTTER_EVENT_STOP:
 *
 * Stops the propagation of an event; this macro should be used
 * in event-related signals.
 *
 * Since: 1.10
 */
#define CLUTTER_EVENT_STOP              (TRUE)

/**
 * CLUTTER_BUTTON_PRIMARY:
 *
 * The primary button of a pointer device.
 *
 * This is typically the left mouse button in a right-handed
 * mouse configuration.
 *
 * Since: 1.10
 */
#define CLUTTER_BUTTON_PRIMARY          (1)

/**
 * CLUTTER_BUTTON_MIDDLE:
 *
 * The middle button of a pointer device.
 *
 * Since: 1.10
 */
#define CLUTTER_BUTTON_MIDDLE           (2)

/**
 * CLUTTER_BUTTON_SECONDARY:
 *
 * The secondary button of a pointer device.
 *
 * This is typically the right mouse button in a right-handed
 * mouse configuration.
 *
 * Since: 1.10
 */
#define CLUTTER_BUTTON_SECONDARY        (3)

typedef struct _ClutterAnyEvent         ClutterAnyEvent;
typedef struct _ClutterButtonEvent      ClutterButtonEvent;
typedef struct _ClutterKeyEvent         ClutterKeyEvent;
typedef struct _ClutterMotionEvent      ClutterMotionEvent;
typedef struct _ClutterScrollEvent      ClutterScrollEvent;
typedef struct _ClutterStageStateEvent  ClutterStageStateEvent;
typedef struct _ClutterCrossingEvent    ClutterCrossingEvent;
typedef struct _ClutterTouchEvent       ClutterTouchEvent;
typedef struct _ClutterTouchpadPinchEvent ClutterTouchpadPinchEvent;
typedef struct _ClutterTouchpadSwipeEvent ClutterTouchpadSwipeEvent;
typedef struct _ClutterProximityEvent   ClutterProximityEvent;
typedef struct _ClutterPadButtonEvent   ClutterPadButtonEvent;
typedef struct _ClutterPadStripEvent    ClutterPadStripEvent;
typedef struct _ClutterPadRingEvent     ClutterPadRingEvent;
typedef struct _ClutterIMEvent          ClutterIMEvent;
typedef struct _ClutterDeviceEvent      ClutterDeviceEvent;

/**
 * ClutterAnyEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @source: event source actor
 *
 * Common members for a #ClutterEvent
 *
 * Since: 0.2
 */
struct _ClutterAnyEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;
};

/**
 * ClutterKeyEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor
 * @modifier_state: key modifiers
 * @keyval: raw key value
 * @hardware_keycode: raw hardware key value
 * @unicode_value: Unicode representation
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 *
 * Key event
 *
 * Since: 0.2
 */
struct _ClutterKeyEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  ClutterModifierType modifier_state;
  guint keyval;
  guint16 hardware_keycode;
  gunichar unicode_value;
  ClutterInputDevice *device;
};

/**
 * ClutterButtonEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor
 * @x: event X coordinate, relative to the stage
 * @y: event Y coordinate, relative to the stage
 * @modifier_state: button modifiers
 * @button: event button
 * @click_count: number of button presses within the default time
 *   and radius
 * @axes: reserved for future use
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 *
 * Button event.
 *
 * The event coordinates are relative to the stage that received the
 * event, and can be transformed into actor-relative coordinates by
 * using clutter_actor_transform_stage_point().
 *
 * Since: 0.2
 */
struct _ClutterButtonEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  gfloat x;
  gfloat y;
  ClutterModifierType modifier_state;
  guint32 button;
  guint click_count;
  gdouble *axes; /* Future use */
  ClutterInputDevice *device;
};

/**
 * ClutterProximityEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 *
 * Event for tool proximity in tablet devices
 *
 * Since: 1.28
 */
struct _ClutterProximityEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;
  ClutterInputDevice *device;
};

/**
 * ClutterCrossingEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor
 * @x: event X coordinate
 * @y: event Y coordinate
 * @related: actor related to the crossing
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 *
 * Event for the movement of the pointer across different actors
 *
 * Since: 0.2
 */
struct _ClutterCrossingEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  gfloat x;
  gfloat y;
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;
  ClutterActor *related;
};

/**
 * ClutterMotionEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor
 * @x: event X coordinate
 * @y: event Y coordinate
 * @modifier_state: button modifiers
 * @axes: reserved for future use
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 *
 * Event for the pointer motion
 *
 * Since: 0.2
 */
struct _ClutterMotionEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  gfloat x;
  gfloat y;
  ClutterModifierType modifier_state;
  gdouble *axes; /* Future use */
  ClutterInputDevice *device;
};

/**
 * ClutterScrollEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor
 * @x: event X coordinate
 * @y: event Y coordinate
 * @direction: direction of the scrolling
 * @modifier_state: button modifiers
 * @axes: reserved for future use
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 * @scroll_source: the source of scroll events. This field is available since 1.26
 * @finish_flags: the axes that were stopped in this event. This field is available since 1.26
 *
 * Scroll wheel (or similar device) event
 *
 * Since: 0.2
 */
struct _ClutterScrollEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  gfloat x;
  gfloat y;
  ClutterScrollDirection direction;
  ClutterModifierType modifier_state;
  gdouble *axes; /* future use */
  ClutterInputDevice *device;
  ClutterScrollSource scroll_source;
  ClutterScrollFinishFlags finish_flags;
};

/**
 * ClutterStageStateEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor (unused)
 * @changed_mask: bitwise OR of the changed flags
 * @new_state: bitwise OR of the current state flags
 *
 * Event signalling a change in the #ClutterStage state.
 *
 * Since: 0.2
 */
struct _ClutterStageStateEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source; /* XXX: should probably be the stage itself */

  ClutterStageState changed_mask;
  ClutterStageState new_state;
};

/**
 * ClutterTouchEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor (unused)
 * @x: the X coordinate of the pointer, relative to the stage
 * @y: the Y coordinate of the pointer, relative to the stage
 * @sequence: the event sequence that this event belongs to
 * @modifier_state: (type ClutterModifierType): a bit-mask representing the state
 *   of modifier keys (e.g. Control, Shift, and Alt) and the pointer
 *   buttons. See #ClutterModifierType
 * @axes: reserved 
 * @device: the device that originated the event. If you want the physical
 * device the event originated from, use clutter_event_get_source_device()
 *
 * Used for touch events.
 *
 * The @type field will be one of %CLUTTER_TOUCH_BEGIN, %CLUTTER_TOUCH_END,
 * %CLUTTER_TOUCH_UPDATE, or %CLUTTER_TOUCH_CANCEL.
 *
 * Touch events are grouped into sequences; each touch sequence will begin
 * with a %CLUTTER_TOUCH_BEGIN event, progress with %CLUTTER_TOUCH_UPDATE
 * events, and end either with a %CLUTTER_TOUCH_END event or with a
 * %CLUTTER_TOUCH_CANCEL event.
 *
 * With multi-touch capable devices there can be multiple event sequence
 * running at the same time.
 *
 * Since: 1.10
 */
struct _ClutterTouchEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  gfloat x;
  gfloat y;
  ClutterEventSequence *sequence;
  ClutterModifierType modifier_state;
  gdouble *axes; /* reserved */
  ClutterInputDevice *device;
};

/**
 * ClutterTouchpadPinchEvent:
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor (unused)
 * @phase: the current phase of the gesture
 * @x: the X coordinate of the pointer, relative to the stage
 * @y: the Y coordinate of the pointer, relative to the stage
 * @dx: movement delta of the pinch focal point in the X axis
 * @dy: movement delta of the pinch focal point in the Y axis
 * @angle_delta: angle delta in degrees, clockwise rotations are
 *   represented by positive deltas
 * @scale: the current scale
 *
 * Used for touchpad pinch gesture events. The current state of the
 * gesture will be determined by the @phase field.
 *
 * Each event with phase %CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN
 * will report a @scale of 1.0, all later phases in the gesture
 * report the current scale relative to the initial 1.0 value
 * (eg. 0.5 being half the size, 2.0 twice as big).
 *
 * Since: 1.24
 */
struct _ClutterTouchpadPinchEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  ClutterTouchpadGesturePhase phase;
  gfloat x;
  gfloat y;
  gfloat dx;
  gfloat dy;
  gfloat angle_delta;
  gfloat scale;
  guint n_fingers;
};

/**
 * ClutterTouchpadSwipeEvent
 * @type: event type
 * @time: event time
 * @flags: event flags
 * @stage: event source stage
 * @source: event source actor (unused)
 * @phase: the current phase of the gesture
 * @n_fingers: the number of fingers triggering the swipe
 * @x: the X coordinate of the pointer, relative to the stage
 * @y: the Y coordinate of the pointer, relative to the stage
 * @dx: movement delta of the pinch focal point in the X axis
 * @dy: movement delta of the pinch focal point in the Y axis
 *
 * Used for touchpad swipe gesture events. The current state of the
 * gesture will be determined by the @phase field.
 *
 * Since: 1.24
 */
struct _ClutterTouchpadSwipeEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  ClutterTouchpadGesturePhase phase;
  guint n_fingers;
  gfloat x;
  gfloat y;
  gfloat dx;
  gfloat dy;
};

struct _ClutterPadButtonEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  guint32 button;
  guint32 group;
  ClutterInputDevice *device;
  guint32 mode;
};

struct _ClutterPadStripEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  ClutterInputDevice *device;
  ClutterInputDevicePadSource strip_source;
  guint32 strip_number;
  guint32 group;
  gdouble value;
  guint32 mode;
};

struct _ClutterPadRingEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  ClutterInputDevice *device;
  ClutterInputDevicePadSource ring_source;
  guint32 ring_number;
  guint32 group;
  gdouble angle;
  guint32 mode;
};

struct _ClutterIMEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  char *text;
  int32_t offset;
  uint32_t len;
};

struct _ClutterDeviceEvent
{
  ClutterEventType type;
  guint32 time;
  ClutterEventFlags flags;
  ClutterStage *stage;
  ClutterActor *source;

  ClutterInputDevice *device;
};

/**
 * ClutterEvent:
 *
 * Generic event wrapper.
 *
 * Since: 0.2
 */
union _ClutterEvent
{
  /*< private >*/
  ClutterEventType type;

  ClutterAnyEvent any;
  ClutterButtonEvent button;
  ClutterKeyEvent key;
  ClutterMotionEvent motion;
  ClutterScrollEvent scroll;
  ClutterStageStateEvent stage_state;
  ClutterCrossingEvent crossing;
  ClutterTouchEvent touch;
  ClutterTouchpadPinchEvent touchpad_pinch;
  ClutterTouchpadSwipeEvent touchpad_swipe;
  ClutterProximityEvent proximity;
  ClutterPadButtonEvent pad_button;
  ClutterPadStripEvent pad_strip;
  ClutterPadRingEvent pad_ring;
  ClutterIMEvent im;
  ClutterDeviceEvent device;
};

/**
 * ClutterEventFilterFunc:
 * @event: the event that is going to be emitted
 * @user_data: the data pointer passed to clutter_event_add_filter()
 *
 * A function pointer type used by event filters that are added with
 * clutter_event_add_filter().
 *
 * Return value: %CLUTTER_EVENT_STOP to indicate that the event
 *   has been handled or %CLUTTER_EVENT_PROPAGATE otherwise.
 *   Returning %CLUTTER_EVENT_STOP skips any further filter
 *   functions and prevents the signal emission for the event.
 *
 * Since: 1.18
 */
typedef gboolean (* ClutterEventFilterFunc) (const ClutterEvent *event,
                                             gpointer            user_data);

CLUTTER_EXPORT
GType clutter_event_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
GType clutter_event_sequence_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
gboolean                clutter_events_pending                  (void);
CLUTTER_EXPORT
ClutterEvent *          clutter_event_get                       (void);
CLUTTER_EXPORT
ClutterEvent *          clutter_event_peek                      (void);
CLUTTER_EXPORT
void                    clutter_event_put                       (const ClutterEvent     *event);

CLUTTER_EXPORT
guint                   clutter_event_add_filter                (ClutterStage          *stage,
                                                                 ClutterEventFilterFunc func,
                                                                 GDestroyNotify         notify,
                                                                 gpointer               user_data);
CLUTTER_EXPORT
void                    clutter_event_remove_filter             (guint                  id);

CLUTTER_EXPORT
ClutterEvent *          clutter_event_new                       (ClutterEventType        type);
CLUTTER_EXPORT
ClutterEvent *          clutter_event_copy                      (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_free                      (ClutterEvent           *event);

CLUTTER_EXPORT
ClutterEventType        clutter_event_type                      (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_flags                 (ClutterEvent           *event,
                                                                 ClutterEventFlags       flags);
CLUTTER_EXPORT
ClutterEventFlags       clutter_event_get_flags                 (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_time                  (ClutterEvent           *event,
                                                                 guint32                 time_);
CLUTTER_EXPORT
guint32                 clutter_event_get_time                  (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_state                 (ClutterEvent           *event,
                                                                 ClutterModifierType     state);
CLUTTER_EXPORT
ClutterModifierType     clutter_event_get_state                 (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_get_state_full            (const ClutterEvent     *event,
								 ClutterModifierType    *button_state,
								 ClutterModifierType    *base_state,
								 ClutterModifierType    *latched_state,
								 ClutterModifierType    *locked_state,
								 ClutterModifierType    *effective_state);
CLUTTER_EXPORT
void                    clutter_event_set_device                (ClutterEvent           *event,
                                                                 ClutterInputDevice     *device);
CLUTTER_EXPORT
ClutterInputDevice *    clutter_event_get_device                (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_source_device         (ClutterEvent           *event,
                                                                 ClutterInputDevice     *device);

CLUTTER_EXPORT
ClutterInputDevice *    clutter_event_get_source_device         (const ClutterEvent     *event);

CLUTTER_EXPORT
void                    clutter_event_set_device_tool           (ClutterEvent           *event,
                                                                 ClutterInputDeviceTool *tool);
CLUTTER_EXPORT
ClutterInputDeviceTool *clutter_event_get_device_tool           (const ClutterEvent     *event);

CLUTTER_EXPORT
void                    clutter_event_set_source                (ClutterEvent           *event,
                                                                 ClutterActor           *actor);
CLUTTER_EXPORT
ClutterActor *          clutter_event_get_source                (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_stage                 (ClutterEvent           *event,
                                                                 ClutterStage           *stage);
CLUTTER_EXPORT
ClutterStage *          clutter_event_get_stage                 (const ClutterEvent     *event);
CLUTTER_EXPORT
gint                    clutter_event_get_device_id             (const ClutterEvent     *event);
CLUTTER_EXPORT
ClutterInputDeviceType  clutter_event_get_device_type           (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_coords                (ClutterEvent           *event,
                                                                 gfloat                  x,
                                                                 gfloat                  y);
CLUTTER_EXPORT
void                    clutter_event_get_coords                (const ClutterEvent     *event,
                                                                 gfloat                 *x,
                                                                 gfloat                 *y);
CLUTTER_EXPORT
void                    clutter_event_get_position              (const ClutterEvent     *event,
                                                                 graphene_point_t       *position);
CLUTTER_EXPORT
float                   clutter_event_get_distance              (const ClutterEvent     *source,
                                                                 const ClutterEvent     *target);
CLUTTER_EXPORT
double                  clutter_event_get_angle                 (const ClutterEvent     *source,
                                                                 const ClutterEvent     *target);
CLUTTER_EXPORT
gdouble *               clutter_event_get_axes                  (const ClutterEvent     *event,
                                                                 guint                  *n_axes);
CLUTTER_EXPORT
gboolean                clutter_event_has_shift_modifier        (const ClutterEvent     *event);
CLUTTER_EXPORT
gboolean                clutter_event_has_control_modifier      (const ClutterEvent     *event);
CLUTTER_EXPORT
gboolean                clutter_event_is_pointer_emulated       (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_key_symbol            (ClutterEvent           *event,
                                                                 guint                   key_sym);
CLUTTER_EXPORT
guint                   clutter_event_get_key_symbol            (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_key_code              (ClutterEvent           *event,
                                                                 guint16                 key_code);
CLUTTER_EXPORT
guint16                 clutter_event_get_key_code              (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_key_unicode           (ClutterEvent           *event,
                                                                 gunichar                key_unicode);
CLUTTER_EXPORT
gunichar                clutter_event_get_key_unicode           (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_button                (ClutterEvent           *event,
                                                                 guint32                 button);
CLUTTER_EXPORT
guint32                 clutter_event_get_button                (const ClutterEvent     *event);
CLUTTER_EXPORT
guint                   clutter_event_get_click_count           (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_related               (ClutterEvent           *event,
                                                                 ClutterActor           *actor);
CLUTTER_EXPORT
ClutterActor *          clutter_event_get_related               (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_scroll_direction      (ClutterEvent           *event,
                                                                 ClutterScrollDirection  direction);
CLUTTER_EXPORT
ClutterScrollDirection  clutter_event_get_scroll_direction      (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_set_scroll_delta          (ClutterEvent           *event,
                                                                 gdouble                 dx,
                                                                 gdouble                 dy);
CLUTTER_EXPORT
void                    clutter_event_get_scroll_delta          (const ClutterEvent     *event,
                                                                 gdouble                *dx,
                                                                 gdouble                *dy);

CLUTTER_EXPORT
ClutterEventSequence *  clutter_event_get_event_sequence        (const ClutterEvent     *event);

CLUTTER_EXPORT
guint32                 clutter_keysym_to_unicode               (guint                   keyval);
CLUTTER_EXPORT
guint                   clutter_unicode_to_keysym               (guint32                 wc);

CLUTTER_EXPORT
guint32                 clutter_get_current_event_time          (void);
CLUTTER_EXPORT
const ClutterEvent *    clutter_get_current_event               (void);

CLUTTER_EXPORT
guint                   clutter_event_get_touchpad_gesture_finger_count (const ClutterEvent  *event);

CLUTTER_EXPORT
gdouble                 clutter_event_get_gesture_pinch_angle_delta  (const ClutterEvent     *event);

CLUTTER_EXPORT
gdouble                 clutter_event_get_gesture_pinch_scale        (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterTouchpadGesturePhase clutter_event_get_gesture_phase          (const ClutterEvent     *event);

CLUTTER_EXPORT
void                    clutter_event_get_gesture_motion_delta       (const ClutterEvent     *event,
                                                                      gdouble                *dx,
                                                                      gdouble                *dy);

CLUTTER_EXPORT
ClutterScrollSource      clutter_event_get_scroll_source             (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterScrollFinishFlags clutter_event_get_scroll_finish_flags       (const ClutterEvent     *event);

CLUTTER_EXPORT
guint                    clutter_event_get_mode_group                (const ClutterEvent     *event);

CLUTTER_EXPORT
gboolean                 clutter_event_get_pad_event_details         (const ClutterEvent     *event,
                                                                      guint                  *number,
                                                                      guint                  *mode,
                                                                      gdouble                *value);


G_END_DECLS

#endif /* __CLUTTER_EVENT_H__ */
