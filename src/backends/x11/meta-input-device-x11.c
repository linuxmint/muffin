/*
 * Copyright © 2011  Intel Corp.
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

#include "config.h"

#include <X11/extensions/XInput2.h>

#include "clutter/clutter-mutter.h"
#include "clutter/x11/clutter-x11.h"
#include "backends/x11/meta-input-device-x11.h"

struct _MetaInputDeviceX11
{
  ClutterInputDevice device;

  int32_t device_id;
  ClutterInputDeviceTool *current_tool;

  int inhibit_pointer_query_timer;
  gboolean query_status;
  float current_x;
  float current_y;

#ifdef HAVE_LIBWACOM
  GArray *group_modes;
#endif
};

struct _MetaInputDeviceX11Class
{
  ClutterInputDeviceClass device_class;
};

#define N_BUTTONS       5

G_DEFINE_TYPE (MetaInputDeviceX11,
               meta_input_device_x11,
               META_TYPE_INPUT_DEVICE)

static void
meta_input_device_x11_constructed (GObject *object)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (object);

  g_object_get (object, "id", &device_xi2->device_id, NULL);

  if (G_OBJECT_CLASS (meta_input_device_x11_parent_class)->constructed)
    G_OBJECT_CLASS (meta_input_device_x11_parent_class)->constructed (object);

#ifdef HAVE_LIBWACOM
  if (clutter_input_device_get_device_type (CLUTTER_INPUT_DEVICE (object)) == CLUTTER_PAD_DEVICE)
    {
      device_xi2->group_modes = g_array_new (FALSE, TRUE, sizeof (uint32_t));
      g_array_set_size (device_xi2->group_modes,
                        clutter_input_device_get_n_mode_groups (CLUTTER_INPUT_DEVICE (object)));
    }
#endif
}

static gboolean
meta_input_device_x11_keycode_to_evdev (ClutterInputDevice *device,
                                        uint32_t            hardware_keycode,
                                        uint32_t           *evdev_keycode)
{
  /* When using evdev under X11 the hardware keycodes are the evdev
     keycodes plus 8. I haven't been able to find any documentation to
     know what the +8 is for. FIXME: This should probably verify that
     X server is using evdev. */
  *evdev_keycode = hardware_keycode - 8;

  return TRUE;
}

static gboolean
meta_input_device_x11_is_grouped (ClutterInputDevice *device,
                                  ClutterInputDevice *other_device)
{
#ifdef HAVE_LIBWACOM
  WacomDevice *wacom_device, *other_wacom_device;

  wacom_device =
    meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));
  other_wacom_device =
    meta_input_device_get_wacom_device (META_INPUT_DEVICE (other_device));

  if (wacom_device && other_wacom_device &&
      libwacom_compare (wacom_device,
                        other_wacom_device,
                        WCOMPARE_NORMAL) == 0)
    return TRUE;
#endif

  if (clutter_input_device_get_vendor_id (device) &&
      clutter_input_device_get_product_id (device) &&
      clutter_input_device_get_vendor_id (other_device) &&
      clutter_input_device_get_product_id (other_device))
    {
      if (strcmp (clutter_input_device_get_vendor_id (device),
                  clutter_input_device_get_vendor_id (other_device)) == 0 &&
          strcmp (clutter_input_device_get_product_id (device),
                  clutter_input_device_get_product_id (other_device)) == 0)
        return TRUE;
    }

  return FALSE;
}

static void
meta_input_device_x11_finalize (GObject *object)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (object);

#ifdef HAVE_LIBWACOM
  if (device_xi2->group_modes)
    g_array_unref (device_xi2->group_modes);
#endif

  g_clear_handle_id (&device_xi2->inhibit_pointer_query_timer, g_source_remove);

  G_OBJECT_CLASS (meta_input_device_x11_parent_class)->finalize (object);
}

static int
meta_input_device_x11_get_group_n_modes (ClutterInputDevice *device,
                                         int                 group)
{
#ifdef HAVE_LIBWACOM
  WacomDevice *wacom_device;

  wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

  if (wacom_device)
    {
      if (group == 0)
        {
          if (libwacom_has_ring (wacom_device))
            return libwacom_get_ring_num_modes (wacom_device);
          else if (libwacom_get_num_strips (wacom_device) >= 1)
            return libwacom_get_strips_num_modes (wacom_device);
        }
      else if (group == 1)
        {
          if (libwacom_has_ring2 (wacom_device))
            return libwacom_get_ring2_num_modes (wacom_device);
          else if (libwacom_get_num_strips (wacom_device) >= 2)
            return libwacom_get_strips_num_modes (wacom_device);
        }
    }
#endif

  return -1;
}

