/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2014 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * SECTION:input-settings
 * @title: MetaInputSettings
 * @short_description: Mutter input device configuration
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-input-device-private.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-input-mapper-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "core/display-private.h"
#include "meta/util.h"

static GQuark quark_tool_settings = 0;

typedef struct _MetaInputSettingsPrivate MetaInputSettingsPrivate;
typedef struct _DeviceMappingInfo DeviceMappingInfo;
typedef struct _CurrentToolInfo CurrentToolInfo;

struct _CurrentToolInfo
{
  MetaInputSettings *input_settings;
  ClutterInputDevice *device;
  ClutterInputDeviceTool *tool;
  GSettings *settings;
  gulong changed_id;
};

struct _DeviceMappingInfo
{
  MetaInputSettings *input_settings;
  ClutterInputDevice *device;
  GSettings *settings;
  gulong changed_id;
  guint *group_modes;
};

struct _MetaInputSettingsPrivate
{
  ClutterSeat *seat;
  MetaMonitorManager *monitor_manager;
  gulong monitors_changed_id;

  GSettings *mouse_settings;
  GSettings *touchpad_settings;
  GSettings *trackball_settings;
  GSettings *keyboard_settings;
  GSettings *gsd_settings;
  GSettings *keyboard_a11y_settings;
  GSettings *mouse_a11y_settings;

  GHashTable *mappable_devices;

  GHashTable *current_tools;

  ClutterVirtualInputDevice *virtual_pad_keyboard;

  GHashTable *two_finger_devices;

  /* Pad ring/strip emission */
  struct {
    ClutterInputDevice *pad;
    MetaPadActionType action;
    guint number;
    gdouble value;
  } last_pad_action_info;

  /* For absolute devices with no mapping in settings */
  MetaInputMapper *input_mapper;
};

typedef gboolean (* ConfigBoolMappingFunc) (MetaInputSettings  *input_settings,
                                            ClutterInputDevice *device,
                                            gboolean            value);

typedef void (*ConfigBoolFunc)   (MetaInputSettings  *input_settings,
                                  ClutterInputDevice *device,
                                  gboolean            setting);
typedef void (*ConfigDoubleFunc) (MetaInputSettings  *input_settings,
                                  ClutterInputDevice *device,
                                  gdouble             value);
typedef void (*ConfigUintFunc)   (MetaInputSettings  *input_settings,
                                  ClutterInputDevice *device,
                                  guint               value);

typedef enum
{
  META_PAD_DIRECTION_NONE = -1,
  META_PAD_DIRECTION_UP = 0,
  META_PAD_DIRECTION_DOWN,
  META_PAD_DIRECTION_CW,
  META_PAD_DIRECTION_CCW,
} MetaPadDirection;

G_DEFINE_TYPE_WITH_PRIVATE (MetaInputSettings, meta_input_settings, G_TYPE_OBJECT)

static GSList *
meta_input_settings_get_devices (MetaInputSettings      *settings,
                                 ClutterInputDeviceType  type)
{
  MetaInputSettingsPrivate *priv;
  GList *l, *devices;
  GSList *list = NULL;

  priv = meta_input_settings_get_instance_private (settings);
  devices = clutter_seat_list_devices (priv->seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_type (device) == type &&
          clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER)
        list = g_slist_prepend (list, device);
    }

  g_list_free (devices);

  return list;
}

static void
meta_input_settings_dispose (GObject *object)
{
  MetaInputSettings *settings = META_INPUT_SETTINGS (object);
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (settings);

  g_clear_object (&priv->virtual_pad_keyboard);

  g_clear_object (&priv->mouse_settings);
  g_clear_object (&priv->touchpad_settings);
  g_clear_object (&priv->trackball_settings);
  g_clear_object (&priv->keyboard_settings);
  g_clear_object (&priv->gsd_settings);
  g_clear_object (&priv->keyboard_a11y_settings);
  g_clear_object (&priv->mouse_a11y_settings);
  g_clear_object (&priv->input_mapper);
  g_clear_pointer (&priv->mappable_devices, g_hash_table_unref);
  g_clear_pointer (&priv->current_tools, g_hash_table_unref);

  if (priv->monitor_manager)
    g_clear_signal_handler (&priv->monitors_changed_id, priv->monitor_manager);

  g_clear_object (&priv->monitor_manager);

  g_clear_pointer (&priv->two_finger_devices, g_hash_table_destroy);

  G_OBJECT_CLASS (meta_input_settings_parent_class)->dispose (object);
}

static void
settings_device_set_bool_setting (MetaInputSettings  *input_settings,
                                  ClutterInputDevice *device,
                                  ConfigBoolFunc      func,
                                  gboolean            enabled)
{
  func (input_settings, device, enabled);
}

static void
settings_set_bool_setting (MetaInputSettings      *input_settings,
                           ClutterInputDeviceType  type,
                           ConfigBoolMappingFunc   mapping_func,
                           ConfigBoolFunc          func,
                           gboolean                enabled)
{
  GSList *devices, *l;

  devices = meta_input_settings_get_devices (input_settings, type);

  for (l = devices; l; l = l->next)
    {
      gboolean value = enabled;

      if (mapping_func)
        value = mapping_func (input_settings, l->data, value);
      settings_device_set_bool_setting (input_settings, l->data, func, value);
    }

  g_slist_free (devices);
}

static void
settings_device_set_double_setting (MetaInputSettings  *input_settings,
                                    ClutterInputDevice *device,
                                    ConfigDoubleFunc    func,
                                    gdouble             value)
{
  func (input_settings, device, value);
}

static void
settings_set_double_setting (MetaInputSettings      *input_settings,
                             ClutterInputDeviceType  type,
                             ConfigDoubleFunc        func,
                             gdouble                 value)
{
  GSList *devices, *d;

  devices = meta_input_settings_get_devices (input_settings, type);

  for (d = devices; d; d = d->next)
    settings_device_set_double_setting (input_settings, d->data, func, value);

  g_slist_free (devices);
}

static void
settings_device_set_uint_setting (MetaInputSettings  *input_settings,
                                  ClutterInputDevice *device,
                                  ConfigUintFunc      func,
                                  guint               value)
{
  (func) (input_settings, device, value);
}

static void
settings_set_uint_setting (MetaInputSettings      *input_settings,
                           ClutterInputDeviceType  type,
                           ConfigUintFunc          func,
                           guint                   value)
{
  GSList *devices, *d;

  devices = meta_input_settings_get_devices (input_settings, type);

  for (d = devices; d; d = d->next)
    settings_device_set_uint_setting (input_settings, d->data, func, value);

  g_slist_free (devices);
}

static void
update_touchpad_left_handed (MetaInputSettings  *input_settings,
                             ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  GDesktopTouchpadHandedness handedness;
  MetaInputSettingsPrivate *priv;
  gboolean enabled = FALSE;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  handedness = g_settings_get_enum (priv->touchpad_settings, "left-handed");

  switch (handedness)
    {
    case G_DESKTOP_TOUCHPAD_HANDEDNESS_RIGHT:
      enabled = FALSE;
      break;
    case G_DESKTOP_TOUCHPAD_HANDEDNESS_LEFT:
      enabled = TRUE;
      break;
    case G_DESKTOP_TOUCHPAD_HANDEDNESS_MOUSE:
      enabled = g_settings_get_boolean (priv->mouse_settings, "left-handed");
      break;
    default:
      g_assert_not_reached ();
    }

  if (device)
    {
      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_left_handed,
                                        enabled);
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE, NULL,
                                 input_settings_class->set_left_handed,
                                 enabled);
    }
}

static void
update_mouse_left_handed (MetaInputSettings  *input_settings,
                          ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  gboolean enabled;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_POINTER_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  enabled = g_settings_get_boolean (priv->mouse_settings, "left-handed");

  if (device)
    {
      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_left_handed,
                                        enabled);
    }
  else
    {
      GDesktopTouchpadHandedness touchpad_handedness;

      settings_set_bool_setting (input_settings, CLUTTER_POINTER_DEVICE, NULL,
                                 input_settings_class->set_left_handed,
                                 enabled);

      touchpad_handedness = g_settings_get_enum (priv->touchpad_settings,
                                                 "left-handed");

      /* Also update touchpads if they're following mouse settings */
      if (touchpad_handedness == G_DESKTOP_TOUCHPAD_HANDEDNESS_MOUSE)
        update_touchpad_left_handed (input_settings, NULL);
    }
}

static void
do_update_pointer_accel_profile (MetaInputSettings          *input_settings,
                                 GSettings                  *settings,
                                 ClutterInputDevice         *device,
                                 GDesktopPointerAccelProfile profile)
{
  MetaInputSettingsPrivate *priv =
    meta_input_settings_get_instance_private (input_settings);
  MetaInputSettingsClass *input_settings_class =
    META_INPUT_SETTINGS_GET_CLASS (input_settings);

  if (settings == priv->mouse_settings)
    input_settings_class->set_mouse_accel_profile (input_settings,
                                                   device,
                                                   profile);
  else if (settings == priv->trackball_settings)
    input_settings_class->set_trackball_accel_profile (input_settings,
                                                       device,
                                                       profile);
}

static void
update_pointer_accel_profile (MetaInputSettings  *input_settings,
                              GSettings          *settings,
                              ClutterInputDevice *device)
{
  GDesktopPointerAccelProfile profile;

  profile = g_settings_get_enum (settings, "accel-profile");

  if (device)
    {
      do_update_pointer_accel_profile (input_settings, settings,
                                       device, profile);
    }
  else
    {
      MetaInputSettingsPrivate *priv =
        meta_input_settings_get_instance_private (input_settings);
      GList *l, *devices;

      devices = clutter_seat_list_devices (priv->seat);
      for (l = devices; l; l = l->next)
        {
          device = l->data;

          if (clutter_input_device_get_device_mode (device) ==
              CLUTTER_INPUT_MODE_MASTER)
            continue;

          do_update_pointer_accel_profile (input_settings, settings,
                                           device, profile);
        }

      g_list_free (devices);
    }
}

static GSettings *
get_settings_for_device_type (MetaInputSettings      *input_settings,
                              ClutterInputDeviceType  type)
{
  MetaInputSettingsPrivate *priv;
  priv = meta_input_settings_get_instance_private (input_settings);
  switch (type)
    {
    case CLUTTER_POINTER_DEVICE:
      return priv->mouse_settings;
    case CLUTTER_TOUCHPAD_DEVICE:
      return priv->touchpad_settings;
    default:
      return NULL;
    }
}

