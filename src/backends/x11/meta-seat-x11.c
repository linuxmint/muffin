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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#include "config.h"

#include <X11/extensions/XInput2.h>

#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-input-device-tool-x11.h"
#include "backends/x11/meta-input-device-x11.h"
#include "backends/x11/meta-keymap-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "backends/x11/meta-virtual-input-device-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "clutter/clutter-mutter.h"
#include "clutter/x11/clutter-x11.h"
#include "core/bell.h"
#include "meta-seat-x11.h"

enum
{
  PROP_0,
  PROP_OPCODE,
  PROP_POINTER_ID,
  PROP_KEYBOARD_ID,
  N_PROPS,

  /* This property is overridden */
  PROP_TOUCH_MODE,
};

struct _MetaSeatX11
{
  ClutterSeat parent_instance;
  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;
  GList *devices;
  GHashTable *devices_by_id;
  GHashTable *tools_by_serial;
  MetaKeymapX11 *keymap;

  int pointer_id;
  int keyboard_id;
  int opcode;
  guint has_touchscreens : 1;
  guint touch_mode : 1;
};

static GParamSpec *props[N_PROPS] = { 0 };

G_DEFINE_TYPE (MetaSeatX11, meta_seat_x11, CLUTTER_TYPE_SEAT)

static const char *clutter_input_axis_atom_names[] = {
  "Abs X",              /* CLUTTER_INPUT_AXIS_X */
  "Abs Y",              /* CLUTTER_INPUT_AXIS_Y */
  "Abs Pressure",       /* CLUTTER_INPUT_AXIS_PRESSURE */
  "Abs Tilt X",         /* CLUTTER_INPUT_AXIS_XTILT */
  "Abs Tilt Y",         /* CLUTTER_INPUT_AXIS_YTILT */
  "Abs Wheel",          /* CLUTTER_INPUT_AXIS_WHEEL */
  "Abs Distance",       /* CLUTTER_INPUT_AXIS_DISTANCE */
};

static const char *wacom_type_atoms[] = {
    "STYLUS",
    "CURSOR",
    "ERASER",
    "PAD",
    "TOUCH"
};
#define N_WACOM_TYPE_ATOMS G_N_ELEMENTS (wacom_type_atoms)

enum
{
    WACOM_TYPE_STYLUS,
    WACOM_TYPE_CURSOR,
    WACOM_TYPE_ERASER,
    WACOM_TYPE_PAD,
    WACOM_TYPE_TOUCH,
};

enum
{
  PAD_AXIS_FIRST  = 3, /* First axes are always x/y/pressure, ignored in pads */
  PAD_AXIS_STRIP1 = PAD_AXIS_FIRST,
  PAD_AXIS_STRIP2,
  PAD_AXIS_RING1,
  PAD_AXIS_RING2,
};

#define N_AXIS_ATOMS    G_N_ELEMENTS (clutter_input_axis_atom_names)

static Atom clutter_input_axis_atoms[N_AXIS_ATOMS] = { 0, };

static void
translate_valuator_class (Display             *xdisplay,
                          ClutterInputDevice  *device,
                          XIValuatorClassInfo *class)
{
  static gboolean atoms_initialized = FALSE;
  ClutterInputAxis i, axis = CLUTTER_INPUT_AXIS_IGNORE;

  if (G_UNLIKELY (!atoms_initialized))
    {
      XInternAtoms (xdisplay,
                    (char **) clutter_input_axis_atom_names, N_AXIS_ATOMS,
                    False,
                    clutter_input_axis_atoms);

      atoms_initialized = TRUE;
    }

  for (i = 0;
       i < N_AXIS_ATOMS;
       i += 1)
    {
      if (clutter_input_axis_atoms[i] == class->label)
        {
          axis = i + 1;
          break;
        }
    }

  _clutter_input_device_add_axis (device, axis,
                                  class->min,
                                  class->max,
                                  class->resolution);

  g_debug ("Added axis '%s' (min:%.2f, max:%.2fd, res:%d) of device %d",
           clutter_input_axis_atom_names[axis],
           class->min,
           class->max,
           class->resolution,
           device->id);
}

static void
translate_device_classes (Display             *xdisplay,
                          ClutterInputDevice  *device,
                          XIAnyClassInfo     **classes,
                          int                  n_classes)
{
  int i;

  for (i = 0; i < n_classes; i++)
    {
      XIAnyClassInfo *class_info = classes[i];

      switch (class_info->type)
        {
        case XIKeyClass:
          {
            XIKeyClassInfo *key_info = (XIKeyClassInfo *) class_info;
            int j;

            _clutter_input_device_set_n_keys (device,
                                              key_info->num_keycodes);

            for (j = 0; j < key_info->num_keycodes; j++)
              {
                clutter_input_device_set_key (device, j,
                                              key_info->keycodes[i],
                                              0);
              }
          }
          break;

        case XIValuatorClass:
          translate_valuator_class (xdisplay, device,
                                    (XIValuatorClassInfo *) class_info);
          break;

        case XIScrollClass:
          {
            XIScrollClassInfo *scroll_info = (XIScrollClassInfo *) class_info;
            ClutterScrollDirection direction;

            if (scroll_info->scroll_type == XIScrollTypeVertical)
              direction = CLUTTER_SCROLL_DOWN;
            else
              direction = CLUTTER_SCROLL_RIGHT;

            g_debug ("Scroll valuator %d: %s, increment: %f",
                     scroll_info->number,
                     scroll_info->scroll_type == XIScrollTypeVertical
                     ? "vertical"
                     : "horizontal",
                     scroll_info->increment);

            _clutter_input_device_add_scroll_info (device,
                                                   scroll_info->number,
                                                   direction,
                                                   scroll_info->increment);
          }
          break;

        default:
          break;
        }
    }
}

