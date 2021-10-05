/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright © 2009, 2010, 2011  Intel Corp.
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef CLUTTER_INPUT_DEVICE_PRIVATE_H
#define CLUTTER_INPUT_DEVICE_PRIVATE_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-input-device.h>

G_BEGIN_DECLS

typedef struct _ClutterAxisInfo
{
  ClutterInputAxis axis;

  double min_axis;
  double max_axis;

  double min_value;
  double max_value;

  double resolution;
} ClutterAxisInfo;

typedef struct _ClutterKeyInfo
{
  guint keyval;
  ClutterModifierType modifiers;
} ClutterKeyInfo;

typedef struct _ClutterScrollInfo
{
  guint axis_id;
  ClutterScrollDirection direction;
  double increment;

  double last_value;
  guint last_value_valid : 1;
} ClutterScrollInfo;

typedef struct _ClutterTouchInfo
{
  ClutterEventSequence *sequence;
  ClutterActor *actor;

  float current_x;
  float current_y;
} ClutterTouchInfo;

typedef struct _ClutterPtrA11yData
{
  int n_btn_pressed;
  float current_x;
  float current_y;

  float dwell_x;
  float dwell_y;
  gboolean dwell_drag_started;
  gboolean dwell_gesture_started;
  guint dwell_timer;
  guint dwell_position_timer;

  guint secondary_click_timer;
  gboolean secondary_click_triggered;
} ClutterPtrA11yData;

struct _ClutterInputDevice
{
  GObject parent_instance;

  int id;

  ClutterInputDeviceType device_type;
  ClutterInputMode device_mode;

  char *device_name;

  ClutterSeat *seat;

  ClutterBackend *backend;

  /* the associated device */
  ClutterInputDevice *associated;

  GList *slaves;

  /* the actor underneath the pointer */
  ClutterActor *cursor_actor;
  GHashTable   *inv_touch_sequence_actors;

  /* the actor that has a grab in place for the device */
  ClutterActor *pointer_grab_actor;
  ClutterActor *keyboard_grab_actor;
  GHashTable   *sequence_grab_actors;
  GHashTable   *inv_sequence_grab_actors;

  /* the current click count */
  int click_count;

  /* the stage the device is on */
  ClutterStage *stage;

  /* the current state */
  float current_x;
  float current_y;
  uint32_t current_time;
  int current_button_number;
  ClutterModifierType current_state;

  /* the current touch points states */
  GHashTable *touch_sequences_info;

  /* the previous state, used for click count generation */
  int previous_x;
  int previous_y;
  uint32_t previous_time;
  int previous_button_number;
  ClutterModifierType previous_state;

  GArray *axes;

  guint n_keys;
  GArray *keys;

  GArray *scroll_info;

  char *vendor_id;
  char *product_id;
  char *node_path;

  GPtrArray *tools;

  int n_rings;
  int n_strips;
  int n_mode_groups;

  ClutterInputDeviceMapping mapping_mode;

  guint has_cursor : 1;
  guint is_enabled : 1;

  /* Accessiblity */
  ClutterVirtualInputDevice *accessibility_virtual_device;
  ClutterPtrA11yData *ptr_a11y_data;
};

CLUTTER_EXPORT
void _clutter_input_device_set_associated_device (ClutterInputDevice *device,
                                                  ClutterInputDevice *associated);
CLUTTER_EXPORT
void _clutter_input_device_add_slave (ClutterInputDevice *master,
                                      ClutterInputDevice *slave);
CLUTTER_EXPORT
void _clutter_input_device_remove_slave (ClutterInputDevice *master,
                                         ClutterInputDevice *slave);
CLUTTER_EXPORT
void clutter_input_device_update_from_tool (ClutterInputDevice     *device,
                                            ClutterInputDeviceTool *tool);
CLUTTER_EXPORT
ClutterStage * _clutter_input_device_get_stage (ClutterInputDevice *device);
CLUTTER_EXPORT
void _clutter_input_device_set_stage (ClutterInputDevice *device,
                                      ClutterStage       *stage);
CLUTTER_EXPORT
void _clutter_input_device_set_coords (ClutterInputDevice   *device,
                                       ClutterEventSequence *sequence,
                                       gfloat                x,
                                       gfloat                y,
                                       ClutterStage         *stage);
CLUTTER_EXPORT
void _clutter_input_device_set_state (ClutterInputDevice  *device,
                                      ClutterModifierType  state);
CLUTTER_EXPORT
void _clutter_input_device_set_time (ClutterInputDevice *device,
                                     guint32             time_);
void _clutter_input_device_set_actor (ClutterInputDevice   *device,
                                      ClutterEventSequence *sequence,
                                      ClutterActor         *actor,
                                      gboolean              emit_crossing);
CLUTTER_EXPORT
ClutterActor * clutter_input_device_update (ClutterInputDevice   *device,
					    ClutterEventSequence *sequence,
					    gboolean              emit_crossing);
CLUTTER_EXPORT
void _clutter_input_device_add_event_sequence (ClutterInputDevice *device,
                                               ClutterEvent       *event);
CLUTTER_EXPORT
void _clutter_input_device_remove_event_sequence (ClutterInputDevice *device,
                                                  ClutterEvent       *event);
CLUTTER_EXPORT
void _clutter_input_device_set_n_keys (ClutterInputDevice *device,
                                       guint               n_keys);
CLUTTER_EXPORT
gboolean _clutter_input_device_translate_axis (ClutterInputDevice *device,
                                               guint               index_,
                                               gdouble             value,
                                               gdouble            *axis_value);
CLUTTER_EXPORT
guint _clutter_input_device_add_axis (ClutterInputDevice *device,
                                      ClutterInputAxis    axis,
                                      gdouble             minimum,
                                      gdouble             maximum,
                                      gdouble             resolution);

CLUTTER_EXPORT
void _clutter_input_device_reset_axes (ClutterInputDevice *device);

CLUTTER_EXPORT
void _clutter_input_device_add_scroll_info (ClutterInputDevice     *device,
                                            guint                   index_,
                                            ClutterScrollDirection  direction,
                                            gdouble                 increment);
CLUTTER_EXPORT
gboolean _clutter_input_device_get_scroll_delta (ClutterInputDevice     *device,
                                                 guint                   index_,
                                                 gdouble                 value,
                                                 ClutterScrollDirection *direction_p,
                                                 gdouble                *delta_p);
CLUTTER_EXPORT
void _clutter_input_device_reset_scroll_info (ClutterInputDevice *device);

CLUTTER_EXPORT
void clutter_input_device_add_tool (ClutterInputDevice     *device,
                                    ClutterInputDeviceTool *tool);

CLUTTER_EXPORT
ClutterInputDeviceTool *
   clutter_input_device_lookup_tool (ClutterInputDevice         *device,
                                     guint64                     serial,
                                     ClutterInputDeviceToolType  type);

#endif /* CLUTTER_INPUT_DEVICE_PRIVATE_H */