static void
update_middle_click_emulation (MetaInputSettings  *input_settings,
                               GSettings          *settings,
                               ClutterInputDevice *device)
{
  ConfigBoolFunc func;
  const gchar *key = "middle-click-emulation";
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (input_settings);

  if (!settings)
    return;

  if (settings == priv->mouse_settings)
    func = META_INPUT_SETTINGS_GET_CLASS (input_settings)->set_mouse_middle_click_emulation;
  else if (settings == priv->touchpad_settings)
    func = META_INPUT_SETTINGS_GET_CLASS (input_settings)->set_touchpad_middle_click_emulation;
  else if (settings == priv->trackball_settings)
    func = META_INPUT_SETTINGS_GET_CLASS (input_settings)->set_trackball_middle_click_emulation;
  else
    return;

  if (device)
    {
      settings_device_set_bool_setting (input_settings, device, func,
                                        g_settings_get_boolean (settings, key));
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_POINTER_DEVICE,
                                 NULL, func,
                                 g_settings_get_boolean (settings, key));
    }
}

static void
update_device_speed (MetaInputSettings      *input_settings,
                     ClutterInputDevice     *device)
{
  GSettings *settings;
  ConfigDoubleFunc func;
  const gchar *key = "speed";

  func = META_INPUT_SETTINGS_GET_CLASS (input_settings)->set_speed;

  if (device)
    {
      settings = get_settings_for_device_type (input_settings,
                                               clutter_input_device_get_device_type (device));
      if (!settings)
        return;

      settings_device_set_double_setting (input_settings, device, func,
                                          g_settings_get_double (settings, key));
    }
  else
    {
      settings = get_settings_for_device_type (input_settings, CLUTTER_POINTER_DEVICE);
      settings_set_double_setting (input_settings, CLUTTER_POINTER_DEVICE, func,
                                   g_settings_get_double (settings, key));
      settings = get_settings_for_device_type (input_settings, CLUTTER_TOUCHPAD_DEVICE);
      settings_set_double_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE, func,
                                   g_settings_get_double (settings, key));
    }
}

static void
update_device_natural_scroll (MetaInputSettings      *input_settings,
                              ClutterInputDevice     *device)
{
  GSettings *settings;
  ConfigBoolFunc func;
  const gchar *key = "natural-scroll";

  func = META_INPUT_SETTINGS_GET_CLASS (input_settings)->set_invert_scroll;

  if (device)
    {
      settings = get_settings_for_device_type (input_settings,
                                               clutter_input_device_get_device_type (device));
      if (!settings)
        return;

      settings_device_set_bool_setting (input_settings, device, func,
                                        g_settings_get_boolean (settings, key));
    }
  else
    {
      settings = get_settings_for_device_type (input_settings, CLUTTER_POINTER_DEVICE);
      settings_set_bool_setting (input_settings, CLUTTER_POINTER_DEVICE,
                                 NULL, func,
                                 g_settings_get_boolean (settings, key));
      settings = get_settings_for_device_type (input_settings, CLUTTER_TOUCHPAD_DEVICE);
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE,
                                 NULL, func,
                                 g_settings_get_boolean (settings, key));
    }
}

static void
update_touchpad_disable_while_typing (MetaInputSettings  *input_settings,
                                      ClutterInputDevice *device)
{
  GSettings *settings;
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  gboolean enabled;
  const gchar *key = "disable-while-typing";

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  enabled = g_settings_get_boolean (priv->touchpad_settings, key);

  if (device)
   {
      settings = get_settings_for_device_type (input_settings,
                                               clutter_input_device_get_device_type (device));

      if (!settings)
        return;

      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_disable_while_typing,
                                        enabled);
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE, NULL,
                                 input_settings_class->set_disable_while_typing,
                                 enabled);
    }
}

static gboolean
device_is_tablet_touchpad (MetaInputSettings  *input_settings,
                           ClutterInputDevice *device)
{
#ifdef HAVE_LIBWACOM
  WacomIntegrationFlags flags = 0;
  WacomDevice *wacom_device;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return FALSE;

  wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

  if (wacom_device)
    {
      flags = libwacom_get_integration_flags (wacom_device);

      if ((flags & (WACOM_DEVICE_INTEGRATED_SYSTEM |
                    WACOM_DEVICE_INTEGRATED_DISPLAY)) == 0)
        return TRUE;
    }
#endif

  return FALSE;
}

static gboolean
force_enable_on_tablet (MetaInputSettings  *input_settings,
                        ClutterInputDevice *device,
                        gboolean            value)
{
  return device_is_tablet_touchpad (input_settings, device) || value;
}

static void
update_touchpad_tap_enabled (MetaInputSettings  *input_settings,
                             ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  gboolean enabled;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  enabled = g_settings_get_boolean (priv->touchpad_settings, "tap-to-click");

  if (device)
    {
      enabled = force_enable_on_tablet (input_settings, device, enabled);
      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_tap_enabled,
                                        enabled);
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE,
                                 force_enable_on_tablet,
                                 input_settings_class->set_tap_enabled,
                                 enabled);
    }
}

static void
update_touchpad_tap_and_drag_enabled (MetaInputSettings  *input_settings,
                                      ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  gboolean enabled;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  enabled = g_settings_get_boolean (priv->touchpad_settings, "tap-and-drag");

  if (device)
    {
      enabled = force_enable_on_tablet (input_settings, device, enabled);
      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_tap_and_drag_enabled,
                                        enabled);
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE,
                                 force_enable_on_tablet,
                                 input_settings_class->set_tap_and_drag_enabled,
                                 enabled);
    }
}

static void
update_touchpad_edge_scroll (MetaInputSettings *input_settings,
                             ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  gboolean edge_scroll_enabled;
  gboolean two_finger_scroll_enabled;
  gboolean two_finger_scroll_available;
  MetaInputSettingsPrivate *priv;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  edge_scroll_enabled = g_settings_get_boolean (priv->touchpad_settings, "edge-scrolling-enabled");
  two_finger_scroll_enabled = g_settings_get_boolean (priv->touchpad_settings, "two-finger-scrolling-enabled");
  two_finger_scroll_available = g_hash_table_size (priv->two_finger_devices) > 0;

  /* If both are enabled we prefer two finger. */
  if (edge_scroll_enabled && two_finger_scroll_enabled && two_finger_scroll_available)
    edge_scroll_enabled = FALSE;

  if (device)
    {
      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_edge_scroll,
                                        edge_scroll_enabled);
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE, NULL,
                                 (ConfigBoolFunc) input_settings_class->set_edge_scroll,
                                 edge_scroll_enabled);
    }
}

static void
update_touchpad_two_finger_scroll (MetaInputSettings *input_settings,
                                   ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  gboolean two_finger_scroll_enabled;
  MetaInputSettingsPrivate *priv;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  two_finger_scroll_enabled = g_settings_get_boolean (priv->touchpad_settings, "two-finger-scrolling-enabled");

  /* Disable edge since they can't both be set. */
  if (two_finger_scroll_enabled)
    update_touchpad_edge_scroll (input_settings, device);

  if (device)
    {
      settings_device_set_bool_setting (input_settings, device,
                                        input_settings_class->set_two_finger_scroll,
                                        two_finger_scroll_enabled);
    }
  else
    {
      settings_set_bool_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE, NULL,
                                 (ConfigBoolFunc) input_settings_class->set_two_finger_scroll,
                                 two_finger_scroll_enabled);
    }

  /* Edge might have been disabled because two finger was on. */
  if (!two_finger_scroll_enabled)
    update_touchpad_edge_scroll (input_settings, device);
}

static void
update_touchpad_click_method (MetaInputSettings *input_settings,
                              ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  GDesktopTouchpadClickMethod method;
  MetaInputSettingsPrivate *priv;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  method = g_settings_get_enum (priv->touchpad_settings, "click-method");

  if (device)
    {
      settings_device_set_uint_setting (input_settings, device,
                                        input_settings_class->set_click_method,
                                        method);
    }
  else
    {
      settings_set_uint_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE,
                                 (ConfigUintFunc) input_settings_class->set_click_method,
                                 method);
    }
}

static void
update_touchpad_send_events (MetaInputSettings  *input_settings,
                             ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  GDesktopDeviceSendEvents mode;

  if (device &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  mode = g_settings_get_enum (priv->touchpad_settings, "send-events");

  if (device)
    {
      settings_device_set_uint_setting (input_settings, device,
                                        input_settings_class->set_send_events,
                                        mode);
    }
  else
    {
      settings_set_uint_setting (input_settings, CLUTTER_TOUCHPAD_DEVICE,
                                 input_settings_class->set_send_events,
                                 mode);
    }
}

static void
update_trackball_scroll_button (MetaInputSettings  *input_settings,
                                ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  guint button;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);

  if (device && !input_settings_class->is_trackball_device (input_settings, device))
    return;

  /* This key is 'i' in the schema but it also specifies a minimum
   * range of 0 so the cast here is safe. */
  button = (guint) g_settings_get_int (priv->trackball_settings, "scroll-wheel-emulation-button");

  if (device)
    {
      input_settings_class->set_scroll_button (input_settings, device, button);
    }
  else if (!device)
    {
      GList *l, *devices;

      devices = clutter_seat_list_devices (priv->seat);

      for (l = devices; l; l = l->next)
        {
          device = l->data;

          if (input_settings_class->is_trackball_device (input_settings, device))
            input_settings_class->set_scroll_button (input_settings, device, button);
        }

      g_list_free (devices);
    }
}

static void
update_keyboard_repeat (MetaInputSettings *input_settings)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  guint delay, interval;
  gboolean repeat;

  priv = meta_input_settings_get_instance_private (input_settings);
  repeat = g_settings_get_boolean (priv->keyboard_settings, "repeat");
  delay = g_settings_get_uint (priv->keyboard_settings, "delay");
  interval = g_settings_get_uint (priv->keyboard_settings, "repeat-interval");

  delay = MAX (1, delay);
  interval = MAX (1, interval);

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  input_settings_class->set_keyboard_repeat (input_settings,
                                             repeat, delay, interval);
}