static gboolean
is_touch_device (XIAnyClassInfo         **classes,
                 int                      n_classes,
                 ClutterInputDeviceType  *device_type,
                 uint32_t                *n_touch_points)
{
  int i;

  for (i = 0; i < n_classes; i++)
    {
      XITouchClassInfo *class = (XITouchClassInfo *) classes[i];

      if (class->type != XITouchClass)
        continue;

      if (class->num_touches > 0)
        {
          if (class->mode == XIDirectTouch)
            *device_type = CLUTTER_TOUCHSCREEN_DEVICE;
          else if (class->mode == XIDependentTouch)
            *device_type = CLUTTER_TOUCHPAD_DEVICE;
          else
            continue;

          *n_touch_points = class->num_touches;

          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
has_8bit_property (XIDeviceInfo      *info,
                   const char        *name)
{
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int rc, format;
  Atom type;
  Atom prop;

  prop = XInternAtom (clutter_x11_get_default_display (), name, True);
  if (prop == None)
    return FALSE;

  clutter_x11_trap_x_errors ();
  rc = XIGetProperty (clutter_x11_get_default_display (),
                      info->deviceid,
                      prop,
                      0, 1, False, XA_INTEGER, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  clutter_x11_untrap_x_errors ();

  /* We don't care about the data */
  XFree (data);

  if (rc != Success || type != XA_INTEGER || format != 8 || nitems != 1)
    return FALSE;

  return TRUE;
}

static gboolean
is_touchpad_device (XIDeviceInfo *info)
{
  return has_8bit_property (info, "libinput Tapping Enabled") ||
         has_8bit_property (info, "Synaptics Off");
}

static gboolean
get_device_ids (XIDeviceInfo  *info,
                char         **vendor_id,
                char         **product_id)
{
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int rc, format;
  Atom type;

  clutter_x11_trap_x_errors ();
  rc = XIGetProperty (clutter_x11_get_default_display (),
                      info->deviceid,
                      XInternAtom (clutter_x11_get_default_display (), "Device Product ID", False),
                      0, 2, False, XA_INTEGER, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  clutter_x11_untrap_x_errors ();

  if (rc != Success || type != XA_INTEGER || format != 32 || nitems != 2)
    {
      XFree (data);
      return FALSE;
    }

  if (vendor_id)
    *vendor_id = g_strdup_printf ("%.4x", data[0]);
  if (product_id)
    *product_id = g_strdup_printf ("%.4x", data[1]);

  XFree (data);

  return TRUE;
}

static char *
get_device_node_path (XIDeviceInfo *info)
{
  gulong nitems, bytes_after;
  guchar *data;
  int rc, format;
  Atom prop, type;
  char *node_path;

  prop = XInternAtom (clutter_x11_get_default_display (), "Device Node", False);
  if (prop == None)
    return NULL;

  clutter_x11_trap_x_errors ();

  rc = XIGetProperty (clutter_x11_get_default_display (),
                      info->deviceid, prop, 0, 1024, False,
                      XA_STRING, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);

  if (clutter_x11_untrap_x_errors ())
    return NULL;

  if (rc != Success || type != XA_STRING || format != 8)
    {
      XFree (data);
      return FALSE;
    }

  node_path = g_strdup ((char *) data);
  XFree (data);

  return node_path;
}

static void
get_pad_features (XIDeviceInfo *info,
                  uint32_t     *n_rings,
                  uint32_t     *n_strips)
{
  int i, rings = 0, strips = 0;

  for (i = PAD_AXIS_FIRST; i < info->num_classes; i++)
    {
      XIValuatorClassInfo *valuator = (XIValuatorClassInfo*) info->classes[i];
      int axis = valuator->number;

      if (valuator->type != XIValuatorClass)
        continue;
      if (valuator->max <= 1)
        continue;

      /* Ring/strip axes are fixed in pad devices as handled by the
       * wacom driver. Match those to detect pad features.
       */
      if (axis == PAD_AXIS_STRIP1 || axis == PAD_AXIS_STRIP2)
        strips++;
      else if (axis == PAD_AXIS_RING1 || axis == PAD_AXIS_RING2)
        rings++;
    }

  *n_rings = rings;
  *n_strips = strips;
}

/* The Wacom driver exports the tool type as property. Use that over
   guessing based on the device name */
static gboolean
guess_source_from_wacom_type (XIDeviceInfo            *info,
                              ClutterInputDeviceType  *source_out)
{
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int rc, format;
  Atom type;
  Atom prop;
  Atom device_type;
  Atom types[N_WACOM_TYPE_ATOMS];

  prop = XInternAtom (clutter_x11_get_default_display (), "Wacom Tool Type", True);
  if (prop == None)
    return FALSE;

  clutter_x11_trap_x_errors ();
  rc = XIGetProperty (clutter_x11_get_default_display (),
                      info->deviceid,
                      prop,
                      0, 1, False, XA_ATOM, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  clutter_x11_untrap_x_errors ();

  if (rc != Success || type != XA_ATOM || format != 32 || nitems != 1)
    {
      XFree (data);
      return FALSE;
    }

  device_type = *data;
  XFree (data);

  if (device_type == 0)
      return FALSE;

  rc = XInternAtoms (clutter_x11_get_default_display (),
                     (char **)wacom_type_atoms,
                     N_WACOM_TYPE_ATOMS,
                     False,
                     types);
  if (rc == 0)
      return FALSE;

  if (device_type == types[WACOM_TYPE_STYLUS])
    {
      *source_out = CLUTTER_PEN_DEVICE;
    }
  else if (device_type == types[WACOM_TYPE_CURSOR])
    {
      *source_out = CLUTTER_CURSOR_DEVICE;
    }
  else if (device_type == types[WACOM_TYPE_ERASER])
    {
      *source_out = CLUTTER_ERASER_DEVICE;
    }
  else if (device_type == types[WACOM_TYPE_PAD])
    {
      *source_out = CLUTTER_PAD_DEVICE;
    }
  else if (device_type == types[WACOM_TYPE_TOUCH])
    {
        uint32_t num_touches = 0;

        if (!is_touch_device (info->classes, info->num_classes,
                              source_out, &num_touches))
            *source_out = CLUTTER_TOUCHSCREEN_DEVICE;
    }
  else
    {
      return FALSE;
    }

  return TRUE;
}

static ClutterInputDevice *
create_device (MetaSeatX11    *seat_x11,
               ClutterBackend *backend,
               XIDeviceInfo   *info)
{
  ClutterInputDeviceType source, touch_source;
  ClutterInputDevice *retval;
  ClutterInputMode mode;
  gboolean is_enabled;
  uint32_t num_touches = 0, num_rings = 0, num_strips = 0;
  char *vendor_id = NULL, *product_id = NULL, *node_path = NULL;

  if (info->use == XIMasterKeyboard || info->use == XISlaveKeyboard)
    {
      source = CLUTTER_KEYBOARD_DEVICE;
    }
  else if (is_touchpad_device (info))
    {
      source = CLUTTER_TOUCHPAD_DEVICE;
    }
  else if (info->use == XISlavePointer &&
           is_touch_device (info->classes, info->num_classes,
                            &touch_source,
                            &num_touches))
    {
      source = touch_source;
    }
  else if (!guess_source_from_wacom_type (info, &source))
    {
      char *name;

      name = g_ascii_strdown (info->name, -1);

      if (strstr (name, "eraser") != NULL)
        source = CLUTTER_ERASER_DEVICE;
      else if (strstr (name, "cursor") != NULL)
        source = CLUTTER_CURSOR_DEVICE;
      else if (strstr (name, " pad") != NULL)
        source = CLUTTER_PAD_DEVICE;
      else if (strstr (name, "wacom") != NULL || strstr (name, "pen") != NULL)
        source = CLUTTER_PEN_DEVICE;
      else if (strstr (name, "touchpad") != NULL)
        source = CLUTTER_TOUCHPAD_DEVICE;
      else
        source = CLUTTER_POINTER_DEVICE;

      g_free (name);
    }

  switch (info->use)
    {
    case XIMasterKeyboard:
    case XIMasterPointer:
      mode = CLUTTER_INPUT_MODE_MASTER;
      is_enabled = TRUE;
      break;

    case XISlaveKeyboard:
    case XISlavePointer:
      mode = CLUTTER_INPUT_MODE_SLAVE;
      is_enabled = FALSE;
      break;

    case XIFloatingSlave:
    default:
      mode = CLUTTER_INPUT_MODE_FLOATING;
      is_enabled = FALSE;
      break;
    }

  if (info->use != XIMasterKeyboard &&
      info->use != XIMasterPointer)
    {
      get_device_ids (info, &vendor_id, &product_id);
      node_path = get_device_node_path (info);
    }

  if (source == CLUTTER_PAD_DEVICE)
    {
      is_enabled = TRUE;
      get_pad_features (info, &num_rings, &num_strips);
    }

  retval = g_object_new (META_TYPE_INPUT_DEVICE_X11,
                         "name", info->name,
                         "id", info->deviceid,
                         "has-cursor", (info->use == XIMasterPointer),
                         "device-type", source,
                         "device-mode", mode,
                         "backend", backend,
                         "enabled", is_enabled,
                         "vendor-id", vendor_id,
                         "product-id", product_id,
                         "device-node", node_path,
                         "n-rings", num_rings,
                         "n-strips", num_strips,
                         "n-mode-groups", MAX (num_rings, num_strips),
                         "seat", seat_x11,
                         NULL);

  translate_device_classes (clutter_x11_get_default_display (), retval,
                            info->classes,
                            info->num_classes);

  g_free (vendor_id);
  g_free (product_id);
  g_free (node_path);

  g_debug ("Created device '%s' (id: %d, has-cursor: %s)",
           info->name,
           info->deviceid,
           info->use == XIMasterPointer ? "yes" : "no");

  return retval;
}

static void
pad_passive_button_grab (ClutterInputDevice *device)
{
  XIGrabModifiers xi_grab_mods = { XIAnyModifier, };
  XIEventMask xi_event_mask;
  int device_id, rc;

  device_id = clutter_input_device_get_device_id (device);

  xi_event_mask.deviceid = device_id;
  xi_event_mask.mask_len = XIMaskLen (XI_LASTEVENT);
  xi_event_mask.mask = g_new0 (unsigned char, xi_event_mask.mask_len);

  XISetMask (xi_event_mask.mask, XI_Motion);
  XISetMask (xi_event_mask.mask, XI_ButtonPress);
  XISetMask (xi_event_mask.mask, XI_ButtonRelease);

  clutter_x11_trap_x_errors ();
  rc = XIGrabButton (clutter_x11_get_default_display (),
                     device_id, XIAnyButton,
                     clutter_x11_get_root_window (), None,
                     XIGrabModeSync, XIGrabModeSync,
                     True, &xi_event_mask, 1, &xi_grab_mods);
  if (rc != 0)
    {
      g_warning ("Could not passively grab pad device: %s",
                 clutter_input_device_get_device_name (device));
    }
  else
    {
      XIAllowEvents (clutter_x11_get_default_display (),
                     device_id, XIAsyncDevice,
                     CLUTTER_CURRENT_TIME);
    }

  clutter_x11_untrap_x_errors ();

  g_free (xi_event_mask.mask);
}

static void
update_touch_mode (MetaSeatX11 *seat_x11)
{
  gboolean touch_mode;

  touch_mode = seat_x11->has_touchscreens;

  if (seat_x11->touch_mode == touch_mode)
    return;

  seat_x11->touch_mode = touch_mode;
  g_object_notify (G_OBJECT (seat_x11), "touch-mode");
}

static ClutterInputDevice *
add_device (MetaSeatX11    *seat_x11,
            ClutterBackend *backend,
            XIDeviceInfo   *info,
            gboolean        in_construction)
{
  ClutterInputDevice *device;

  device = create_device (seat_x11, backend, info);

  g_hash_table_replace (seat_x11->devices_by_id,
                        GINT_TO_POINTER (info->deviceid),
                        device);

  if (info->use == XIMasterPointer &&
      info->deviceid == seat_x11->pointer_id)
    {
      seat_x11->core_pointer = device;
    }
  else if (info->use == XIMasterKeyboard &&
           info->deviceid == seat_x11->keyboard_id)
    {
      seat_x11->core_keyboard = device;
    }
  else if ((info->use == XISlavePointer &&
            info->attachment == seat_x11->pointer_id) ||
           (info->use == XISlaveKeyboard &&
            info->attachment == seat_x11->keyboard_id))
    {
      seat_x11->devices = g_list_prepend (seat_x11->devices, device);
    }
  else
    {
      g_warning ("Unhandled device: %s",
                 clutter_input_device_get_device_name (device));
    }

  if (clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE)
    pad_passive_button_grab (device);

  /* relationships between devices and signal emissions are not
   * necessary while we're constructing the device manager instance
   */
  if (!in_construction)
    {
      if (info->use == XISlavePointer || info->use == XISlaveKeyboard)
        {
          ClutterInputDevice *master;

          master = g_hash_table_lookup (seat_x11->devices_by_id,
                                        GINT_TO_POINTER (info->attachment));
          _clutter_input_device_set_associated_device (device, master);
          _clutter_input_device_add_slave (master, device);
        }
    }

  return device;
}

static gboolean
has_touchscreens (MetaSeatX11 *seat_x11)
{
  GList *l;

  for (l = seat_x11->devices; l; l = l->next)
    {
      if (clutter_input_device_get_device_type (l->data) == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static void
remove_device (MetaSeatX11        *seat_x11,
               ClutterInputDevice *device)
{
  if (seat_x11->core_pointer == device)
    {
      seat_x11->core_pointer = NULL;
    }
  else if (seat_x11->core_keyboard == device)
    {
      seat_x11->core_keyboard = NULL;
    }
  else
    {
      seat_x11->devices = g_list_remove (seat_x11->devices, device);
    }
}

static gboolean
meta_seat_x11_handle_device_event (ClutterSeat  *seat,
                                   ClutterEvent *event)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  ClutterInputDevice *device = event->device.device;
  gboolean is_touch;

  is_touch =
    clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE;

  switch (event->type)
    {
      case CLUTTER_DEVICE_ADDED:
        seat_x11->has_touchscreens |= is_touch;
        break;
      case CLUTTER_DEVICE_REMOVED:
        if (is_touch)
          seat_x11->has_touchscreens = has_touchscreens (seat_x11);
        break;
      default:
        break;
    }

  if (is_touch)
    update_touch_mode (seat_x11);

  return TRUE;
}

static void
relate_masters (gpointer key,
                gpointer value,
                gpointer data)
{
  MetaSeatX11 *seat_x11 = data;
  ClutterInputDevice *device, *relative;

  device = g_hash_table_lookup (seat_x11->devices_by_id, key);
  relative = g_hash_table_lookup (seat_x11->devices_by_id, value);

  _clutter_input_device_set_associated_device (device, relative);
  _clutter_input_device_set_associated_device (relative, device);
}

static void
relate_slaves (gpointer key,
               gpointer value,
               gpointer data)
{
  MetaSeatX11 *seat_x11 = data;
  ClutterInputDevice *master, *slave;

  slave = g_hash_table_lookup (seat_x11->devices_by_id, key);
  master = g_hash_table_lookup (seat_x11->devices_by_id, value);

  _clutter_input_device_set_associated_device (slave, master);
  _clutter_input_device_add_slave (master, slave);
}

static uint
device_get_tool_serial (ClutterInputDevice *device)
{
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int serial_id = 0;
  int rc, format;
  Atom type;
  Atom prop;

  prop = XInternAtom (clutter_x11_get_default_display (), "Wacom Serial IDs", True);
  if (prop == None)
    return 0;

  clutter_x11_trap_x_errors ();
  rc = XIGetProperty (clutter_x11_get_default_display (),
                      clutter_input_device_get_device_id (device),
                      prop, 0, 4, FALSE, XA_INTEGER, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  clutter_x11_untrap_x_errors ();

  if (rc == Success && type == XA_INTEGER && format == 32 && nitems >= 4)
    serial_id = data[3];

  XFree (data);

  return serial_id;
}

static gboolean
translate_hierarchy_event (ClutterBackend   *backend,
                           MetaSeatX11      *seat_x11,
                           XIHierarchyEvent *ev,
                           ClutterEvent     *event)
{
  int i;
  gboolean retval = FALSE;

  for (i = 0; i < ev->num_info; i++)
    {
      if (ev->info[i].flags & XIDeviceEnabled &&
          !g_hash_table_lookup (seat_x11->devices_by_id,
                                GINT_TO_POINTER (ev->info[i].deviceid)))
        {
          XIDeviceInfo *info;
          int n_devices;

          g_debug ("Hierarchy event: device enabled");

          clutter_x11_trap_x_errors ();
          info = XIQueryDevice (clutter_x11_get_default_display (),
                                ev->info[i].deviceid,
                                &n_devices);
          clutter_x11_untrap_x_errors ();
          if (info != NULL)
            {
              ClutterInputDevice *device;

              device = add_device (seat_x11, backend, &info[0], FALSE);

              event->any.type = CLUTTER_DEVICE_ADDED;
              event->any.time = ev->time;
              clutter_event_set_device (event, device);

              retval = TRUE;
              XIFreeDeviceInfo (info);
            }
        }
      else if (ev->info[i].flags & XIDeviceDisabled)
        {
          g_autoptr (ClutterInputDevice) device = NULL;
          g_debug ("Hierarchy event: device disabled");

          g_hash_table_steal_extended (seat_x11->devices_by_id,
                                       GINT_TO_POINTER (ev->info[i].deviceid),
                                       NULL,
                                       (gpointer) &device);

          if (device != NULL)
            {
              remove_device (seat_x11, device);

              event->any.type = CLUTTER_DEVICE_REMOVED;
              event->any.time = ev->time;
              clutter_event_set_device (event, device);

              retval = TRUE;
            }
        }
      else if ((ev->info[i].flags & XISlaveAttached) ||
               (ev->info[i].flags & XISlaveDetached))
        {
          ClutterInputDevice *master, *slave;
          XIDeviceInfo *info;
          int n_devices;

          g_debug ("Hierarchy event: slave %s",
                   (ev->info[i].flags & XISlaveAttached)
                   ? "attached"
                   : "detached");

          slave = g_hash_table_lookup (seat_x11->devices_by_id,
                                       GINT_TO_POINTER (ev->info[i].deviceid));
          master = clutter_input_device_get_associated_device (slave);

          /* detach the slave in both cases */
          if (master != NULL)
            {
              _clutter_input_device_remove_slave (master, slave);
              _clutter_input_device_set_associated_device (slave, NULL);
            }

          /* and attach the slave to the new master if needed */
          if (ev->info[i].flags & XISlaveAttached)
            {
              clutter_x11_trap_x_errors ();
              info = XIQueryDevice (clutter_x11_get_default_display (),
                                    ev->info[i].deviceid,
                                    &n_devices);
              clutter_x11_untrap_x_errors ();
              if (info != NULL)
                {
                  master = g_hash_table_lookup (seat_x11->devices_by_id,
                                                GINT_TO_POINTER (info->attachment));
                  if (master != NULL)
                    {
                      _clutter_input_device_set_associated_device (slave, master);
                      _clutter_input_device_add_slave (master, slave);
                    }
                  XIFreeDeviceInfo (info);
                }
            }
        }
    }

  return retval;
}

static void
translate_property_event (MetaSeatX11 *seat_x11,
                          XIEvent     *event)
{
  XIPropertyEvent *xev = (XIPropertyEvent *) event;
  Atom serial_ids_prop = XInternAtom (clutter_x11_get_default_display (), "Wacom Serial IDs", True);
  ClutterInputDevice *device;

  device = g_hash_table_lookup (seat_x11->devices_by_id,
                                GINT_TO_POINTER (xev->deviceid));
  if (!device)
    return;

  if (xev->property == serial_ids_prop)
    {
      ClutterInputDeviceTool *tool = NULL;
      ClutterInputDeviceToolType type;
      int serial_id;

      serial_id = device_get_tool_serial (device);

      if (serial_id != 0)
        {
          tool = g_hash_table_lookup (seat_x11->tools_by_serial,
                                      GUINT_TO_POINTER (serial_id));
          if (!tool)
            {
              type = clutter_input_device_get_device_type (device) == CLUTTER_ERASER_DEVICE ?
                CLUTTER_INPUT_DEVICE_TOOL_ERASER : CLUTTER_INPUT_DEVICE_TOOL_PEN;
              tool = meta_input_device_tool_x11_new (serial_id, type);
              g_hash_table_insert (seat_x11->tools_by_serial,
                                   GUINT_TO_POINTER (serial_id),
                                   tool);
            }
        }

      meta_input_device_x11_update_tool (device, tool);
      g_signal_emit_by_name (seat_x11, "tool-changed", device, tool);
    }
}

static void
translate_raw_event (MetaSeatX11 *seat_x11,
                     XEvent      *xevent)
{
  ClutterInputDevice *device;
  XGenericEventCookie *cookie;
  XIEvent *xi_event;
  XIRawEvent *xev;
  float x,y;

  cookie = &xevent->xcookie;
  xi_event = (XIEvent *) cookie->data;
  xev = (XIRawEvent *) xi_event;

  device = g_hash_table_lookup (seat_x11->devices_by_id,
                                GINT_TO_POINTER (xev->deviceid));
  if (device == NULL)
    return;

  if (!_clutter_is_input_pointer_a11y_enabled (device))
    return;

  switch (cookie->evtype)
    {
    case XI_RawMotion:
      g_debug ("raw motion: device:%d '%s'",
               device->id,
               device->device_name);

      /* We don't get actual pointer location with raw events, and we cannot
       * rely on `clutter_input_device_get_coords()` either because of
       * unreparented toplevels (like all client-side decoration windows),
       * so we need to explicitely query the pointer here...
       */
      if (meta_input_device_x11_get_pointer_location (device, &x, &y))
        _clutter_input_pointer_a11y_on_motion_event (device, x, y);
      break;
    case XI_RawButtonPress:
    case XI_RawButtonRelease:
      g_debug ("raw button %s: device:%d '%s' button %i",
               cookie->evtype == XI_RawButtonPress
               ? "press  "
               : "release",
               device->id,
               device->device_name,
               xev->detail);
      _clutter_input_pointer_a11y_on_button_event (device,
                                                  xev->detail,
                                                  (cookie->evtype == XI_RawButtonPress));
      break;
    }
}

static gboolean
translate_pad_axis (ClutterInputDevice *device,
                    XIValuatorState    *valuators,
                    ClutterEventType   *evtype,
                    uint32_t           *number,
                    double             *value)
{
  double *values;
  int i;

  values = valuators->values;

  for (i = PAD_AXIS_FIRST; i < valuators->mask_len * 8; i++)
    {
      double val;
      uint32_t axis_number = 0;

      if (!XIMaskIsSet (valuators->mask, i))
        continue;

      val = *values++;
      if (val <= 0)
        continue;

      _clutter_input_device_translate_axis (device, i, val, value);

      if (i == PAD_AXIS_RING1 || i == PAD_AXIS_RING2)
        {
          *evtype = CLUTTER_PAD_RING;
          (*value) *= 360.0;
        }
      else if (i == PAD_AXIS_STRIP1 || i == PAD_AXIS_STRIP2)
        {
          *evtype = CLUTTER_PAD_STRIP;
        }
      else
        continue;

      if (i == PAD_AXIS_STRIP2 || i == PAD_AXIS_RING2)
        axis_number++;

      *number = axis_number;
      return TRUE;
    }

  return FALSE;
}

static gboolean
translate_pad_event (ClutterEvent       *event,
                     XIDeviceEvent      *xev,
                     ClutterInputDevice *device)
{
  double value;
  uint32_t number, mode = 0;

  if (!translate_pad_axis (device, &xev->valuators,
                           &event->any.type,
                           &number, &value))
    return FALSE;

  /* When touching a ring/strip a first XI_Motion event
   * is generated. Use it to reset the pad state, so
   * later events actually have a directionality.
   */
  if (xev->evtype == XI_Motion)
    value = -1;

#ifdef HAVE_LIBWACOM
  mode = meta_input_device_x11_get_pad_group_mode (device, number);
#endif

  if (event->any.type == CLUTTER_PAD_RING)
    {
      event->pad_ring.ring_number = number;
      event->pad_ring.angle = value;
      event->pad_ring.mode = mode;
    }
  else
    {
      event->pad_strip.strip_number = number;
      event->pad_strip.value = value;
      event->pad_strip.mode = mode;
    }

  event->any.time = xev->time;
  clutter_event_set_device (event, device);
  clutter_event_set_source_device (event, device);

  g_debug ("%s: win:0x%x, device:%d '%s', time:%d "
           "(value:%f)",
           event->any.type == CLUTTER_PAD_RING
           ? "pad ring  "
           : "pad strip",
           (unsigned int) xev->event,
           device->id,
           device->device_name,
           event->any.time, value);

  return TRUE;
}

static ClutterStage *
get_event_stage (MetaSeatX11 *seat_x11,
                 XIEvent     *xi_event)
{
  Window xwindow = None;

  switch (xi_event->evtype)
    {
    case XI_KeyPress:
    case XI_KeyRelease:
    case XI_ButtonPress:
    case XI_ButtonRelease:
    case XI_Motion:
    case XI_TouchBegin:
    case XI_TouchUpdate:
    case XI_TouchEnd:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;

        xwindow = xev->event;
      }
      break;

    case XI_Enter:
    case XI_Leave:
    case XI_FocusIn:
    case XI_FocusOut:
      {
        XIEnterEvent *xev = (XIEnterEvent *) xi_event;

        xwindow = xev->event;
      }
      break;

    case XI_HierarchyChanged:
      return CLUTTER_STAGE (meta_backend_get_stage (meta_get_backend ()));

    default:
      break;
    }

  if (xwindow == None)
    return NULL;

  return meta_x11_get_stage_from_window (xwindow);
}

/*
 * print_key_sym: Translate a symbol to its printable form if any
 * @symbol: the symbol to translate
 * @buffer: the buffer where to put the translated string
 * @len: size of the buffer
 *
 * Translates @symbol into a printable representation in @buffer, if possible.
 *
 * Return value: The number of bytes of the translated string, 0 if the
 *               symbol can't be printed
 *
 * Note: The code is derived from libX11's src/KeyBind.c
 *       Copyright 1985, 1987, 1998  The Open Group
 *
 * Note: This code works for Latin-1 symbols. clutter_keysym_to_unicode()
 *       does the work for the other keysyms.
 */
static int
print_keysym (uint32_t symbol,
              char    *buffer,
              int      len)
{
  unsigned long high_bytes;
  unsigned char c;

  high_bytes = symbol >> 8;
  if (!(len &&
        ((high_bytes == 0) ||
         ((high_bytes == 0xFF) &&
          (((symbol >= CLUTTER_KEY_BackSpace) &&
            (symbol <= CLUTTER_KEY_Clear)) ||
           (symbol == CLUTTER_KEY_Return) ||
           (symbol == CLUTTER_KEY_Escape) ||
           (symbol == CLUTTER_KEY_KP_Space) ||
           (symbol == CLUTTER_KEY_KP_Tab) ||
           (symbol == CLUTTER_KEY_KP_Enter) ||
           ((symbol >= CLUTTER_KEY_KP_Multiply) &&
            (symbol <= CLUTTER_KEY_KP_9)) ||
           (symbol == CLUTTER_KEY_KP_Equal) ||
           (symbol == CLUTTER_KEY_Delete))))))
    return 0;

  /* if X keysym, convert to ascii by grabbing low 7 bits */
  if (symbol == CLUTTER_KEY_KP_Space)
    c = CLUTTER_KEY_space & 0x7F; /* patch encoding botch */
  else if (high_bytes == 0xFF)
    c = symbol & 0x7F;
  else
    c = symbol & 0xFF;

  buffer[0] = c;
  return 1;
}

static double *
translate_axes (ClutterInputDevice *device,
                double              x,
                double              y,
                XIValuatorState    *valuators)
{
  uint32_t n_axes = clutter_input_device_get_n_axes (device);
  uint32_t i;
  double *retval;
  double *values;

  retval = g_new0 (double, n_axes);
  values = valuators->values;

  for (i = 0; i < valuators->mask_len * 8; i++)
    {
      ClutterInputAxis axis;
      double val;

      if (!XIMaskIsSet (valuators->mask, i))
        continue;

      axis = clutter_input_device_get_axis (device, i);
      val = *values++;

      switch (axis)
        {
        case CLUTTER_INPUT_AXIS_X:
          retval[i] = x;
          break;

        case CLUTTER_INPUT_AXIS_Y:
          retval[i] = y;
          break;

        default:
          _clutter_input_device_translate_axis (device, i, val, &retval[i]);
          break;
        }
    }

  return retval;
}

static double
scroll_valuators_changed (ClutterInputDevice *device,
                          XIValuatorState    *valuators,
                          double             *dx_p,
                          double             *dy_p)
{
  gboolean retval = FALSE;
  uint32_t n_axes, n_val, i;
  double *values;

  n_axes = clutter_input_device_get_n_axes (device);
  values = valuators->values;

  *dx_p = *dy_p = 0.0;

  n_val = 0;

  for (i = 0; i < MIN (valuators->mask_len * 8, n_axes); i++)
    {
      ClutterScrollDirection direction;
      double delta;

      if (!XIMaskIsSet (valuators->mask, i))
        continue;

      if (_clutter_input_device_get_scroll_delta (device, i,
                                                  values[n_val],
                                                  &direction,
                                                  &delta))
        {
          retval = TRUE;

          if (direction == CLUTTER_SCROLL_UP ||
              direction == CLUTTER_SCROLL_DOWN)
            *dy_p = delta;
          else
            *dx_p = delta;
        }

      n_val += 1;
    }

  return retval;
}

static void
translate_coords (MetaStageX11 *stage_x11,
                  double        event_x,
                  double        event_y,
                  float        *x_out,
                  float        *y_out)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_x11);
  ClutterActor *stage = CLUTTER_ACTOR (stage_cogl->wrapper);
  float stage_width;
  float stage_height;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  *x_out = CLAMP (event_x, 0, stage_width);
  *y_out = CLAMP (event_y, 0, stage_height);
}

static void
on_keymap_state_change (MetaKeymapX11 *keymap_x11,
                        gpointer       data)
{
  ClutterSeat *seat = CLUTTER_SEAT (data);
  ClutterKbdA11ySettings kbd_a11y_settings;

  /* On keymaps state change, just reapply the current settings, it'll
   * take care of enabling/disabling mousekeys based on NumLock state.
   */
  clutter_seat_get_kbd_a11y_settings (seat, &kbd_a11y_settings);
  meta_seat_x11_apply_kbd_a11y_settings (seat, &kbd_a11y_settings);
}

static void
meta_seat_x11_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);

  switch (prop_id)
    {
    case PROP_OPCODE:
      seat_x11->opcode = g_value_get_int (value);
      break;
    case PROP_POINTER_ID:
      seat_x11->pointer_id = g_value_get_int (value);
      break;
    case PROP_KEYBOARD_ID:
      seat_x11->keyboard_id = g_value_get_int (value);
      break;
    case PROP_TOUCH_MODE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_x11_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);

  switch (prop_id)
    {
    case PROP_OPCODE:
      g_value_set_int (value, seat_x11->opcode);
      break;
    case PROP_POINTER_ID:
      g_value_set_int (value, seat_x11->pointer_id);
      break;
    case PROP_KEYBOARD_ID:
      g_value_set_int (value, seat_x11->keyboard_id);
      break;
    case PROP_TOUCH_MODE:
      g_value_set_boolean (value, seat_x11->touch_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

void
meta_seat_x11_notify_devices (MetaSeatX11  *seat_x11,
			      ClutterStage *stage)
{
  GHashTableIter iter;
  ClutterInputDevice *device;

  g_hash_table_iter_init (&iter, seat_x11->devices_by_id);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device))
    {
      ClutterEvent *event;

      event = clutter_event_new (CLUTTER_DEVICE_ADDED);
      clutter_event_set_device (event, device);
      clutter_event_set_stage (event, stage);
      clutter_do_event (event);
    }
}

static void
meta_seat_x11_constructed (GObject *object)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);
  ClutterBackend *backend = clutter_get_default_backend ();
  GHashTable *masters, *slaves;
  XIDeviceInfo *info;
  XIEventMask event_mask;
  unsigned char mask[XIMaskLen(XI_LASTEVENT)] = { 0, };
  int n_devices, i;
  Display *xdisplay;

  xdisplay = clutter_x11_get_default_display ();
  masters = g_hash_table_new (NULL, NULL);
  slaves = g_hash_table_new (NULL, NULL);

  info = XIQueryDevice (clutter_x11_get_default_display (),
                        XIAllDevices, &n_devices);

  for (i = 0; i < n_devices; i++)
    {
      XIDeviceInfo *xi_device = &info[i];

      if (!xi_device->enabled)
        continue;

      add_device (seat_x11, backend, xi_device, TRUE);

      if (xi_device->use == XIMasterPointer ||
          xi_device->use == XIMasterKeyboard)
        {
          g_hash_table_insert (masters,
                               GINT_TO_POINTER (xi_device->deviceid),
                               GINT_TO_POINTER (xi_device->attachment));
        }
      else if (xi_device->use == XISlavePointer ||
               xi_device->use == XISlaveKeyboard)
        {
          g_hash_table_insert (slaves,
                               GINT_TO_POINTER (xi_device->deviceid),
                               GINT_TO_POINTER (xi_device->attachment));
        }
    }

  XIFreeDeviceInfo (info);

  g_hash_table_foreach (masters, relate_masters, seat_x11);
  g_hash_table_destroy (masters);

  g_hash_table_foreach (slaves, relate_slaves, seat_x11);
  g_hash_table_destroy (slaves);

  XISetMask (mask, XI_HierarchyChanged);
  XISetMask (mask, XI_DeviceChanged);
  XISetMask (mask, XI_PropertyEvent);

  event_mask.deviceid = XIAllDevices;
  event_mask.mask_len = sizeof (mask);
  event_mask.mask = mask;

  XISelectEvents (xdisplay, clutter_x11_get_root_window (),
                  &event_mask, 1);

  memset(mask, 0, sizeof (mask));
  XISetMask (mask, XI_RawMotion);
  XISetMask (mask, XI_RawButtonPress);
  XISetMask (mask, XI_RawButtonRelease);

  event_mask.deviceid = XIAllMasterDevices;
  event_mask.mask_len = sizeof (mask);
  event_mask.mask = mask;

  XISelectEvents (xdisplay, clutter_x11_get_root_window (),
                  &event_mask, 1);

  XSync (xdisplay, False);

  seat_x11->keymap = g_object_new (META_TYPE_KEYMAP_X11,
                                   "backend", backend,
                                   NULL);
  g_signal_connect (seat_x11->keymap,
                    "state-changed",
                    G_CALLBACK (on_keymap_state_change),
                    seat_x11);

  meta_seat_x11_a11y_init (CLUTTER_SEAT (seat_x11));

  if (G_OBJECT_CLASS (meta_seat_x11_parent_class)->constructed)
    G_OBJECT_CLASS (meta_seat_x11_parent_class)->constructed (object);
}

static void
meta_seat_x11_finalize (GObject *object)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);

  g_hash_table_unref (seat_x11->devices_by_id);
  g_hash_table_unref (seat_x11->tools_by_serial);
  g_list_free (seat_x11->devices);

  G_OBJECT_CLASS (meta_seat_x11_parent_class)->finalize (object);
}

static ClutterInputDevice *
meta_seat_x11_get_pointer (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);

  return seat_x11->core_pointer;
}