#ifdef HAVE_LIBWACOM
static int
meta_input_device_x11_get_button_group (ClutterInputDevice *device,
                                        uint32_t            button)
{
  WacomDevice *wacom_device;

  wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

  if (wacom_device)
    {
      WacomButtonFlags flags;

      if (button >= libwacom_get_num_buttons (wacom_device))
        return -1;

      flags = libwacom_get_button_flag (wacom_device, 'A' + button);

      if (flags &
          (WACOM_BUTTON_RING_MODESWITCH |
           WACOM_BUTTON_TOUCHSTRIP_MODESWITCH))
        return 0;
      if (flags &
          (WACOM_BUTTON_RING2_MODESWITCH |
           WACOM_BUTTON_TOUCHSTRIP2_MODESWITCH))
        return 1;
    }

  return -1;
}
#endif

static gboolean
meta_input_device_x11_is_mode_switch_button (ClutterInputDevice *device,
                                             uint32_t            group,
                                             uint32_t            button)
{
  int button_group = -1;

#ifdef HAVE_LIBWACOM
  button_group = meta_input_device_x11_get_button_group (device, button);
#endif

  return button_group == (int) group;
}

static void
meta_input_device_x11_class_init (MetaInputDeviceX11Class *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterInputDeviceClass *device_class = CLUTTER_INPUT_DEVICE_CLASS (klass);

  gobject_class->constructed = meta_input_device_x11_constructed;
  gobject_class->finalize = meta_input_device_x11_finalize;

  device_class->keycode_to_evdev = meta_input_device_x11_keycode_to_evdev;
  device_class->is_grouped = meta_input_device_x11_is_grouped;
  device_class->get_group_n_modes = meta_input_device_x11_get_group_n_modes;
  device_class->is_mode_switch_button = meta_input_device_x11_is_mode_switch_button;
}

static void
meta_input_device_x11_init (MetaInputDeviceX11 *self)
{
}

static ClutterModifierType
get_modifier_for_button (int i)
{
  switch (i)
    {
    case 1:
      return CLUTTER_BUTTON1_MASK;
    case 2:
      return CLUTTER_BUTTON2_MASK;
    case 3:
      return CLUTTER_BUTTON3_MASK;
    case 4:
      return CLUTTER_BUTTON4_MASK;
    case 5:
      return CLUTTER_BUTTON5_MASK;
    default:
      return 0;
    }
}

void
meta_input_device_x11_translate_state (ClutterEvent    *event,
                                       XIModifierState *modifiers_state,
                                       XIButtonState   *buttons_state,
                                       XIGroupState    *group_state)
{
  uint32_t button = 0;
  uint32_t base = 0;
  uint32_t latched = 0;
  uint32_t locked = 0;
  uint32_t effective;

  if (modifiers_state)
    {
      base = (uint32_t) modifiers_state->base;
      latched = (uint32_t) modifiers_state->latched;
      locked = (uint32_t) modifiers_state->locked;
    }

  if (buttons_state)
    {
      int len, i;

      len = MIN (N_BUTTONS, buttons_state->mask_len * 8);

      for (i = 0; i < len; i++)
        {
          if (!XIMaskIsSet (buttons_state->mask, i))
            continue;

          button |= get_modifier_for_button (i);
        }
    }

  /* The XIButtonState sent in the event specifies the
   * state of the buttons before the event. In order to
   * get the current state of the buttons, we need to
   * filter out the current button.
   */
  switch (event->type)
    {
    case CLUTTER_BUTTON_PRESS:
      button |=  (get_modifier_for_button (event->button.button));
      break;
    case CLUTTER_BUTTON_RELEASE:
      button &= ~(get_modifier_for_button (event->button.button));
      break;
    default:
      break;
    }

  effective = button | base | latched | locked;
  if (group_state)
    effective |= (group_state->effective) << 13;

  _clutter_event_set_state_full (event, button, base, latched, locked, effective);
}

void
meta_input_device_x11_update_tool (ClutterInputDevice     *device,
                                   ClutterInputDeviceTool *tool)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (device);
  g_set_object (&device_xi2->current_tool, tool);
}

ClutterInputDeviceTool *
meta_input_device_x11_get_current_tool (ClutterInputDevice *device)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (device);
  return device_xi2->current_tool;
}