static MetaMonitor *
logical_monitor_find_monitor (MetaLogicalMonitor *logical_monitor,
                              const char         *vendor,
                              const char         *product,
                              const char         *serial)
{
  GList *monitors;
  GList *l;

  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (g_strcmp0 (meta_monitor_get_vendor (monitor), vendor) == 0 &&
          g_strcmp0 (meta_monitor_get_product (monitor), product) == 0 &&
          g_strcmp0 (meta_monitor_get_serial (monitor), serial) == 0)
        return monitor;
    }

  return NULL;
}

static void
meta_input_settings_find_monitor (MetaInputSettings   *input_settings,
                                  GSettings           *settings,
                                  ClutterInputDevice  *device,
                                  MetaMonitor        **out_monitor,
                                  MetaLogicalMonitor **out_logical_monitor)
{
  MetaInputSettingsPrivate *priv;
  MetaMonitorManager *monitor_manager;
  MetaMonitor *monitor;
  guint n_values;
  GList *logical_monitors;
  GList *l;
  gchar **edid;

  priv = meta_input_settings_get_instance_private (input_settings);
  edid = g_settings_get_strv (settings, "output");
  n_values = g_strv_length (edid);

  if (n_values != 3)
    {
      g_warning ("EDID configuration for device '%s' "
                 "is incorrect, must have 3 values",
                 clutter_input_device_get_device_name (device));
      goto out;
    }

  if (!*edid[0] && !*edid[1] && !*edid[2])
    goto out;

  monitor_manager = priv->monitor_manager;
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      monitor = logical_monitor_find_monitor (logical_monitor,
                                              edid[0], edid[1], edid[2]);
      if (monitor)
        {
          if (out_monitor)
            *out_monitor = monitor;
          if (out_logical_monitor)
            *out_logical_monitor = logical_monitor;
          break;
        }
    }

out:
  g_strfreev (edid);
}

static gboolean
meta_input_settings_delegate_on_mapper (MetaInputSettings  *input_settings,
                                        ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv;
  gboolean builtin = FALSE;

  priv = meta_input_settings_get_instance_private (input_settings);

#ifdef HAVE_LIBWACOM
  if (clutter_input_device_get_device_type (device) != CLUTTER_TOUCHSCREEN_DEVICE)
    {
      WacomDevice *wacom_device;
      WacomIntegrationFlags flags = 0;

      wacom_device =
        meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

      if (wacom_device)
        {
          flags = libwacom_get_integration_flags (wacom_device);

          if ((flags & (WACOM_DEVICE_INTEGRATED_SYSTEM |
                        WACOM_DEVICE_INTEGRATED_DISPLAY)) == 0)
            return FALSE;

          builtin = (flags & WACOM_DEVICE_INTEGRATED_SYSTEM) != 0;
        }
    }
#endif

  meta_input_mapper_add_device (priv->input_mapper, device, builtin);
  return TRUE;
}

static void
update_tablet_keep_aspect (MetaInputSettings  *input_settings,
                           GSettings          *settings,
                           ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaLogicalMonitor *logical_monitor = NULL;
  gboolean keep_aspect;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE)
    return;

#ifdef HAVE_LIBWACOM
  {
    WacomDevice *wacom_device;

    wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

    /* Keep aspect only makes sense in external tablets */
    if (wacom_device &&
        libwacom_get_integration_flags (wacom_device) != WACOM_DEVICE_INTEGRATED_NONE)
      return;
  }
#endif

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);

  if (clutter_input_device_get_mapping_mode (device) ==
      CLUTTER_INPUT_DEVICE_MAPPING_ABSOLUTE)
    {
      keep_aspect = g_settings_get_boolean (settings, "keep-aspect");
      meta_input_settings_find_monitor (input_settings, settings, device,
                                        NULL, &logical_monitor);
    }
  else
    {
      keep_aspect = FALSE;
    }

  input_settings_class->set_tablet_keep_aspect (input_settings, device,
                                                logical_monitor, keep_aspect);
}

static void
update_device_display (MetaInputSettings  *input_settings,
                       GSettings          *settings,
                       ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  MetaInputSettingsPrivate *priv;
  gfloat matrix[6] = { 1, 0, 0, 0, 1, 0 };
  MetaMonitor *monitor = NULL;
  MetaLogicalMonitor *logical_monitor = NULL;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_TOUCHSCREEN_DEVICE)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);
  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);

  /* If mapping is relative, the device can move on all displays */
  if (clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE ||
      clutter_input_device_get_mapping_mode (device) ==
      CLUTTER_INPUT_DEVICE_MAPPING_ABSOLUTE)
    {
      meta_input_settings_find_monitor (input_settings, settings, device,
                                        &monitor, &logical_monitor);
      if (monitor)
        {
          meta_input_mapper_remove_device (priv->input_mapper, device);
          meta_monitor_manager_get_monitor_matrix (priv->monitor_manager,
                                                   monitor, logical_monitor, matrix);
        }
      else
        {
          if (meta_input_settings_delegate_on_mapper (input_settings, device))
            return;
        }
    }

  input_settings_class->set_matrix (input_settings, device, matrix);

  /* Ensure the keep-aspect mapping is updated */
  update_tablet_keep_aspect (input_settings, settings, device);
}

static void
update_tablet_mapping (MetaInputSettings  *input_settings,
                       GSettings          *settings,
                       ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  GDesktopTabletMapping mapping;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE)
    return;

#ifdef HAVE_LIBWACOM
  {
    WacomDevice *wacom_device;

    wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

    /* Tablet mapping only makes sense on external tablets */
    if (wacom_device &&
        (libwacom_get_integration_flags (wacom_device) != WACOM_DEVICE_INTEGRATED_NONE))
      return;
  }
#endif

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  mapping = g_settings_get_enum (settings, "mapping");

  settings_device_set_uint_setting (input_settings, device,
                                    input_settings_class->set_tablet_mapping,
                                    mapping);

  /* Relative mapping disables keep-aspect/display */
  update_tablet_keep_aspect (input_settings, settings, device);
  update_device_display (input_settings, settings, device);
}

static void
update_tablet_area (MetaInputSettings  *input_settings,
                    GSettings          *settings,
                    ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  GVariant *variant;
  const gdouble *area;
  gsize n_elems;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE)
    return;

#ifdef HAVE_LIBWACOM
  {
    WacomDevice *wacom_device;

    wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

    /* Tablet area only makes sense on system/display integrated tablets */
    if (wacom_device &&
        (libwacom_get_integration_flags (wacom_device) &
         (WACOM_DEVICE_INTEGRATED_SYSTEM | WACOM_DEVICE_INTEGRATED_DISPLAY)) == 0)
      return;
  }
#endif

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  variant = g_settings_get_value (settings, "area");

  area = g_variant_get_fixed_array (variant, &n_elems, sizeof (gdouble));
  if (n_elems == 4)
    {
      input_settings_class->set_tablet_area (input_settings, device,
                                             area[0], area[1],
                                             area[2], area[3]);
    }

  g_variant_unref (variant);
}

static void
update_tablet_left_handed (MetaInputSettings  *input_settings,
                           GSettings          *settings,
                           ClutterInputDevice *device)
{
  MetaInputSettingsClass *input_settings_class;
  gboolean enabled;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PAD_DEVICE)
    return;

#ifdef HAVE_LIBWACOM
  {
    WacomDevice *wacom_device;

    wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

    /* Left handed mode only makes sense on external tablets */
    if (wacom_device &&
        (libwacom_get_integration_flags (wacom_device) != WACOM_DEVICE_INTEGRATED_NONE))
      return;
  }
#endif

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  enabled = g_settings_get_boolean (settings, "left-handed");

  settings_device_set_bool_setting (input_settings, device,
                                    input_settings_class->set_left_handed,
                                    enabled);
}

static void
meta_input_settings_changed_cb (GSettings  *settings,
                                const char *key,
                                gpointer    user_data)
{
  MetaInputSettings *input_settings = META_INPUT_SETTINGS (user_data);
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (input_settings);

  if (settings == priv->mouse_settings)
    {
      if (strcmp (key, "left-handed") == 0)
        update_mouse_left_handed (input_settings, NULL);
      else if (strcmp (key, "speed") == 0)
        update_device_speed (input_settings, NULL);
      else if (strcmp (key, "natural-scroll") == 0)
        update_device_natural_scroll (input_settings, NULL);
      else if (strcmp (key, "accel-profile") == 0)
        update_pointer_accel_profile (input_settings, settings, NULL);
      else if (strcmp (key, "middle-click-emulation") == 0)
        update_middle_click_emulation (input_settings, settings, NULL);
    }
  else if (settings == priv->touchpad_settings)
    {
      if (strcmp (key, "left-handed") == 0)
        update_touchpad_left_handed (input_settings, NULL);
      else if (strcmp (key, "speed") == 0)
        update_device_speed (input_settings, NULL);
      else if (strcmp (key, "natural-scroll") == 0)
        update_device_natural_scroll (input_settings, NULL);
      else if (strcmp (key, "tap-to-click") == 0)
        update_touchpad_tap_enabled (input_settings, NULL);
      else if (strcmp (key, "tap-and-drag") == 0)
        update_touchpad_tap_and_drag_enabled (input_settings, NULL);
      else if (strcmp(key, "disable-while-typing") == 0)
        update_touchpad_disable_while_typing (input_settings, NULL);
      else if (strcmp (key, "send-events") == 0)
        update_touchpad_send_events (input_settings, NULL);
      else if (strcmp (key, "edge-scrolling-enabled") == 0)
        update_touchpad_edge_scroll (input_settings, NULL);
      else if (strcmp (key, "two-finger-scrolling-enabled") == 0)
        update_touchpad_two_finger_scroll (input_settings, NULL);
      else if (strcmp (key, "click-method") == 0)
        update_touchpad_click_method (input_settings, NULL);
      else if (strcmp (key, "middle-click-emulation") == 0)
        update_middle_click_emulation (input_settings, settings, NULL);
    }
  else if (settings == priv->trackball_settings)
    {
      if (strcmp (key, "scroll-wheel-emulation-button") == 0)
        update_trackball_scroll_button (input_settings, NULL);
      else if (strcmp (key, "accel-profile") == 0)
        update_pointer_accel_profile (input_settings, settings, NULL);
      else if (strcmp (key, "middle-click-emulation") == 0)
        update_middle_click_emulation (input_settings, settings, NULL);
    }
  else if (settings == priv->keyboard_settings)
    {
      if (strcmp (key, "repeat") == 0 ||
          strcmp (key, "repeat-interval") == 0 ||
          strcmp (key, "delay") == 0)
        update_keyboard_repeat (input_settings);
      else if (strcmp (key, "remember-numlock-state") == 0)
        meta_input_settings_maybe_save_numlock_state (input_settings);
    }
}