static ClutterInputDevice *
meta_seat_x11_get_keyboard (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);

  return seat_x11->core_keyboard;
}

static GList *
meta_seat_x11_list_devices (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  GList *retval = NULL, *l;

  for (l = seat_x11->devices; l; l = l->next)
    retval = g_list_prepend (retval, l->data);

  return retval;
}

static void
meta_seat_x11_bell_notify (ClutterSeat *seat)
{
  MetaDisplay *display = meta_get_display ();

  meta_bell_notify (display, NULL);
}

static ClutterKeymap *
meta_seat_x11_get_keymap (ClutterSeat *seat)
{
  return CLUTTER_KEYMAP (META_SEAT_X11 (seat)->keymap);
}

static void
meta_seat_x11_copy_event_data (ClutterSeat        *seat,
                               const ClutterEvent *src,
                               ClutterEvent       *dest)
{
  gpointer event_x11;

  event_x11 = _clutter_event_get_platform_data (src);
  if (event_x11 != NULL)
    _clutter_event_set_platform_data (dest, meta_event_x11_copy (event_x11));
}

static void
meta_seat_x11_free_event_data (ClutterSeat  *seat,
                               ClutterEvent *event)
{
  gpointer event_x11;

  event_x11 = _clutter_event_get_platform_data (event);
  if (event_x11 != NULL)
    meta_event_x11_free (event_x11);
}