static gboolean
meta_input_device_x11_query_pointer_location (MetaInputDeviceX11 *device_xi2)
{
  Window xroot_window, xchild_window;
  double xroot_x, xroot_y, xwin_x, xwin_y;
  XIButtonState button_state;
  XIModifierState mod_state;
  XIGroupState group_state;
  int result;

  clutter_x11_trap_x_errors ();
  result = XIQueryPointer (clutter_x11_get_default_display (),
                           device_xi2->device_id,
                           clutter_x11_get_root_window (),
                           &xroot_window,
                           &xchild_window,
                           &xroot_x, &xroot_y,
                           &xwin_x, &xwin_y,
                           &button_state,
                           &mod_state,
                           &group_state);
  clutter_x11_untrap_x_errors ();

  if (!result)
    return FALSE;

  device_xi2->current_x = (float) xroot_x;
  device_xi2->current_y = (float) xroot_y;

  return TRUE;
}

static gboolean
clear_inhibit_pointer_query_cb (gpointer data)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (data);

  device_xi2->inhibit_pointer_query_timer = 0;

  return G_SOURCE_REMOVE;
}

gboolean
meta_input_device_x11_get_pointer_location (ClutterInputDevice *device,
                                            float              *x,
                                            float              *y)

{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (device);

  g_return_val_if_fail (META_IS_INPUT_DEVICE_X11 (device), FALSE);
  g_return_val_if_fail (device->device_type == CLUTTER_POINTER_DEVICE, FALSE);

  /* Throttle XServer queries and roundtrips using an idle timeout */
  if (device_xi2->inhibit_pointer_query_timer == 0)
    {
      device_xi2->query_status =
        meta_input_device_x11_query_pointer_location (device_xi2);
      device_xi2->inhibit_pointer_query_timer =
        clutter_threads_add_idle (clear_inhibit_pointer_query_cb, device_xi2);
    }

  *x = device_xi2->current_x;
  *y = device_xi2->current_y;

  return device_xi2->query_status;
}

#ifdef HAVE_LIBWACOM
uint32_t
meta_input_device_x11_get_pad_group_mode (ClutterInputDevice *device,
                                          uint32_t            group)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (device);

  if (group >= device_xi2->group_modes->len)
    return 0;

  return g_array_index (device_xi2->group_modes, uint32_t, group);
}

static gboolean
pad_switch_mode (ClutterInputDevice *device,
                 uint32_t            button,
                 uint32_t            group,
                 uint32_t           *mode)
{
  MetaInputDeviceX11 *device_x11 = META_INPUT_DEVICE_X11 (device);
  uint32_t n_buttons, n_modes, button_group, next_mode, i;
  WacomDevice *wacom_device;
  GList *switch_buttons = NULL;

  wacom_device =
    meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));
  n_buttons = libwacom_get_num_buttons (wacom_device);

  for (i = 0; i < n_buttons; i++)
    {
      button_group = meta_input_device_x11_get_button_group (device, i);
      if (button_group == group)
        switch_buttons = g_list_prepend (switch_buttons, GINT_TO_POINTER (button));
    }

  switch_buttons = g_list_reverse (switch_buttons);
  n_modes = clutter_input_device_get_group_n_modes (device, group);

  if (g_list_length (switch_buttons) > 1)
    {
      /* If there's multiple switch buttons, we don't toggle but assign a mode
       * to each of those buttons.
       */
      next_mode = g_list_index (switch_buttons, GINT_TO_POINTER (button));
    }
  else if (switch_buttons)
    {
      uint32_t cur_mode;

      /* If there is a single button, have it toggle across modes */
      cur_mode = g_array_index (device_x11->group_modes, uint32_t, group);
      next_mode = (cur_mode + 1) % n_modes;
    }
  else
    {
      return FALSE;
    }

  g_list_free (switch_buttons);

  if (next_mode < 0 || next_mode > n_modes)
    return FALSE;

  *mode = next_mode;
  return TRUE;
}

void
meta_input_device_x11_update_pad_state (ClutterInputDevice *device,
                                        uint32_t            button,
                                        uint32_t            state,
                                        uint32_t           *group,
                                        uint32_t           *mode)
{
  MetaInputDeviceX11 *device_xi2 = META_INPUT_DEVICE_X11 (device);
  uint32_t button_group, *group_mode;

  button_group = meta_input_device_x11_get_button_group (device, button);

  if (button_group < 0 || button_group >= device_xi2->group_modes->len)
    {
      if (group)
        *group = 0;
      if (mode)
        *mode = 0;
      return;
    }

  group_mode = &g_array_index (device_xi2->group_modes, uint32_t, button_group);

  if (state)
    {
      uint32_t next_mode;

      if (pad_switch_mode (device, button, button_group, &next_mode))
        *group_mode = next_mode;
    }

  if (group)
    *group = button_group;
  if (mode)
    *mode = *group_mode;
}
#endif