static void
mapped_device_changed_cb (GSettings         *settings,
                          const gchar       *key,
                          DeviceMappingInfo *info)
{
  if (strcmp (key, "output") == 0)
    update_device_display (info->input_settings, settings, info->device);
  else if (strcmp (key, "mapping") == 0)
    update_tablet_mapping (info->input_settings, settings, info->device);
  else if (strcmp (key, "area") == 0)
    update_tablet_area (info->input_settings, settings, info->device);
  else if (strcmp (key, "keep-aspect") == 0)
    update_tablet_keep_aspect (info->input_settings, settings, info->device);
  else if (strcmp (key, "left-handed") == 0)
    update_tablet_left_handed (info->input_settings, settings, info->device);
}

static void
apply_mappable_device_settings (MetaInputSettings *input_settings,
                                DeviceMappingInfo *info)
{
  ClutterInputDeviceType device_type;

  update_device_display (input_settings, info->settings, info->device);
  device_type = clutter_input_device_get_device_type (info->device);

  if (device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_ERASER_DEVICE ||
      device_type == CLUTTER_PAD_DEVICE)
    {
      update_tablet_mapping (input_settings, info->settings, info->device);
      update_tablet_area (input_settings, info->settings, info->device);
      update_tablet_keep_aspect (input_settings, info->settings, info->device);
      update_tablet_left_handed (input_settings, info->settings, info->device);
    }
}

struct _keyboard_a11y_settings_flags_pair {
  const char *name;
  ClutterKeyboardA11yFlags flag;
} keyboard_a11y_settings_flags_pair[] = {
  { "enable",                    CLUTTER_A11Y_KEYBOARD_ENABLED },
  { "timeout-enable",            CLUTTER_A11Y_TIMEOUT_ENABLED },
  { "mousekeys-enable",          CLUTTER_A11Y_MOUSE_KEYS_ENABLED },
  { "slowkeys-enable",           CLUTTER_A11Y_SLOW_KEYS_ENABLED },
  { "slowkeys-beep-press",       CLUTTER_A11Y_SLOW_KEYS_BEEP_PRESS },
  { "slowkeys-beep-accept",      CLUTTER_A11Y_SLOW_KEYS_BEEP_ACCEPT },
  { "slowkeys-beep-reject",      CLUTTER_A11Y_SLOW_KEYS_BEEP_REJECT },
  { "bouncekeys-enable",         CLUTTER_A11Y_BOUNCE_KEYS_ENABLED },
  { "bouncekeys-beep-reject",    CLUTTER_A11Y_BOUNCE_KEYS_BEEP_REJECT },
  { "togglekeys-enable",         CLUTTER_A11Y_TOGGLE_KEYS_ENABLED },
  { "stickykeys-enable",         CLUTTER_A11Y_STICKY_KEYS_ENABLED },
  { "stickykeys-modifier-beep",  CLUTTER_A11Y_STICKY_KEYS_BEEP },
  { "stickykeys-two-key-off",    CLUTTER_A11Y_STICKY_KEYS_TWO_KEY_OFF },
  { "feature-state-change-beep", CLUTTER_A11Y_FEATURE_STATE_CHANGE_BEEP },
};

static void
load_keyboard_a11y_settings (MetaInputSettings  *input_settings,
                             ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (input_settings);
  ClutterKbdA11ySettings kbd_a11y_settings = { 0 };
  ClutterInputDevice *core_keyboard;
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);
  guint i;

  core_keyboard = clutter_seat_get_keyboard (priv->seat);
  if (device && device != core_keyboard)
    return;

  kbd_a11y_settings.controls = 0;
  for (i = 0; i < G_N_ELEMENTS (keyboard_a11y_settings_flags_pair); i++)
    {
      if (g_settings_get_boolean (priv->keyboard_a11y_settings, keyboard_a11y_settings_flags_pair[i].name))
        kbd_a11y_settings.controls |= keyboard_a11y_settings_flags_pair[i].flag;
    }

  kbd_a11y_settings.timeout_delay = g_settings_get_int (priv->keyboard_a11y_settings,
                                                        "disable-timeout");
  kbd_a11y_settings.slowkeys_delay = g_settings_get_int (priv->keyboard_a11y_settings,
                                                         "slowkeys-delay");
  kbd_a11y_settings.debounce_delay = g_settings_get_int (priv->keyboard_a11y_settings,
                                                         "bouncekeys-delay");
  kbd_a11y_settings.mousekeys_init_delay = g_settings_get_int (priv->keyboard_a11y_settings,
                                                               "mousekeys-init-delay");
  kbd_a11y_settings.mousekeys_max_speed = g_settings_get_int (priv->keyboard_a11y_settings,
                                                              "mousekeys-max-speed");
  kbd_a11y_settings.mousekeys_accel_time = g_settings_get_int (priv->keyboard_a11y_settings,
                                                               "mousekeys-accel-time");

  clutter_seat_set_kbd_a11y_settings (seat, &kbd_a11y_settings);
}

static void
on_keyboard_a11y_settings_changed (ClutterSeat              *seat,
                                   ClutterKeyboardA11yFlags  new_flags,
                                   ClutterKeyboardA11yFlags  what_changed,
                                   MetaInputSettings        *input_settings)
{
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (input_settings);
  guint i;

  for (i = 0; i < G_N_ELEMENTS (keyboard_a11y_settings_flags_pair); i++)
    {
      if (keyboard_a11y_settings_flags_pair[i].flag & what_changed)
        g_settings_set_boolean (priv->keyboard_a11y_settings,
                                keyboard_a11y_settings_flags_pair[i].name,
                                (new_flags & keyboard_a11y_settings_flags_pair[i].flag) ? TRUE : FALSE);
    }
}

static void
meta_input_keyboard_a11y_settings_changed (GSettings  *settings,
                                           const char *key,
                                           gpointer    user_data)
{
  MetaInputSettings *input_settings = META_INPUT_SETTINGS (user_data);

  load_keyboard_a11y_settings (input_settings, NULL);
}

struct _pointer_a11y_settings_flags_pair {
  const char *name;
  ClutterPointerA11yFlags flag;
} pointer_a11y_settings_flags_pair[] = {
  { "secondary-click-enabled", CLUTTER_A11Y_SECONDARY_CLICK_ENABLED },
  { "dwell-click-enabled",     CLUTTER_A11Y_DWELL_ENABLED },
};

static ClutterPointerA11yDwellDirection
pointer_a11y_dwell_direction_from_setting (MetaInputSettings *input_settings,
                                           const char        *key)
{
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (input_settings);
  GDesktopMouseDwellDirection dwell_gesture_direction;

  dwell_gesture_direction = g_settings_get_enum (priv->mouse_a11y_settings, key);
  switch (dwell_gesture_direction)
    {
    case G_DESKTOP_MOUSE_DWELL_DIRECTION_LEFT:
      return CLUTTER_A11Y_DWELL_DIRECTION_LEFT;
      break;
    case G_DESKTOP_MOUSE_DWELL_DIRECTION_RIGHT:
      return CLUTTER_A11Y_DWELL_DIRECTION_RIGHT;
      break;
    case G_DESKTOP_MOUSE_DWELL_DIRECTION_UP:
      return CLUTTER_A11Y_DWELL_DIRECTION_UP;
      break;
    case G_DESKTOP_MOUSE_DWELL_DIRECTION_DOWN:
      return CLUTTER_A11Y_DWELL_DIRECTION_DOWN;
      break;
    default:
      break;
    }
  return CLUTTER_A11Y_DWELL_DIRECTION_NONE;
}

static void
load_pointer_a11y_settings (MetaInputSettings  *input_settings,
                            ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv = meta_input_settings_get_instance_private (input_settings);
  ClutterPointerA11ySettings pointer_a11y_settings;
  ClutterInputDevice *core_pointer;
  GDesktopMouseDwellMode dwell_mode;
  guint i;

  core_pointer = clutter_seat_get_pointer (priv->seat);
  if (device && device != core_pointer)
    return;

  clutter_seat_get_pointer_a11y_settings (CLUTTER_SEAT (priv->seat),
                                          &pointer_a11y_settings);
  pointer_a11y_settings.controls = 0;
  for (i = 0; i < G_N_ELEMENTS (pointer_a11y_settings_flags_pair); i++)
    {
      if (g_settings_get_boolean (priv->mouse_a11y_settings, pointer_a11y_settings_flags_pair[i].name))
        pointer_a11y_settings.controls |= pointer_a11y_settings_flags_pair[i].flag;
    }

  /* "secondary-click-time" is expressed in seconds */
  pointer_a11y_settings.secondary_click_delay =
    (1000 * g_settings_get_double (priv->mouse_a11y_settings, "secondary-click-time"));
  /* "dwell-time" is expressed in seconds */
  pointer_a11y_settings.dwell_delay =
    (1000 * g_settings_get_double (priv->mouse_a11y_settings, "dwell-time"));
  pointer_a11y_settings.dwell_threshold = g_settings_get_int (priv->mouse_a11y_settings,
                                                              "dwell-threshold");

  dwell_mode = g_settings_get_enum (priv->mouse_a11y_settings, "dwell-mode");
  if (dwell_mode == G_DESKTOP_MOUSE_DWELL_MODE_WINDOW)
    pointer_a11y_settings.dwell_mode = CLUTTER_A11Y_DWELL_MODE_WINDOW;
  else
    pointer_a11y_settings.dwell_mode = CLUTTER_A11Y_DWELL_MODE_GESTURE;

  pointer_a11y_settings.dwell_gesture_single =
    pointer_a11y_dwell_direction_from_setting (input_settings, "dwell-gesture-single");
  pointer_a11y_settings.dwell_gesture_double =
    pointer_a11y_dwell_direction_from_setting (input_settings, "dwell-gesture-double");
  pointer_a11y_settings.dwell_gesture_drag =
    pointer_a11y_dwell_direction_from_setting (input_settings, "dwell-gesture-drag");
  pointer_a11y_settings.dwell_gesture_secondary =
    pointer_a11y_dwell_direction_from_setting (input_settings, "dwell-gesture-secondary");

  clutter_seat_set_pointer_a11y_settings (CLUTTER_SEAT (priv->seat),
                                          &pointer_a11y_settings);
}