static ClutterVirtualInputDevice *
meta_seat_x11_create_virtual_device (ClutterSeat            *seat,
                                     ClutterInputDeviceType  device_type)
{
  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_X11,
                       "seat", seat,
                       "device-type", device_type,
                       NULL);
}

static ClutterVirtualDeviceType
meta_seat_x11_get_supported_virtual_device_types (ClutterSeat *seat)
{
  return (CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD |
          CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER);
}

static void
meta_seat_x11_warp_pointer (ClutterSeat *seat,
                            int          x,
                            int          y)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);

  XIWarpPointer (clutter_x11_get_default_display (),
                 seat_x11->pointer_id,
                 None,
                 clutter_x11_get_root_window (),
                 0, 0, 0, 0,
                 x, y);
}

static void
meta_seat_x11_class_init (MetaSeatX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterSeatClass *seat_class = CLUTTER_SEAT_CLASS (klass);

  object_class->set_property = meta_seat_x11_set_property;
  object_class->get_property = meta_seat_x11_get_property;
  object_class->constructed = meta_seat_x11_constructed;
  object_class->finalize = meta_seat_x11_finalize;

  seat_class->get_pointer = meta_seat_x11_get_pointer;
  seat_class->get_keyboard = meta_seat_x11_get_keyboard;
  seat_class->list_devices = meta_seat_x11_list_devices;
  seat_class->bell_notify = meta_seat_x11_bell_notify;
  seat_class->get_keymap = meta_seat_x11_get_keymap;
  seat_class->copy_event_data = meta_seat_x11_copy_event_data;
  seat_class->free_event_data = meta_seat_x11_free_event_data;
  seat_class->apply_kbd_a11y_settings = meta_seat_x11_apply_kbd_a11y_settings;
  seat_class->create_virtual_device = meta_seat_x11_create_virtual_device;
  seat_class->get_supported_virtual_device_types = meta_seat_x11_get_supported_virtual_device_types;
  seat_class->warp_pointer = meta_seat_x11_warp_pointer;
  seat_class->handle_device_event = meta_seat_x11_handle_device_event;

  props[PROP_OPCODE] =
    g_param_spec_int ("opcode",
                      "Opcode",
                      "Opcode",
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);
  props[PROP_POINTER_ID] =
    g_param_spec_int ("pointer-id",
                      "Pointer ID",
                      "Pointer ID",
                      2, G_MAXINT, 2,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);
  props[PROP_KEYBOARD_ID] =
    g_param_spec_int ("keyboard-id",
                      "Keyboard ID",
                      "Keyboard ID",
                      2, G_MAXINT, 2,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);

  g_object_class_override_property (object_class, PROP_TOUCH_MODE,
                                    "touch-mode");
}

