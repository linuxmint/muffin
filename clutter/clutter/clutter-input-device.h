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

#ifndef __CLUTTER_INPUT_DEVICE_H__
#define __CLUTTER_INPUT_DEVICE_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-backend.h>
#include <clutter/clutter-types.h>
#include <clutter/clutter-seat.h>

G_BEGIN_DECLS

typedef void (*ClutterEmitInputDeviceEvent) (ClutterEvent       *event,
                                             ClutterInputDevice *device);

struct _ClutterInputDeviceClass
{
  GObjectClass parent_class;

  gboolean (* keycode_to_evdev) (ClutterInputDevice *device,
                                 guint               hardware_keycode,
                                 guint              *evdev_keycode);
  void (* update_from_tool) (ClutterInputDevice     *device,
                             ClutterInputDeviceTool *tool);

  gboolean (* is_mode_switch_button) (ClutterInputDevice *device,
                                      guint               group,
                                      guint               button);
  gint (* get_group_n_modes) (ClutterInputDevice *device,
                              gint                group);

  gboolean (* is_grouped) (ClutterInputDevice *device,
                           ClutterInputDevice *other_device);

  /* Keyboard accessbility */
  void (* process_kbd_a11y_event) (ClutterEvent               *event,
                                   ClutterInputDevice         *device,
                                   ClutterEmitInputDeviceEvent emit_event_func);
};

#define CLUTTER_TYPE_INPUT_DEVICE               (clutter_input_device_get_type ())
#define CLUTTER_INPUT_DEVICE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_INPUT_DEVICE, ClutterInputDevice))
#define CLUTTER_IS_INPUT_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_INPUT_DEVICE))
#define CLUTTER_INPUT_DEVICE_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_INPUT_DEVICE, ClutterInputDeviceClass))
#define CLUTTER_IS_INPUT_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_INPUT_DEVICE))
#define CLUTTER_INPUT_DEVICE_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_INPUT_DEVICE, ClutterInputDeviceClass))

/**
 * ClutterInputDevice:
 *
 * Generic representation of an input device. The actual contents of this
 * structure depend on the backend used.
 */
typedef struct _ClutterInputDeviceClass ClutterInputDeviceClass;

CLUTTER_EXPORT
GType clutter_input_device_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterInputDeviceType  clutter_input_device_get_device_type    (ClutterInputDevice  *device);
CLUTTER_EXPORT
gint                    clutter_input_device_get_device_id      (ClutterInputDevice  *device);

CLUTTER_EXPORT
gboolean                clutter_input_device_get_coords        (ClutterInputDevice   *device,
                                                                ClutterEventSequence *sequence,
                                                                graphene_point_t     *point);
CLUTTER_EXPORT
ClutterModifierType     clutter_input_device_get_modifier_state (ClutterInputDevice  *device);
CLUTTER_EXPORT
ClutterActor *          clutter_input_device_get_pointer_actor  (ClutterInputDevice  *device);
CLUTTER_EXPORT
ClutterStage *          clutter_input_device_get_pointer_stage  (ClutterInputDevice  *device);
CLUTTER_EXPORT
const gchar *           clutter_input_device_get_device_name    (ClutterInputDevice  *device);
CLUTTER_EXPORT
ClutterInputMode        clutter_input_device_get_device_mode    (ClutterInputDevice  *device);
CLUTTER_EXPORT
gboolean                clutter_input_device_get_has_cursor     (ClutterInputDevice  *device);
CLUTTER_EXPORT
void                    clutter_input_device_set_enabled        (ClutterInputDevice  *device,
                                                                 gboolean             enabled);
CLUTTER_EXPORT
gboolean                clutter_input_device_get_enabled        (ClutterInputDevice  *device);

CLUTTER_EXPORT
guint                   clutter_input_device_get_n_axes         (ClutterInputDevice  *device);
CLUTTER_EXPORT
ClutterInputAxis        clutter_input_device_get_axis           (ClutterInputDevice  *device,
                                                                 guint                index_);
CLUTTER_EXPORT
gboolean                clutter_input_device_get_axis_value     (ClutterInputDevice  *device,
                                                                 gdouble             *axes,
                                                                 ClutterInputAxis     axis,
                                                                 gdouble             *value);