static void
meta_input_mouse_a11y_settings_changed (GSettings  *settings,
                                        const char *key,
                                        gpointer    user_data)
{
  MetaInputSettings *input_settings = META_INPUT_SETTINGS (user_data);

  load_pointer_a11y_settings (input_settings, NULL);
}

static GSettings *
lookup_device_settings (ClutterInputDevice *device)
{
  const gchar *group, *schema, *vendor, *product;
  ClutterInputDeviceType type;
  GSettings *settings;
  gchar *path;

  type = clutter_input_device_get_device_type (device);

  if (type == CLUTTER_TOUCHSCREEN_DEVICE)
    {
      group = "touchscreens";
      schema = "org.gnome.desktop.peripherals.touchscreen";
    }
  else if (type == CLUTTER_TABLET_DEVICE ||
           type == CLUTTER_PEN_DEVICE ||
           type == CLUTTER_ERASER_DEVICE ||
           type == CLUTTER_CURSOR_DEVICE ||
           type == CLUTTER_PAD_DEVICE)
    {
      group = "tablets";
      schema = "org.gnome.desktop.peripherals.tablet";
    }
  else
    return NULL;

  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);
  path = g_strdup_printf ("/org/gnome/desktop/peripherals/%s/%s:%s/",
                          group, vendor, product);

  settings = g_settings_new_with_path (schema, path);
  g_free (path);

  return settings;
}

static GSettings *
lookup_tool_settings (ClutterInputDeviceTool *tool,
                      ClutterInputDevice     *device)
{
  GSettings *tool_settings;
  guint64 serial;
  gchar *path;

  tool_settings = g_object_get_qdata (G_OBJECT (tool), quark_tool_settings);
  if (tool_settings)
    return tool_settings;

  serial = clutter_input_device_tool_get_serial (tool);

  /* The Wacom driver uses serial 1 for serial-less devices but 1 is not a
   * real serial, so let's custom-case this */
  if (serial == 0 || serial == 1)
    {
      path = g_strdup_printf ("/org/gnome/desktop/peripherals/stylus/default-%s:%s/",
                              clutter_input_device_get_vendor_id (device),
                              clutter_input_device_get_product_id (device));
    }
  else
    {
      path = g_strdup_printf ("/org/gnome/desktop/peripherals/stylus/%" G_GINT64_MODIFIER "x/",
                              serial);
    }

  tool_settings =
    g_settings_new_with_path ("org.gnome.desktop.peripherals.tablet.stylus",
                              path);
  g_object_set_qdata_full (G_OBJECT (tool), quark_tool_settings, tool_settings,
                           (GDestroyNotify) g_object_unref);
  g_free (path);

  return tool_settings;
}

static GSettings *
lookup_pad_action_settings (ClutterInputDevice *device,
                            MetaPadActionType   action,
                            guint               number,
                            MetaPadDirection    direction,
                            gint                mode)
{
  const gchar *vendor, *product, *action_type, *detail_type = NULL;
  GSettings *settings;
  GString *path;
  gchar action_label;

  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);

  action_label = 'A' + number;

  switch (action)
    {
    case META_PAD_ACTION_BUTTON:
      action_type = "button";
      break;
    case META_PAD_ACTION_RING:
      g_assert (direction == META_PAD_DIRECTION_CW ||
                direction == META_PAD_DIRECTION_CCW);
      action_type = "ring";
      detail_type = (direction == META_PAD_DIRECTION_CW) ? "cw" : "ccw";
      break;
    case META_PAD_ACTION_STRIP:
      g_assert (direction == META_PAD_DIRECTION_UP ||
                direction == META_PAD_DIRECTION_DOWN);
      action_type = "strip";
      detail_type = (direction == META_PAD_DIRECTION_UP) ? "up" : "down";
      break;
    default:
      return NULL;
    }

  path = g_string_new (NULL);
  g_string_append_printf (path, "/org/gnome/desktop/peripherals/tablets/%s:%s/%s%c",
                          vendor, product, action_type, action_label);

  if (detail_type)
    g_string_append_printf (path, "-%s", detail_type);

  if (mode >= 0)
    g_string_append_printf (path, "-mode-%d", mode);

  g_string_append_c (path, '/');

  settings = g_settings_new_with_path ("org.gnome.desktop.peripherals.tablet.pad-button",
                                       path->str);
  g_string_free (path, TRUE);

  return settings;
}

static void
monitors_changed_cb (MetaMonitorManager *monitor_manager,
                     MetaInputSettings  *input_settings)
{
  MetaInputSettingsPrivate *priv;
  ClutterInputDevice *device;
  DeviceMappingInfo *info;
  GHashTableIter iter;

  priv = meta_input_settings_get_instance_private (input_settings);
  g_hash_table_iter_init (&iter, priv->mappable_devices);

  while (g_hash_table_iter_next (&iter, (gpointer *) &device,
                                 (gpointer *) &info))
    update_device_display (input_settings, info->settings, device);
}

static void
input_mapper_device_mapped_cb (MetaInputMapper    *mapper,
                               ClutterInputDevice *device,
                               MetaLogicalMonitor *logical_monitor,
                               MetaMonitor        *monitor,
                               MetaInputSettings  *input_settings)
{
  MetaInputSettingsPrivate *priv;
  float matrix[6] = { 1, 0, 0, 0, 1, 0 };

  priv = meta_input_settings_get_instance_private (input_settings);

  if (monitor && logical_monitor)
    {
      meta_monitor_manager_get_monitor_matrix (priv->monitor_manager,
                                               monitor, logical_monitor,
                                               matrix);
    }

  META_INPUT_SETTINGS_GET_CLASS (input_settings)->set_matrix (input_settings,
                                                              device, matrix);
}

static void
device_mapping_info_free (DeviceMappingInfo *info)
{
  g_clear_signal_handler (&info->changed_id, info->settings);
  g_object_unref (info->settings);
  g_free (info->group_modes);
  g_slice_free (DeviceMappingInfo, info);
}

static gboolean
check_add_mappable_device (MetaInputSettings  *input_settings,
                           ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv;
  DeviceMappingInfo *info;
  ClutterInputDeviceType device_type;
  GSettings *settings;

  device_type = clutter_input_device_get_device_type (device);

  if ((device_type == CLUTTER_TABLET_DEVICE ||
       device_type == CLUTTER_PEN_DEVICE ||
       device_type == CLUTTER_ERASER_DEVICE ||
       device_type == CLUTTER_PAD_DEVICE) &&
      g_getenv ("MUTTER_DISABLE_WACOM_CONFIGURATION") != NULL)
    return FALSE;

  settings = lookup_device_settings (device);

  if (!settings)
    return FALSE;

  priv = meta_input_settings_get_instance_private (input_settings);

  info = g_slice_new0 (DeviceMappingInfo);
  info->input_settings = input_settings;
  info->device = device;
  info->settings = settings;

  if (device_type == CLUTTER_PAD_DEVICE)
    {
      info->group_modes =
        g_new0 (guint, clutter_input_device_get_n_mode_groups (device));
    }

  info->changed_id = g_signal_connect (settings, "changed",
                                       G_CALLBACK (mapped_device_changed_cb),
                                       info);

  g_hash_table_insert (priv->mappable_devices, device, info);

  apply_mappable_device_settings (input_settings, info);

  return TRUE;
}

static void
apply_device_settings (MetaInputSettings  *input_settings,
                       ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv =
    meta_input_settings_get_instance_private (input_settings);

  update_device_speed (input_settings, device);
  update_device_natural_scroll (input_settings, device);

  update_mouse_left_handed (input_settings, device);
  update_pointer_accel_profile (input_settings,
                                priv->mouse_settings,
                                device);

  update_touchpad_left_handed (input_settings, device);
  update_touchpad_tap_enabled (input_settings, device);
  update_touchpad_tap_and_drag_enabled (input_settings, device);
  update_touchpad_disable_while_typing (input_settings, device);
  update_touchpad_send_events (input_settings, device);
  update_touchpad_two_finger_scroll (input_settings, device);
  update_touchpad_edge_scroll (input_settings, device);
  update_touchpad_click_method (input_settings, device);

  update_trackball_scroll_button (input_settings, device);
  update_pointer_accel_profile (input_settings,
                                priv->trackball_settings,
                                device);
  load_keyboard_a11y_settings (input_settings, device);
  load_pointer_a11y_settings (input_settings, device);

  update_middle_click_emulation (input_settings, priv->mouse_settings, device);
  update_middle_click_emulation (input_settings, priv->touchpad_settings, device);
  update_middle_click_emulation (input_settings, priv->trackball_settings, device);
}

static void
update_stylus_pressure (MetaInputSettings      *input_settings,
                        ClutterInputDevice     *device,
                        ClutterInputDeviceTool *tool)
{
  MetaInputSettingsClass *input_settings_class;
  GSettings *tool_settings;
  const gint32 *curve;
  GVariant *variant;
  gsize n_elems;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE)
    return;

  if (!tool)
    return;

  tool_settings = lookup_tool_settings (tool, device);

  if (clutter_input_device_tool_get_tool_type (tool) ==
      CLUTTER_INPUT_DEVICE_TOOL_ERASER)
    variant = g_settings_get_value (tool_settings, "eraser-pressure-curve");
  else
    variant = g_settings_get_value (tool_settings, "pressure-curve");

  curve = g_variant_get_fixed_array (variant, &n_elems, sizeof (gint32));
  if (n_elems != 4)
    return;

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  input_settings_class->set_stylus_pressure (input_settings, device, tool, curve);
}

static void
update_stylus_buttonmap (MetaInputSettings      *input_settings,
                         ClutterInputDevice     *device,
                         ClutterInputDeviceTool *tool)
{
  MetaInputSettingsClass *input_settings_class;
  GDesktopStylusButtonAction primary, secondary, tertiary;
  GSettings *tool_settings;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TABLET_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_PEN_DEVICE &&
      clutter_input_device_get_device_type (device) != CLUTTER_ERASER_DEVICE)
    return;

  if (!tool)
    return;

  tool_settings = lookup_tool_settings (tool, device);

  primary = g_settings_get_enum (tool_settings, "button-action");
  secondary = g_settings_get_enum (tool_settings, "secondary-button-action");
  tertiary = g_settings_get_enum (tool_settings, "tertiary-button-action");

  input_settings_class = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  input_settings_class->set_stylus_button_map (input_settings, device, tool,
                                               primary, secondary, tertiary);
}