static void
meta_seat_x11_init (MetaSeatX11 *seat)
{
  seat->devices_by_id = g_hash_table_new_full (NULL, NULL,
                                               NULL,
                                               (GDestroyNotify) g_object_unref);
  seat->tools_by_serial = g_hash_table_new_full (NULL, NULL, NULL,
                                                 (GDestroyNotify) g_object_unref);
}

MetaSeatX11 *
meta_seat_x11_new (int opcode,
                   int master_pointer,
                   int master_keyboard)
{
  return g_object_new (META_TYPE_SEAT_X11,
                       "opcode", opcode,
                       "pointer-id", master_pointer,
                       "keyboard-id", master_keyboard,
                       NULL);
}

static ClutterInputDevice *
get_source_device_checked (MetaSeatX11   *seat,
                           XIDeviceEvent *xev)
{
  ClutterInputDevice *source_device;

  source_device = g_hash_table_lookup (seat->devices_by_id,
                                       GINT_TO_POINTER (xev->sourceid));

  if (!source_device)
    g_warning ("Impossible to get the source device with id %d for event of "
               "type %d", xev->sourceid, xev->evtype);

  return source_device;
}

gboolean
meta_seat_x11_translate_event (MetaSeatX11  *seat,
                               XEvent       *xevent,
                               ClutterEvent *event)
{
  gboolean retval = FALSE;
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterStage *stage = NULL;
  MetaStageX11 *stage_x11 = NULL;
  ClutterInputDevice *device, *source_device;
  XGenericEventCookie *cookie;
  XIEvent *xi_event;

  if (meta_keymap_x11_handle_event (seat->keymap, xevent))
    return FALSE;

  cookie = &xevent->xcookie;

  if (cookie->type != GenericEvent ||
      cookie->extension != seat->opcode)
    return FALSE;

  xi_event = (XIEvent *) cookie->data;

  if (!xi_event)
    return FALSE;

  if (cookie->evtype == XI_RawMotion ||
      cookie->evtype == XI_RawButtonPress ||
      cookie->evtype == XI_RawButtonRelease)
    {
      translate_raw_event (seat, xevent);
      return FALSE;
    }

  if (!(xi_event->evtype == XI_DeviceChanged ||
        xi_event->evtype == XI_PropertyEvent))
    {
      stage = get_event_stage (seat, xi_event);
      if (stage == NULL || CLUTTER_ACTOR_IN_DESTRUCTION (stage))
        return FALSE;
      else
        stage_x11 = META_STAGE_X11 (_clutter_stage_get_window (stage));
    }

  event->any.stage = stage;

  switch (xi_event->evtype)
    {
    case XI_HierarchyChanged:
      {
        XIHierarchyEvent *xev = (XIHierarchyEvent *) xi_event;

        retval = translate_hierarchy_event (backend, seat, xev, event);
      }
      break;

    case XI_DeviceChanged:
      {
        XIDeviceChangedEvent *xev = (XIDeviceChangedEvent *) xi_event;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));
        if (device)
          {
            _clutter_input_device_reset_axes (device);
            translate_device_classes (clutter_x11_get_default_display (),
                                      device,
                                      xev->classes,
                                      xev->num_classes);
          }

        if (source_device)
          _clutter_input_device_reset_scroll_info (source_device);
      }
      retval = FALSE;
      break;
    case XI_KeyPress:
    case XI_KeyRelease:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        MetaKeymapX11 *keymap_x11 = seat->keymap;
        MetaEventX11 *event_x11;
        char buffer[7] = { 0, };
        gunichar n;

        source_device = get_source_device_checked (seat, xev);
        if (!source_device)
          return FALSE;

        event->key.type = event->type = (xev->evtype == XI_KeyPress)
                                      ? CLUTTER_KEY_PRESS
                                      : CLUTTER_KEY_RELEASE;

        if (xev->evtype == XI_KeyPress && xev->flags & XIKeyRepeat)
          clutter_event_set_flags (event, CLUTTER_EVENT_FLAG_REPEATED);

        event->key.time = xev->time;
        event->key.stage = stage;
        meta_input_device_x11_translate_state (event, &xev->mods, &xev->buttons, &xev->group);
        event->key.hardware_keycode = xev->detail;

          /* keyval is the key ignoring all modifiers ('1' vs. '!') */
        event->key.keyval =
          meta_keymap_x11_translate_key_state (keymap_x11,
                                               event->key.hardware_keycode,
                                               &event->key.modifier_state,
                                               NULL);

        /* KeyEvents have platform specific data associated to them */
        event_x11 = meta_event_x11_new ();
        _clutter_event_set_platform_data (event, event_x11);

        event_x11->key_group =
          meta_keymap_x11_get_key_group (keymap_x11,
                                         event->key.modifier_state);
        event_x11->key_is_modifier =
          meta_keymap_x11_get_is_modifier (keymap_x11,
                                           event->key.hardware_keycode);
        event_x11->num_lock_set =
          clutter_keymap_get_num_lock_state (CLUTTER_KEYMAP (keymap_x11));
        event_x11->caps_lock_set =
          clutter_keymap_get_caps_lock_state (CLUTTER_KEYMAP (keymap_x11));

        clutter_event_set_source_device (event, source_device);

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        clutter_event_set_device (event, device);

        if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER &&
            stage != NULL)
          _clutter_input_device_set_stage (device, stage);

        /* XXX keep this in sync with the evdev device manager */
        n = print_keysym (event->key.keyval, buffer, sizeof (buffer));
        if (n == 0)
          {
            /* not printable */
            event->key.unicode_value = (gunichar) '\0';
          }
        else
          {
            event->key.unicode_value = g_utf8_get_char_validated (buffer, n);
            if (event->key.unicode_value == -1 ||
                event->key.unicode_value == -2)
              event->key.unicode_value = (gunichar) '\0';
          }

        g_debug ("%s: win:0x%x device:%d source:%d, key: %12s (%d)",
                 event->any.type == CLUTTER_KEY_PRESS
                 ? "key press  "
                 : "key release",
                 (unsigned int) stage_x11->xwin,
                 xev->deviceid,
                 xev->sourceid,
                 event->key.keyval ? buffer : "(none)",
                 event->key.keyval);

        if (xi_event->evtype == XI_KeyPress)
          meta_stage_x11_set_user_time (stage_x11, event->key.time);

        retval = TRUE;
      }
      break;

    case XI_ButtonPress:
    case XI_ButtonRelease:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;

        source_device = get_source_device_checked (seat, xev);
	if (!source_device)
          return FALSE;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));

        /* Set the stage for core events coming out of nowhere (see bug #684509) */
        if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER &&
            clutter_input_device_get_pointer_stage (device) == NULL &&
            stage != NULL)
          _clutter_input_device_set_stage (device, stage);

	if (clutter_input_device_get_device_type (source_device) == CLUTTER_PAD_DEVICE)
          {
            /* We got these events because of the passive button grab */
            XIAllowEvents (clutter_x11_get_default_display (),
                           xev->sourceid,
                           XIAsyncDevice,
                           xev->time);

            event->any.stage = stage;

            if (xev->detail >= 4 && xev->detail <= 7)
              {
                retval = FALSE;

                if (xi_event->evtype == XI_ButtonPress &&
                    translate_pad_event (event, xev, source_device))
                  retval = TRUE;

                break;
              }

            event->any.type =
              (xi_event->evtype == XI_ButtonPress) ? CLUTTER_PAD_BUTTON_PRESS
                                                   : CLUTTER_PAD_BUTTON_RELEASE;
            event->any.time = xev->time;

            /* The 4-7 button range is taken as non-existent on pad devices,
             * let the buttons above that take over this range.
             */
            if (xev->detail > 7)
              xev->detail -= 4;

            /* Pad buttons are 0-indexed */
            event->pad_button.button = xev->detail - 1;
#ifdef HAVE_LIBWACOM
            meta_input_device_x11_update_pad_state (device,
                                                    event->pad_button.button,
                                                    (xi_event->evtype == XI_ButtonPress),
                                                    &event->pad_button.group,
                                                    &event->pad_button.mode);
#endif
            clutter_event_set_device (event, device);
            clutter_event_set_source_device (event, source_device);

            g_debug ("%s: win:0x%x, device:%d '%s', time:%d "
                     "(button:%d)",
                     event->any.type == CLUTTER_BUTTON_PRESS
                     ? "pad button press  "
                     : "pad button release",
                     (unsigned int) stage_x11->xwin,
                     device->id,
                     device->device_name,
                     event->any.time,
                     event->pad_button.button);
            retval = TRUE;
            break;
          }

        switch (xev->detail)
          {
          case 4:
          case 5:
          case 6:
          case 7:
            /* we only generate Scroll events on ButtonPress */
            if (xi_event->evtype == XI_ButtonRelease)
              return FALSE;

            event->scroll.type = event->type = CLUTTER_SCROLL;

            if (xev->detail == 4)
              event->scroll.direction = CLUTTER_SCROLL_UP;
            else if (xev->detail == 5)
              event->scroll.direction = CLUTTER_SCROLL_DOWN;
            else if (xev->detail == 6)
              event->scroll.direction = CLUTTER_SCROLL_LEFT;
            else
              event->scroll.direction = CLUTTER_SCROLL_RIGHT;

            event->scroll.stage = stage;

            event->scroll.time = xev->time;
            translate_coords (stage_x11, xev->event_x, xev->event_y, &event->scroll.x, &event->scroll.y);
            meta_input_device_x11_translate_state (event,
                                                   &xev->mods,
                                                   &xev->buttons,
                                                   &xev->group);

            clutter_event_set_source_device (event, source_device);
            clutter_event_set_device (event, device);

            event->scroll.axes = translate_axes (event->scroll.device,
                                                 event->scroll.x,
                                                 event->scroll.y,
                                                 &xev->valuators);
            g_debug ("scroll: win:0x%x, device:%d '%s', time:%d "
                     "(direction:%s, "
                     "x:%.2f, y:%.2f, "
                     "emulated:%s)",
                     (unsigned int) stage_x11->xwin,
                     device->id,
                     device->device_name,
                     event->any.time,
                     event->scroll.direction == CLUTTER_SCROLL_UP ? "up" :
                     event->scroll.direction == CLUTTER_SCROLL_DOWN ? "down" :
                     event->scroll.direction == CLUTTER_SCROLL_LEFT ? "left" :
                     event->scroll.direction == CLUTTER_SCROLL_RIGHT ? "right" :
                     "invalid",
                     event->scroll.x,
                     event->scroll.y,
                     (xev->flags & XIPointerEmulated) ? "yes" : "no");
            break;

          default:
            event->button.type = event->type =
              (xi_event->evtype == XI_ButtonPress) ? CLUTTER_BUTTON_PRESS
                                                   : CLUTTER_BUTTON_RELEASE;

            event->button.stage = stage;

            event->button.time = xev->time;
            translate_coords (stage_x11, xev->event_x, xev->event_y, &event->button.x, &event->button.y);
            event->button.button = xev->detail;
            meta_input_device_x11_translate_state (event,
                                                   &xev->mods,
                                                   &xev->buttons,
                                                   &xev->group);

            clutter_event_set_source_device (event, source_device);
            clutter_event_set_device (event, device);
            clutter_event_set_device_tool (event,
                                           meta_input_device_x11_get_current_tool (source_device));

            event->button.axes = translate_axes (event->button.device,
                                                 event->button.x,
                                                 event->button.y,
                                                 &xev->valuators);
            g_debug ("%s: win:0x%x, device:%d '%s', time:%d "
                     "(button:%d, "
                     "x:%.2f, y:%.2f, "
                     "axes:%s, "
                     "emulated:%s)",
                     event->any.type == CLUTTER_BUTTON_PRESS
                     ? "button press  "
                     : "button release",
                     (unsigned int) stage_x11->xwin,
                     device->id,
                     device->device_name,
                     event->any.time,
                     event->button.button,
                     event->button.x,
                     event->button.y,
                     event->button.axes != NULL ? "yes" : "no",
                     (xev->flags & XIPointerEmulated) ? "yes" : "no");
            break;
          }

        if (device->stage != NULL)
          _clutter_input_device_set_stage (source_device, device->stage);

        if (xev->flags & XIPointerEmulated)
          _clutter_event_set_pointer_emulated (event, TRUE);

        if (xi_event->evtype == XI_ButtonPress)
          meta_stage_x11_set_user_time (stage_x11, event->button.time);

        retval = TRUE;
      }
      break;

    case XI_Motion:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        double delta_x, delta_y;

        source_device = get_source_device_checked (seat, xev);
        if (!source_device)
          return FALSE;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));

        if (clutter_input_device_get_device_type (source_device) == CLUTTER_PAD_DEVICE)
          {
            event->any.stage = stage;

            if (translate_pad_event (event, xev, source_device))
              retval = TRUE;
            break;
          }

        /* Set the stage for core events coming out of nowhere (see bug #684509) */
        if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER &&
            clutter_input_device_get_pointer_stage (device) == NULL &&
            stage != NULL)
          _clutter_input_device_set_stage (device, stage);

        if (scroll_valuators_changed (source_device,
                                      &xev->valuators,
                                      &delta_x, &delta_y))
          {
            event->scroll.type = event->type = CLUTTER_SCROLL;
            event->scroll.direction = CLUTTER_SCROLL_SMOOTH;

            event->scroll.stage = stage;
            event->scroll.time = xev->time;
            translate_coords (stage_x11, xev->event_x, xev->event_y, &event->scroll.x, &event->scroll.y);
            meta_input_device_x11_translate_state (event,
                                                   &xev->mods,
                                                   &xev->buttons,
                                                   &xev->group);

            clutter_event_set_scroll_delta (event, delta_x, delta_y);
            clutter_event_set_source_device (event, source_device);
            clutter_event_set_device (event, device);

            g_debug ("smooth scroll: win:0x%x device:%d '%s' (x:%.2f, y:%.2f, delta:%f, %f)",
                     (unsigned int) stage_x11->xwin,
                     event->scroll.device->id,
                     event->scroll.device->device_name,
                     event->scroll.x,
                     event->scroll.y,
                     delta_x, delta_y);

            retval = TRUE;
            break;
          }

        event->motion.type = event->type = CLUTTER_MOTION;

        event->motion.stage = stage;

        event->motion.time = xev->time;
        translate_coords (stage_x11, xev->event_x, xev->event_y, &event->motion.x, &event->motion.y);
        meta_input_device_x11_translate_state (event,
                                               &xev->mods,
                                               &xev->buttons,
                                               &xev->group);

        clutter_event_set_source_device (event, source_device);
        clutter_event_set_device (event, device);
        clutter_event_set_device_tool (event,
                                       meta_input_device_x11_get_current_tool (source_device));

        event->motion.axes = translate_axes (event->motion.device,
                                             event->motion.x,
                                             event->motion.y,
                                             &xev->valuators);

        if (device->stage != NULL)
          _clutter_input_device_set_stage (source_device, device->stage);

        if (xev->flags & XIPointerEmulated)
          _clutter_event_set_pointer_emulated (event, TRUE);

        g_debug ("motion: win:0x%x device:%d '%s' (x:%.2f, y:%.2f, axes:%s)",
                 (unsigned int) stage_x11->xwin,
                 event->motion.device->id,
                 event->motion.device->device_name,
                 event->motion.x,
                 event->motion.y,
                 event->motion.axes != NULL ? "yes" : "no");

        retval = TRUE;
      }
      break;

    case XI_TouchBegin:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        if (!_clutter_input_device_get_stage (device))
          _clutter_input_device_set_stage (device, stage);
      }
      /* Fall through */
    case XI_TouchEnd:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;

        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));

        if (xi_event->evtype == XI_TouchBegin)
          event->touch.type = event->type = CLUTTER_TOUCH_BEGIN;
        else
          event->touch.type = event->type = CLUTTER_TOUCH_END;

        event->touch.stage = stage;
        event->touch.time = xev->time;
        translate_coords (stage_x11, xev->event_x, xev->event_y, &event->touch.x, &event->touch.y);
        meta_input_device_x11_translate_state (event,
                                               &xev->mods,
                                               &xev->buttons,
                                               &xev->group);

        clutter_event_set_source_device (event, source_device);

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        clutter_event_set_device (event, device);

        event->touch.axes = translate_axes (event->touch.device,
                                            event->motion.x,
                                            event->motion.y,
                                            &xev->valuators);

        if (xi_event->evtype == XI_TouchBegin)
          {
            event->touch.modifier_state |= CLUTTER_BUTTON1_MASK;

            meta_stage_x11_set_user_time (stage_x11, event->touch.time);
          }

        event->touch.sequence = GUINT_TO_POINTER (xev->detail);

        if (xev->flags & XITouchEmulatingPointer)
          _clutter_event_set_pointer_emulated (event, TRUE);

        g_debug ("touch %s: win:0x%x device:%d '%s' (seq:%d, x:%.2f, y:%.2f, axes:%s)",
                 event->type == CLUTTER_TOUCH_BEGIN ? "begin" : "end",
                 (unsigned int) stage_x11->xwin,
                 event->touch.device->id,
                 event->touch.device->device_name,
                 GPOINTER_TO_UINT (event->touch.sequence),
                 event->touch.x,
                 event->touch.y,
                 event->touch.axes != NULL ? "yes" : "no");

        retval = TRUE;
      }
      break;

    case XI_TouchUpdate:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;

        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));

        event->touch.type = event->type = CLUTTER_TOUCH_UPDATE;
        event->touch.stage = stage;
        event->touch.time = xev->time;
        event->touch.sequence = GUINT_TO_POINTER (xev->detail);
        translate_coords (stage_x11, xev->event_x, xev->event_y, &event->touch.x, &event->touch.y);

        clutter_event_set_source_device (event, source_device);

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        clutter_event_set_device (event, device);

        event->touch.axes = translate_axes (event->touch.device,
                                            event->motion.x,
                                            event->motion.y,
                                            &xev->valuators);

        meta_input_device_x11_translate_state (event,
                                               &xev->mods,
                                               &xev->buttons,
                                               &xev->group);
        event->touch.modifier_state |= CLUTTER_BUTTON1_MASK;

        if (xev->flags & XITouchEmulatingPointer)
          _clutter_event_set_pointer_emulated (event, TRUE);

        g_debug ("touch update: win:0x%x device:%d '%s' (seq:%d, x:%.2f, y:%.2f, axes:%s)",
                 (unsigned int) stage_x11->xwin,
                 event->touch.device->id,
                 event->touch.device->device_name,
                 GPOINTER_TO_UINT (event->touch.sequence),
                 event->touch.x,
                 event->touch.y,
                 event->touch.axes != NULL ? "yes" : "no");

        retval = TRUE;
      }
      break;

    case XI_Enter:
    case XI_Leave:
      {
        XIEnterEvent *xev = (XIEnterEvent *) xi_event;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));

        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));

        if (xi_event->evtype == XI_Enter)
          {
            event->crossing.type = event->type = CLUTTER_ENTER;

            event->crossing.stage = stage;
            event->crossing.source = CLUTTER_ACTOR (stage);
            event->crossing.related = NULL;

            event->crossing.time = xev->time;
            translate_coords (stage_x11, xev->event_x, xev->event_y, &event->crossing.x, &event->crossing.y);
          }
        else
          {
            if (device->stage == NULL)
              {
                g_debug ("Discarding Leave for ButtonRelease "
                         "event off-stage");
                retval = FALSE;
                break;
              }

            event->crossing.type = event->type = CLUTTER_LEAVE;

            event->crossing.stage = stage;
            event->crossing.source = CLUTTER_ACTOR (stage);
            event->crossing.related = NULL;

            event->crossing.time = xev->time;
            translate_coords (stage_x11, xev->event_x, xev->event_y, &event->crossing.x, &event->crossing.y);
          }

        _clutter_input_device_reset_scroll_info (source_device);

        clutter_event_set_device (event, device);
        clutter_event_set_source_device (event, source_device);

        retval = TRUE;
      }
      break;

    case XI_FocusIn:
    case XI_FocusOut:
      retval = FALSE;
      break;
    case XI_PropertyEvent:
      translate_property_event (seat, xi_event);
      retval = FALSE;
      break;
    }

  return retval;
}