CLUTTER_EXPORT
guint                   clutter_input_device_get_n_keys         (ClutterInputDevice  *device);
CLUTTER_EXPORT
void                    clutter_input_device_set_key            (ClutterInputDevice  *device,
                                                                 guint                index_,
                                                                 guint                keyval,
                                                                 ClutterModifierType  modifiers);
CLUTTER_EXPORT
gboolean                clutter_input_device_get_key            (ClutterInputDevice  *device,
                                                                 guint                index_,
                                                                 guint               *keyval,
                                                                 ClutterModifierType *modifiers);

CLUTTER_EXPORT
ClutterInputDevice *    clutter_input_device_get_associated_device (ClutterInputDevice *device);
CLUTTER_EXPORT
GList *                 clutter_input_device_get_slave_devices  (ClutterInputDevice  *device);

CLUTTER_EXPORT
void                    clutter_input_device_update_from_event  (ClutterInputDevice  *device,
                                                                 ClutterEvent        *event,
                                                                 gboolean             update_stage);

CLUTTER_EXPORT
void                    clutter_input_device_grab               (ClutterInputDevice  *device,
                                                                 ClutterActor        *actor);
CLUTTER_EXPORT
void                    clutter_input_device_ungrab             (ClutterInputDevice  *device);
CLUTTER_EXPORT
ClutterActor *          clutter_input_device_get_grabbed_actor  (ClutterInputDevice  *device);

CLUTTER_EXPORT
void                    clutter_input_device_sequence_grab      (ClutterInputDevice   *device,
                                                                 ClutterEventSequence *sequence,
                                                                 ClutterActor         *actor);
CLUTTER_EXPORT
void                    clutter_input_device_sequence_ungrab    (ClutterInputDevice   *device,
                                                                 ClutterEventSequence *sequence);
CLUTTER_EXPORT
ClutterActor *          clutter_input_device_sequence_get_grabbed_actor (ClutterInputDevice   *device,
                                                                         ClutterEventSequence *sequence);

CLUTTER_EXPORT
gboolean                clutter_input_device_keycode_to_evdev   (ClutterInputDevice *device,
                                                                 guint               hardware_keycode,
                                                                 guint              *evdev_keycode);

CLUTTER_EXPORT
const gchar *           clutter_input_device_get_vendor_id      (ClutterInputDevice *device);
CLUTTER_EXPORT
const gchar *           clutter_input_device_get_product_id     (ClutterInputDevice *device);

CLUTTER_EXPORT
gint                    clutter_input_device_get_n_rings        (ClutterInputDevice *device);
CLUTTER_EXPORT
gint                    clutter_input_device_get_n_strips       (ClutterInputDevice *device);
CLUTTER_EXPORT
gint                    clutter_input_device_get_n_mode_groups  (ClutterInputDevice *device);

CLUTTER_EXPORT
gint                    clutter_input_device_get_group_n_modes  (ClutterInputDevice *device,
                                                                 gint                group);

CLUTTER_EXPORT
gboolean                clutter_input_device_is_mode_switch_button (ClutterInputDevice *device,
                                                                    guint               group,
								    guint               button);
CLUTTER_EXPORT
gint                    clutter_input_device_get_mode_switch_button_group (ClutterInputDevice *device,
                                                                           guint               button);

CLUTTER_EXPORT
const gchar *           clutter_input_device_get_device_node    (ClutterInputDevice *device);

CLUTTER_EXPORT
ClutterInputDeviceMapping clutter_input_device_get_mapping_mode (ClutterInputDevice *device);

CLUTTER_EXPORT
void                      clutter_input_device_set_mapping_mode (ClutterInputDevice        *device,
                                                                 ClutterInputDeviceMapping  mapping);
CLUTTER_EXPORT
gboolean                  clutter_input_device_is_grouped       (ClutterInputDevice *device,
                                                                 ClutterInputDevice *other_device);
CLUTTER_EXPORT
ClutterSeat *             clutter_input_device_get_seat         (ClutterInputDevice *device);

G_END_DECLS

#endif /* __CLUTTER_INPUT_DEVICE_H__ */