static void
apply_stylus_settings (MetaInputSettings      *input_settings,
                       ClutterInputDevice     *device,
                       ClutterInputDeviceTool *tool)
{
  update_stylus_pressure (input_settings, device, tool);
  update_stylus_buttonmap (input_settings, device, tool);
}

static void
evaluate_two_finger_scrolling (MetaInputSettings  *input_settings,
                               ClutterInputDevice *device)
{
  MetaInputSettingsClass *klass;
  MetaInputSettingsPrivate *priv;

  if (clutter_input_device_get_device_type (device) != CLUTTER_TOUCHPAD_DEVICE)
    return;

  klass = META_INPUT_SETTINGS_GET_CLASS (input_settings);
  priv = meta_input_settings_get_instance_private (input_settings);

  if (klass->has_two_finger_scroll (input_settings, device))
    g_hash_table_add (priv->two_finger_devices, device);
}

static void
meta_input_settings_device_added (ClutterSeat        *seat,
                                  ClutterInputDevice *device,
                                  MetaInputSettings  *input_settings)
{
  if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
    return;

  evaluate_two_finger_scrolling (input_settings, device);

  apply_device_settings (input_settings, device);
  check_add_mappable_device (input_settings, device);
}

static void
meta_input_settings_device_removed (ClutterSeat        *seat,
                                    ClutterInputDevice *device,
                                    MetaInputSettings  *input_settings)
{
  MetaInputSettingsPrivate *priv;

  priv = meta_input_settings_get_instance_private (input_settings);
  meta_input_mapper_remove_device (priv->input_mapper, device);
  g_hash_table_remove (priv->mappable_devices, device);
  g_hash_table_remove (priv->current_tools, device);

  if (g_hash_table_remove (priv->two_finger_devices, device) &&
      g_hash_table_size (priv->two_finger_devices) == 0)
    apply_device_settings (input_settings, NULL);
}

static void
current_tool_changed_cb (GSettings  *settings,
                         const char *key,
                         gpointer    user_data)
{
  CurrentToolInfo *info = user_data;

  apply_stylus_settings (info->input_settings, info->device, info->tool);
}

static CurrentToolInfo *
current_tool_info_new (MetaInputSettings      *input_settings,
                       ClutterInputDevice     *device,
                       ClutterInputDeviceTool *tool)
{
  CurrentToolInfo *info;

  info = g_new0 (CurrentToolInfo, 1);
  info->input_settings = input_settings;
  info->device = device;
  info->tool = tool;
  info->settings = lookup_tool_settings (tool, device);
  info->changed_id =
    g_signal_connect (info->settings, "changed",
                      G_CALLBACK (current_tool_changed_cb),
                      info);
  return info;
}

static void
current_tool_info_free (CurrentToolInfo *info)
{
  g_clear_signal_handler (&info->changed_id, info->settings);
  g_free (info);
}

static void
meta_input_settings_tool_changed (ClutterSeat            *seat,
                                  ClutterInputDevice     *device,
                                  ClutterInputDeviceTool *tool,
                                  MetaInputSettings      *input_settings)
{
  MetaInputSettingsPrivate *priv;

  priv = meta_input_settings_get_instance_private (input_settings);

  if (tool)
    {
      CurrentToolInfo *current_tool;

      current_tool = current_tool_info_new (input_settings, device, tool);
      g_hash_table_insert (priv->current_tools, device, current_tool);
      apply_stylus_settings (input_settings, device, tool);
    }
  else
    {
      g_hash_table_remove (priv->current_tools, device);
    }
}

static void
check_mappable_devices (MetaInputSettings *input_settings)
{
  MetaInputSettingsPrivate *priv;
  GList *l, *devices;

  priv = meta_input_settings_get_instance_private (input_settings);
  devices = clutter_seat_list_devices (priv->seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
        continue;

      check_add_mappable_device (input_settings, device);
    }

  g_list_free (devices);
}

static void
power_save_mode_changed_cb (MetaMonitorManager *manager,
                            gpointer            user_data)
{
  MetaInputSettingsPrivate *priv;
  ClutterInputDevice *device;
  MetaLogicalMonitor *logical_monitor;
  MetaMonitor *builtin;
  MetaPowerSave power_save_mode;
  gboolean on;

  power_save_mode = meta_monitor_manager_get_power_save_mode (manager);
  on = power_save_mode == META_POWER_SAVE_ON;
  priv = meta_input_settings_get_instance_private (user_data);

  builtin = meta_monitor_manager_get_laptop_panel (manager);
  if (!builtin)
    return;

  logical_monitor = meta_monitor_get_logical_monitor (builtin);
  if (!logical_monitor)
    return;

  device =
    meta_input_mapper_get_logical_monitor_device (priv->input_mapper,
                                                  logical_monitor,
                                                  CLUTTER_TOUCHSCREEN_DEVICE);
  if (!device)
    return;

  clutter_input_device_set_enabled (device, on);
}

static void
meta_input_settings_constructed (GObject *object)
{
  MetaInputSettings *input_settings = META_INPUT_SETTINGS (object);
  GSList *devices, *d;

  devices = meta_input_settings_get_devices (input_settings, CLUTTER_TOUCHPAD_DEVICE);
  for (d = devices; d; d = d->next)
    evaluate_two_finger_scrolling (input_settings, d->data);

  g_slist_free (devices);

  apply_device_settings (input_settings, NULL);
  update_keyboard_repeat (input_settings);
  check_mappable_devices (input_settings);
}

static void
meta_input_settings_class_init (MetaInputSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_input_settings_dispose;
  object_class->constructed = meta_input_settings_constructed;

  quark_tool_settings =
    g_quark_from_static_string ("meta-input-settings-tool-settings");
}

static void
meta_input_settings_init (MetaInputSettings *settings)
{
  MetaInputSettingsPrivate *priv;

  priv = meta_input_settings_get_instance_private (settings);
  priv->seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_connect (priv->seat, "device-added",
                    G_CALLBACK (meta_input_settings_device_added), settings);
  g_signal_connect (priv->seat, "device-removed",
                    G_CALLBACK (meta_input_settings_device_removed), settings);
  g_signal_connect (priv->seat, "tool-changed",
                    G_CALLBACK (meta_input_settings_tool_changed), settings);

  priv->mouse_settings = g_settings_new ("org.gnome.desktop.peripherals.mouse");
  g_signal_connect (priv->mouse_settings, "changed",
                    G_CALLBACK (meta_input_settings_changed_cb), settings);

  priv->touchpad_settings = g_settings_new ("org.gnome.desktop.peripherals.touchpad");
  g_signal_connect (priv->touchpad_settings, "changed",
                    G_CALLBACK (meta_input_settings_changed_cb), settings);

  priv->trackball_settings = g_settings_new ("org.gnome.desktop.peripherals.trackball");
  g_signal_connect (priv->trackball_settings, "changed",
                    G_CALLBACK (meta_input_settings_changed_cb), settings);

  priv->keyboard_settings = g_settings_new ("org.gnome.desktop.peripherals.keyboard");
  g_signal_connect (priv->keyboard_settings, "changed",
                    G_CALLBACK (meta_input_settings_changed_cb), settings);

  priv->gsd_settings = g_settings_new ("org.gnome.settings-daemon.peripherals.mouse");

  g_settings_bind (priv->gsd_settings, "double-click",
                   clutter_settings_get_default(), "double-click-time",
                   G_SETTINGS_BIND_GET);

  priv->keyboard_a11y_settings = g_settings_new ("org.gnome.desktop.a11y.keyboard");
  g_signal_connect (priv->keyboard_a11y_settings, "changed",
                    G_CALLBACK (meta_input_keyboard_a11y_settings_changed), settings);
  g_signal_connect (priv->seat, "kbd-a11y-flags-changed",
                    G_CALLBACK (on_keyboard_a11y_settings_changed), settings);

  priv->mouse_a11y_settings = g_settings_new ("org.gnome.desktop.a11y.mouse");
  g_signal_connect (priv->mouse_a11y_settings, "changed",
                    G_CALLBACK (meta_input_mouse_a11y_settings_changed), settings);

  priv->mappable_devices =
    g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) device_mapping_info_free);

  priv->current_tools =
    g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) current_tool_info_free);

  priv->monitor_manager = g_object_ref (meta_monitor_manager_get ());
  g_signal_connect (priv->monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (monitors_changed_cb), settings);
  g_signal_connect (priv->monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (power_save_mode_changed_cb), settings);

  priv->two_finger_devices = g_hash_table_new (NULL, NULL);

  priv->input_mapper = meta_input_mapper_new ();
  g_signal_connect (priv->input_mapper, "device-mapped",
                    G_CALLBACK (input_mapper_device_mapped_cb), settings);
}