ClutterInputDevice *
meta_seat_x11_lookup_device_id (MetaSeatX11 *seat_x11,
                                int          device_id)
{
  return g_hash_table_lookup (seat_x11->devices_by_id,
                              GINT_TO_POINTER (device_id));
}

void
meta_seat_x11_select_stage_events (MetaSeatX11  *seat,
                                   ClutterStage *stage)
{
  MetaStageX11 *stage_x11;
  XIEventMask xi_event_mask;
  unsigned char *mask;
  int len;

  stage_x11 = META_STAGE_X11 (_clutter_stage_get_window (stage));

  len = XIMaskLen (XI_LASTEVENT);
  mask = g_new0 (unsigned char, len);

  XISetMask (mask, XI_Motion);
  XISetMask (mask, XI_ButtonPress);
  XISetMask (mask, XI_ButtonRelease);
  XISetMask (mask, XI_KeyPress);
  XISetMask (mask, XI_KeyRelease);
  XISetMask (mask, XI_Enter);
  XISetMask (mask, XI_Leave);

  XISetMask (mask, XI_TouchBegin);
  XISetMask (mask, XI_TouchUpdate);
  XISetMask (mask, XI_TouchEnd);

  xi_event_mask.deviceid = XIAllMasterDevices;
  xi_event_mask.mask = mask;
  xi_event_mask.mask_len = len;

  XISelectEvents (clutter_x11_get_default_display (),
                  stage_x11->xwin, &xi_event_mask, 1);

  g_free (mask);
}