GSettings *
meta_input_settings_get_tablet_settings (MetaInputSettings  *settings,
                                         ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv;
  DeviceMappingInfo *info;

  g_return_val_if_fail (META_IS_INPUT_SETTINGS (settings), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  priv = meta_input_settings_get_instance_private (settings);
  info = g_hash_table_lookup (priv->mappable_devices, device);

  return info ? g_object_ref (info->settings) : NULL;
}

static ClutterInputDevice *
find_grouped_pen (MetaInputSettings  *settings,
                  ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv;
  GList *l, *devices;
  ClutterInputDeviceType device_type;
  ClutterInputDevice *pen = NULL;

  device_type = clutter_input_device_get_device_type (device);

  if (device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE)
    return device;

  priv = meta_input_settings_get_instance_private (settings);
  devices = clutter_seat_list_devices (priv->seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *other_device = l->data;

      device_type = clutter_input_device_get_device_type (other_device);

      if ((device_type == CLUTTER_TABLET_DEVICE ||
           device_type == CLUTTER_PEN_DEVICE) &&
          clutter_input_device_is_grouped (device, other_device))
        {
          pen = other_device;
          break;
        }
    }

  g_list_free (devices);

  return pen;
}

MetaLogicalMonitor *
meta_input_settings_get_tablet_logical_monitor (MetaInputSettings  *settings,
                                                ClutterInputDevice *device)
{
  MetaLogicalMonitor *logical_monitor = NULL;
  MetaInputSettingsPrivate *priv;
  DeviceMappingInfo *info;

  g_return_val_if_fail (META_IS_INPUT_SETTINGS (settings), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  if (clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE)
    {
      device = find_grouped_pen (settings, device);
      if (!device)
        return NULL;
    }

  priv = meta_input_settings_get_instance_private (settings);
  info = g_hash_table_lookup (priv->mappable_devices, device);
  if (!info)
    return NULL;

  logical_monitor =
    meta_input_mapper_get_device_logical_monitor (priv->input_mapper, device);

  if (!logical_monitor)
    {
      meta_input_settings_find_monitor (settings, info->settings, device,
                                        NULL, &logical_monitor);
    }

  return logical_monitor;
}

GDesktopTabletMapping
meta_input_settings_get_tablet_mapping (MetaInputSettings  *settings,
                                        ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv;
  DeviceMappingInfo *info;

  g_return_val_if_fail (META_IS_INPUT_SETTINGS (settings),
                        G_DESKTOP_TABLET_MAPPING_ABSOLUTE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        G_DESKTOP_TABLET_MAPPING_ABSOLUTE);

  priv = meta_input_settings_get_instance_private (settings);
  info = g_hash_table_lookup (priv->mappable_devices, device);
  g_return_val_if_fail (info != NULL, G_DESKTOP_TABLET_MAPPING_ABSOLUTE);

  return g_settings_get_enum (info->settings, "mapping");
}

static GDesktopPadButtonAction
meta_input_settings_get_pad_button_action (MetaInputSettings   *input_settings,
                                           ClutterInputDevice  *pad,
                                           guint                button)
{
  GDesktopPadButtonAction action;
  GSettings *settings;

  g_return_val_if_fail (META_IS_INPUT_SETTINGS (input_settings),
                        G_DESKTOP_PAD_BUTTON_ACTION_NONE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad),
                        G_DESKTOP_PAD_BUTTON_ACTION_NONE);

  settings = lookup_pad_action_settings (pad, META_PAD_ACTION_BUTTON,
                                         button, META_PAD_DIRECTION_NONE, -1);
  action = g_settings_get_enum (settings, "action");
  g_object_unref (settings);

  return action;
}

static gboolean
cycle_logical_monitors (MetaInputSettings   *settings,
                        MetaLogicalMonitor  *current_logical_monitor,
                        MetaLogicalMonitor **next_logical_monitor)
{
  MetaInputSettingsPrivate *priv =
    meta_input_settings_get_instance_private (settings);
  MetaMonitorManager *monitor_manager = priv->monitor_manager;
  GList *logical_monitors;

  /* We cycle between:
   * - the span of all monitors (current_output = NULL)
   * - each monitor individually.
   */

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  if (!current_logical_monitor)
    {
      *next_logical_monitor = logical_monitors->data;
    }
  else
    {
      GList *l;

      l = g_list_find (logical_monitors, current_logical_monitor);
      if (l->next)
        *next_logical_monitor = l->next->data;
      else
        *next_logical_monitor = NULL;
    }

  return TRUE;
}

static void
meta_input_settings_cycle_tablet_output (MetaInputSettings  *input_settings,
                                         ClutterInputDevice *device)
{
  MetaInputSettingsPrivate *priv;
  DeviceMappingInfo *info;
  MetaLogicalMonitor *logical_monitor = NULL;
  const gchar *edid[4] = { 0 }, *pretty_name = NULL;
#ifdef HAVE_LIBWACOM
  WacomDevice *wacom_device;
#endif

  g_return_if_fail (META_IS_INPUT_SETTINGS (input_settings));
  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (clutter_input_device_get_device_type (device) == CLUTTER_TABLET_DEVICE ||
                    clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE);

  priv = meta_input_settings_get_instance_private (input_settings);
  info = g_hash_table_lookup (priv->mappable_devices, device);
  g_return_if_fail (info != NULL);

#ifdef HAVE_LIBWACOM
  wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

  if (wacom_device)
    {
      /* Output rotation only makes sense on external tablets */
      if (libwacom_get_integration_flags (wacom_device) != WACOM_DEVICE_INTEGRATED_NONE)
        return;

      pretty_name = libwacom_get_name (wacom_device);
    }
#endif

  meta_input_settings_find_monitor (input_settings, info->settings, device,
                                    NULL, &logical_monitor);

  if (!cycle_logical_monitors (input_settings,
                               logical_monitor,
                               &logical_monitor))
    return;

  if (logical_monitor)
    {
      MetaMonitor *monitor;

      /* Pick an arbitrary monitor in the logical monitor to represent it. */
      monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
      edid[0] = meta_monitor_get_vendor (monitor);
      edid[1] = meta_monitor_get_product (monitor);
      edid[2] = meta_monitor_get_serial (monitor);
    }
  else
    {
      edid[0] = "";
      edid[1] = "";
      edid[2] = "";
    }
  g_settings_set_strv (info->settings, "output", edid);

  meta_display_show_tablet_mapping_notification (meta_get_display (),
                                                 device, pretty_name);
}

static void
emulate_modifiers (ClutterVirtualInputDevice *device,
                   ClutterModifierType        mods,
                   ClutterKeyState            state)
{
  guint i;
  struct {
    ClutterModifierType mod;
    guint keyval;
  } mod_map[] = {
    { CLUTTER_SHIFT_MASK, CLUTTER_KEY_Shift_L },
    { CLUTTER_CONTROL_MASK, CLUTTER_KEY_Control_L },
    { CLUTTER_MOD1_MASK, CLUTTER_KEY_Meta_L }
  };

  for (i = 0; i < G_N_ELEMENTS (mod_map); i++)
    {
      if ((mods & mod_map[i].mod) == 0)
        continue;

      clutter_virtual_input_device_notify_keyval (device,
                                                  clutter_get_current_event_time (),
                                                  mod_map[i].keyval, state);
    }
}

static void
meta_input_settings_emulate_keybinding (MetaInputSettings  *input_settings,
                                        const gchar        *accel,
                                        gboolean            is_press)
{
  MetaInputSettingsPrivate *priv;
  ClutterKeyState state;
  guint key, mods;

  if (!accel || !*accel)
    return;

  priv = meta_input_settings_get_instance_private (input_settings);

  /* FIXME: This is appalling */
  gtk_accelerator_parse (accel, &key, &mods);

  if (!priv->virtual_pad_keyboard)
    {
      ClutterBackend *backend;
      ClutterSeat *seat;

      backend = clutter_get_default_backend ();
      seat = clutter_backend_get_default_seat (backend);

      priv->virtual_pad_keyboard =
        clutter_seat_create_virtual_device (seat,
                                            CLUTTER_KEYBOARD_DEVICE);
    }

  state = is_press ? CLUTTER_KEY_STATE_PRESSED : CLUTTER_KEY_STATE_RELEASED;

  if (is_press)
    emulate_modifiers (priv->virtual_pad_keyboard, mods, state);

  clutter_virtual_input_device_notify_keyval (priv->virtual_pad_keyboard,
                                              clutter_get_current_event_time (),
                                              key, state);
  if (!is_press)
    emulate_modifiers (priv->virtual_pad_keyboard, mods, state);
}

gboolean
meta_input_settings_is_pad_button_grabbed (MetaInputSettings  *input_settings,
                                           ClutterInputDevice *pad,
                                           guint               button)
{
  g_return_val_if_fail (META_IS_INPUT_SETTINGS (input_settings), FALSE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad), FALSE);
  g_return_val_if_fail (clutter_input_device_get_device_type (pad) ==
                        CLUTTER_PAD_DEVICE, FALSE);

  return (meta_input_settings_get_pad_button_action (input_settings, pad, button) !=
          G_DESKTOP_PAD_BUTTON_ACTION_NONE);
}

static gboolean
meta_input_settings_handle_pad_button (MetaInputSettings           *input_settings,
                                       ClutterInputDevice          *pad,
                                       const ClutterPadButtonEvent *event)
{
  GDesktopPadButtonAction action;
  gint button, group, mode;
  gboolean is_press;
  GSettings *settings;
  gchar *accel;

  g_return_val_if_fail (META_IS_INPUT_SETTINGS (input_settings), FALSE);
  g_return_val_if_fail (event->type == CLUTTER_PAD_BUTTON_PRESS ||
                        event->type == CLUTTER_PAD_BUTTON_RELEASE, FALSE);

  button = event->button;
  mode = event->mode;
  group = clutter_input_device_get_mode_switch_button_group (pad, button);
  is_press = event->type == CLUTTER_PAD_BUTTON_PRESS;

  if (is_press && group >= 0)
    {
      guint n_modes = clutter_input_device_get_group_n_modes (pad, group);
      const gchar *pretty_name = NULL;
      MetaInputSettingsPrivate *priv;
      DeviceMappingInfo *info;
#ifdef HAVE_LIBWACOM
      WacomDevice *wacom_device;
#endif

      priv = meta_input_settings_get_instance_private (input_settings);
      info = g_hash_table_lookup (priv->mappable_devices, pad);

#ifdef HAVE_LIBWACOM
      wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (pad));

      if (wacom_device)
        pretty_name = libwacom_get_name (wacom_device);
#endif
      meta_display_notify_pad_group_switch (meta_get_display (), pad,
                                            pretty_name, group, mode, n_modes);
      info->group_modes[group] = mode;
    }

  action = meta_input_settings_get_pad_button_action (input_settings, pad, button);

  switch (action)
    {
    case G_DESKTOP_PAD_BUTTON_ACTION_SWITCH_MONITOR:
      if (is_press)
        meta_input_settings_cycle_tablet_output (input_settings, pad);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_HELP:
      if (is_press)
        meta_display_request_pad_osd (meta_get_display (), pad, FALSE);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_KEYBINDING:
      settings = lookup_pad_action_settings (pad, META_PAD_ACTION_BUTTON,
                                             button, META_PAD_DIRECTION_NONE, -1);
      accel = g_settings_get_string (settings, "keybinding");
      meta_input_settings_emulate_keybinding (input_settings, accel, is_press);
      g_object_unref (settings);
      g_free (accel);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_NONE:
    default:
      return FALSE;
    }
}

static gboolean
meta_input_settings_handle_pad_action (MetaInputSettings         *input_settings,
                                       ClutterInputDevice        *pad,
                                       MetaPadActionType          action,
                                       guint                      number,
                                       MetaPadDirection           direction,
                                       guint                      mode)
{
  GSettings *settings;
  gboolean handled = FALSE;
  gchar *accel;

  settings = lookup_pad_action_settings (pad, action, number, direction, mode);
  accel = g_settings_get_string (settings, "keybinding");

  if (accel && *accel)
    {
      meta_input_settings_emulate_keybinding (input_settings, accel, TRUE);
      meta_input_settings_emulate_keybinding (input_settings, accel, FALSE);
      handled = TRUE;
    }

  g_object_unref (settings);
  g_free (accel);

  return handled;
}

static gboolean
meta_input_settings_get_pad_action_direction (MetaInputSettings  *input_settings,
                                              const ClutterEvent *event,
                                              MetaPadDirection   *direction)
{
  MetaInputSettingsPrivate *priv;
  ClutterInputDevice *pad = clutter_event_get_device (event);
  MetaPadActionType pad_action;
  gboolean has_direction = FALSE;
  MetaPadDirection inc_dir, dec_dir;
  guint number;
  gdouble value;

  priv = meta_input_settings_get_instance_private (input_settings);
  *direction = META_PAD_DIRECTION_NONE;

  switch (event->type)
    {
    case CLUTTER_PAD_RING:
      pad_action = META_PAD_ACTION_RING;
      number = event->pad_ring.ring_number;
      value = event->pad_ring.angle;
      inc_dir = META_PAD_DIRECTION_CW;
      dec_dir = META_PAD_DIRECTION_CCW;
      break;
    case CLUTTER_PAD_STRIP:
      pad_action = META_PAD_ACTION_STRIP;
      number = event->pad_strip.strip_number;
      value = event->pad_strip.value;
      inc_dir = META_PAD_DIRECTION_DOWN;
      dec_dir = META_PAD_DIRECTION_UP;
      break;
    default:
      return FALSE;
    }

  if (priv->last_pad_action_info.pad == pad &&
      priv->last_pad_action_info.action == pad_action &&
      priv->last_pad_action_info.number == number &&
      value >= 0 && priv->last_pad_action_info.value >= 0)
    {
      *direction = (value - priv->last_pad_action_info.value) > 0 ?
        inc_dir : dec_dir;
      has_direction = TRUE;
    }

  priv->last_pad_action_info.pad = pad;
  priv->last_pad_action_info.action = pad_action;
  priv->last_pad_action_info.number = number;
  priv->last_pad_action_info.value = value;
  return has_direction;
}

gboolean
meta_input_settings_handle_pad_event (MetaInputSettings  *input_settings,
                                      const ClutterEvent *event)
{
  ClutterInputDevice *pad;
  MetaPadDirection direction = META_PAD_DIRECTION_NONE;

  pad = clutter_event_get_source_device ((ClutterEvent *) event);

  switch (event->type)
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      return meta_input_settings_handle_pad_button (input_settings, pad,
                                                    &event->pad_button);
    case CLUTTER_PAD_RING:
      if (!meta_input_settings_get_pad_action_direction (input_settings,
                                                         event, &direction))
        return FALSE;
      return meta_input_settings_handle_pad_action (input_settings, pad,
                                                    META_PAD_ACTION_RING,
                                                    event->pad_ring.ring_number,
                                                    direction,
                                                    event->pad_ring.mode);
    case CLUTTER_PAD_STRIP:
      if (!meta_input_settings_get_pad_action_direction (input_settings,
                                                         event, &direction))
        return FALSE;
      return meta_input_settings_handle_pad_action (input_settings, pad,
                                                    META_PAD_ACTION_STRIP,
                                                    event->pad_strip.strip_number,
                                                    direction,
                                                    event->pad_strip.mode);
    default:
      return FALSE;
    }
}

static gchar *
compose_directional_action_label (GSettings *direction1,
                                  GSettings *direction2)
{
  gchar *accel1, *accel2, *str = NULL;

  accel1 = g_settings_get_string (direction1, "keybinding");
  accel2 = g_settings_get_string (direction2, "keybinding");

  if (accel1 && *accel1 && accel2 && *accel2)
    str = g_strdup_printf ("%s / %s", accel1, accel2);

  g_free (accel1);
  g_free (accel2);

  return str;
}

static gchar *
meta_input_settings_get_ring_label (MetaInputSettings  *settings,
                                    ClutterInputDevice *pad,
                                    guint               number,
                                    guint               mode)
{
  GSettings *settings1, *settings2;
  gchar *label;

  /* We only allow keybinding actions with those */
  settings1 = lookup_pad_action_settings (pad, META_PAD_ACTION_RING, number,
                                          META_PAD_DIRECTION_CW, mode);
  settings2 = lookup_pad_action_settings (pad, META_PAD_ACTION_RING, number,
                                          META_PAD_DIRECTION_CCW, mode);
  label = compose_directional_action_label (settings1, settings2);
  g_object_unref (settings1);
  g_object_unref (settings2);

  return label;
}

static gchar *
meta_input_settings_get_strip_label (MetaInputSettings  *settings,
                                     ClutterInputDevice *pad,
                                     guint               number,
                                     guint               mode)
{
  GSettings *settings1, *settings2;
  gchar *label;

  /* We only allow keybinding actions with those */
  settings1 = lookup_pad_action_settings (pad, META_PAD_ACTION_STRIP, number,
                                          META_PAD_DIRECTION_UP, mode);
  settings2 = lookup_pad_action_settings (pad, META_PAD_ACTION_STRIP, number,
                                          META_PAD_DIRECTION_DOWN, mode);
  label = compose_directional_action_label (settings1, settings2);
  g_object_unref (settings1);
  g_object_unref (settings2);

  return label;
}

static gchar *
meta_input_settings_get_button_label (MetaInputSettings  *input_settings,
                                      ClutterInputDevice *pad,
                                      guint               button)
{
  GDesktopPadButtonAction action;
  gint group;

  g_return_val_if_fail (META_IS_INPUT_SETTINGS (input_settings), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_type (pad) ==
                        CLUTTER_PAD_DEVICE, NULL);

  group = clutter_input_device_get_mode_switch_button_group (pad, button);

  if (group >= 0)
    {
      /* TRANSLATORS: This string refers to a button that switches between
       * different modes.
       */
      return g_strdup_printf (_("Mode Switch (Group %d)"), group);
    }

  action = meta_input_settings_get_pad_button_action (input_settings, pad, button);

  switch (action)
    {
    case G_DESKTOP_PAD_BUTTON_ACTION_KEYBINDING:
      {
        GSettings *settings;
        gchar *accel;

        settings = lookup_pad_action_settings (pad, META_PAD_ACTION_BUTTON,
                                               button, META_PAD_DIRECTION_NONE, -1);
        accel = g_settings_get_string (settings, "keybinding");
        g_object_unref (settings);

        return accel;
      }
    case G_DESKTOP_PAD_BUTTON_ACTION_SWITCH_MONITOR:
      /* TRANSLATORS: This string refers to an action, cycles drawing tablets'
       * mapping through the available outputs.
       */
      return g_strdup (_("Switch monitor"));
    case G_DESKTOP_PAD_BUTTON_ACTION_HELP:
      return g_strdup (_("Show on-screen help"));
    case G_DESKTOP_PAD_BUTTON_ACTION_NONE:
    default:
      return NULL;
    }
}

static guint
get_current_pad_mode (MetaInputSettings  *input_settings,
                      ClutterInputDevice *pad,
                      MetaPadActionType   action_type,
                      guint               number)
{
  MetaInputSettingsPrivate *priv;
  DeviceMappingInfo *info;
  guint group = 0, n_groups;

  priv = meta_input_settings_get_instance_private (input_settings);
  info = g_hash_table_lookup (priv->mappable_devices, pad);
  n_groups = clutter_input_device_get_n_mode_groups (pad);

  if (!info->group_modes || n_groups == 0)
    return 0;

  if (action_type == META_PAD_ACTION_RING ||
      action_type == META_PAD_ACTION_STRIP)
    {
      /* Assume features are evenly distributed in groups */
      group = number % n_groups;
    }

  return info->group_modes[group];
}

gchar *
meta_input_settings_get_pad_action_label (MetaInputSettings  *input_settings,
                                          ClutterInputDevice *pad,
                                          MetaPadActionType   action_type,
                                          guint               number)
{
  guint mode;

  switch (action_type)
    {
    case META_PAD_ACTION_BUTTON:
      return meta_input_settings_get_button_label (input_settings, pad, number);
    case META_PAD_ACTION_RING:
      mode = get_current_pad_mode (input_settings, pad, action_type, number);
      return meta_input_settings_get_ring_label (input_settings, pad,
                                                 number, mode);
    case META_PAD_ACTION_STRIP:
      mode = get_current_pad_mode (input_settings, pad, action_type, number);
      return meta_input_settings_get_strip_label (input_settings, pad,
                                                  number, mode);
    }

  return NULL;
}

void
meta_input_settings_maybe_save_numlock_state (MetaInputSettings *input_settings)
{
  MetaInputSettingsPrivate *priv;
  ClutterSeat *seat;
  ClutterKeymap *keymap;
  gboolean numlock_state;

  priv = meta_input_settings_get_instance_private (input_settings);

  if (!g_settings_get_boolean (priv->keyboard_settings, "remember-numlock-state"))
    return;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keymap = clutter_seat_get_keymap (seat);
  numlock_state = clutter_keymap_get_num_lock_state (keymap);

  if (numlock_state == g_settings_get_boolean (priv->keyboard_settings, "numlock-state"))
    return;

  g_settings_set_boolean (priv->keyboard_settings, "numlock-state", numlock_state);
}

void
meta_input_settings_maybe_restore_numlock_state (MetaInputSettings *input_settings)
{
  MetaInputSettingsPrivate *priv;
  gboolean numlock_state;

  priv = meta_input_settings_get_instance_private (input_settings);

  if (!g_settings_get_boolean (priv->keyboard_settings, "remember-numlock-state"))
    return;

  numlock_state = g_settings_get_boolean (priv->keyboard_settings, "numlock-state");
  meta_backend_set_numlock (meta_get_backend (), numlock_state);
}
