/*
 * Copyright © 2001 Ximian, Inc.
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2017 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <X11/XKBlib.h>
#include <X11/extensions/XKBstr.h>

#include "backends/x11/meta-xkb-a11y-x11.h"
#include "clutter/x11/clutter-x11.h"
#include "core/display-private.h"
#include "meta/meta-x11-errors.h"

#define DEFAULT_XKB_SET_CONTROLS_MASK XkbSlowKeysMask         | \
                                      XkbBounceKeysMask       | \
                                      XkbStickyKeysMask       | \
                                      XkbMouseKeysMask        | \
                                      XkbMouseKeysAccelMask   | \
                                      XkbAccessXKeysMask      | \
                                      XkbAccessXTimeoutMask   | \
                                      XkbAccessXFeedbackMask  | \
                                      XkbControlsEnabledMask

static int _xkb_event_base;

static XkbDescRec *
get_xkb_desc_rec (Display *xdisplay)
{
  XkbDescRec *desc;
  Status      status = Success;

  clutter_x11_trap_x_errors ();
  desc = XkbGetMap (xdisplay, XkbAllMapComponentsMask, XkbUseCoreKbd);
  if (desc != NULL)
    {
      desc->ctrls = NULL;
      status = XkbGetControls (xdisplay, XkbAllControlsMask, desc);
    }
  clutter_x11_untrap_x_errors ();

  g_return_val_if_fail (desc != NULL, NULL);
  g_return_val_if_fail (desc->ctrls != NULL, NULL);
  g_return_val_if_fail (status == Success, NULL);

  return desc;
}

static void
set_xkb_desc_rec (Display    *xdisplay,
                  XkbDescRec *desc)
{
  clutter_x11_trap_x_errors ();
  XkbSetControls (xdisplay, DEFAULT_XKB_SET_CONTROLS_MASK, desc);
  XSync (xdisplay, FALSE);
  clutter_x11_untrap_x_errors ();
}

static void
check_settings_changed (ClutterSeat *seat)
{
  Display *xdisplay = clutter_x11_get_default_display ();
  ClutterKbdA11ySettings kbd_a11y_settings;
  ClutterKeyboardA11yFlags what_changed = 0;
  XkbDescRec *desc;

  desc = get_xkb_desc_rec (xdisplay);
  if (!desc)
    return;

  clutter_seat_get_kbd_a11y_settings (seat, &kbd_a11y_settings);

  if (desc->ctrls->enabled_ctrls & XkbSlowKeysMask &&
      !(kbd_a11y_settings.controls & CLUTTER_A11Y_SLOW_KEYS_ENABLED))
    {
      what_changed |= CLUTTER_A11Y_SLOW_KEYS_ENABLED;
      kbd_a11y_settings.controls |= CLUTTER_A11Y_SLOW_KEYS_ENABLED;
    }
  else if (!(desc->ctrls->enabled_ctrls & XkbSlowKeysMask) &&
           kbd_a11y_settings.controls & CLUTTER_A11Y_SLOW_KEYS_ENABLED)
    {
      what_changed |= CLUTTER_A11Y_SLOW_KEYS_ENABLED;
      kbd_a11y_settings.controls &= ~CLUTTER_A11Y_SLOW_KEYS_ENABLED;
    }

  if (desc->ctrls->enabled_ctrls & XkbStickyKeysMask &&
      !(kbd_a11y_settings.controls & CLUTTER_A11Y_STICKY_KEYS_ENABLED))
    {
      what_changed |= CLUTTER_A11Y_STICKY_KEYS_ENABLED;
      kbd_a11y_settings.controls |= CLUTTER_A11Y_STICKY_KEYS_ENABLED;
    }
  else if (!(desc->ctrls->enabled_ctrls & XkbStickyKeysMask) &&
           kbd_a11y_settings.controls & CLUTTER_A11Y_STICKY_KEYS_ENABLED)
    {
      what_changed |= CLUTTER_A11Y_STICKY_KEYS_ENABLED;
      kbd_a11y_settings.controls &= ~CLUTTER_A11Y_STICKY_KEYS_ENABLED;
    }

  if (what_changed)
    g_signal_emit_by_name (seat,
                           "kbd-a11y-flags-changed",
                           kbd_a11y_settings.controls,
                           what_changed);

  XkbFreeKeyboard (desc, XkbAllComponentsMask, TRUE);
}

static ClutterX11FilterReturn
xkb_a11y_event_filter (XEvent       *xevent,
                       ClutterEvent *clutter_event,
                       gpointer      data)
{
  ClutterSeat *seat = CLUTTER_SEAT (data);
  XkbEvent *xkbev = (XkbEvent *) xevent;

  /* 'event_type' is set to zero on notifying us of updates in
   * response to client requests (including our own) and non-zero
   * to notify us of key/mouse events causing changes (like
   * pressing shift 5 times to enable sticky keys).
   *
   * We only want to update out settings when it's in response to an
   * explicit user input event, so require a non-zero event_type.
   */
  if (xevent->xany.type == (_xkb_event_base + XkbEventCode) &&
      xkbev->any.xkb_type == XkbControlsNotify && xkbev->ctrls.event_type != 0)
    check_settings_changed (seat);

  return CLUTTER_X11_FILTER_CONTINUE;
}

static gboolean
is_xkb_available (Display *xdisplay)
{
  int opcode, error_base, event_base, major, minor;

  if (_xkb_event_base)
    return TRUE;

  if (!XkbQueryExtension (xdisplay,
                          &opcode,
                          &event_base,
                          &error_base,
                          &major,
                          &minor))
    return FALSE;

  if (!XkbUseExtension (xdisplay, &major, &minor))
    return FALSE;

  _xkb_event_base = event_base;

  return TRUE;
}

static unsigned long
set_value_mask (gboolean      flag,
                unsigned long value,
                unsigned long mask)
{
  if (flag)
    return value | mask;

  return value & ~mask;
}

static gboolean
set_xkb_ctrl (XkbDescRec               *desc,
              ClutterKeyboardA11yFlags settings,
              ClutterKeyboardA11yFlags flag,
              unsigned long            mask)
{
  gboolean result = (settings & flag) == flag;
  desc->ctrls->enabled_ctrls = set_value_mask (result, desc->ctrls->enabled_ctrls, mask);

  return result;
}

void
meta_seat_x11_apply_kbd_a11y_settings (ClutterSeat            *seat,
                                       ClutterKbdA11ySettings *kbd_a11y_settings)
{
  Display *xdisplay = clutter_x11_get_default_display ();
  XkbDescRec *desc;
  gboolean enable_accessX;

  desc = get_xkb_desc_rec (xdisplay);
  if (!desc)
    return;

  /* general */
  enable_accessX = kbd_a11y_settings->controls & CLUTTER_A11Y_KEYBOARD_ENABLED;

  desc->ctrls->enabled_ctrls = set_value_mask (enable_accessX,
                                               desc->ctrls->enabled_ctrls,
                                               XkbAccessXKeysMask);

  if (set_xkb_ctrl (desc, kbd_a11y_settings->controls, CLUTTER_A11Y_TIMEOUT_ENABLED,
                    XkbAccessXTimeoutMask))
    {
      desc->ctrls->ax_timeout = kbd_a11y_settings->timeout_delay;
      /* disable only the master flag via the server we will disable
       * the rest on the rebound without affecting settings state
       * don't change the option flags at all.
       */
      desc->ctrls->axt_ctrls_mask = XkbAccessXKeysMask | XkbAccessXFeedbackMask;
      desc->ctrls->axt_ctrls_values = 0;
      desc->ctrls->axt_opts_mask = 0;
    }

  desc->ctrls->ax_options =
    set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_FEATURE_STATE_CHANGE_BEEP,
                    desc->ctrls->ax_options,
                    XkbAccessXFeedbackMask | XkbAX_FeatureFBMask | XkbAX_SlowWarnFBMask);

  /* bounce keys */
  if (set_xkb_ctrl (desc, kbd_a11y_settings->controls,
                    CLUTTER_A11Y_BOUNCE_KEYS_ENABLED, XkbBounceKeysMask))
    {
      desc->ctrls->debounce_delay = kbd_a11y_settings->debounce_delay;
      desc->ctrls->ax_options =
        set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_BOUNCE_KEYS_BEEP_REJECT,
                        desc->ctrls->ax_options,
                        XkbAccessXFeedbackMask | XkbAX_BKRejectFBMask);
    }

  /* mouse keys */
  if (clutter_keymap_get_num_lock_state (clutter_seat_get_keymap (seat)))
    {
      /* Disable mousekeys when NumLock is ON */
      desc->ctrls->enabled_ctrls &= ~(XkbMouseKeysMask | XkbMouseKeysAccelMask);
    }
  else if (set_xkb_ctrl (desc, kbd_a11y_settings->controls,
                         CLUTTER_A11Y_MOUSE_KEYS_ENABLED, XkbMouseKeysMask | XkbMouseKeysAccelMask))
    {
      int mk_max_speed;
      int mk_accel_time;

      desc->ctrls->mk_interval     = 100;     /* msec between mousekey events */
      desc->ctrls->mk_curve        = 50;

      /* We store pixels / sec, XKB wants pixels / event */
      mk_max_speed = kbd_a11y_settings->mousekeys_max_speed;
      desc->ctrls->mk_max_speed = mk_max_speed / (1000 / desc->ctrls->mk_interval);
      if (desc->ctrls->mk_max_speed <= 0)
        desc->ctrls->mk_max_speed = 1;

      mk_accel_time = kbd_a11y_settings->mousekeys_accel_time;
      desc->ctrls->mk_time_to_max = mk_accel_time / desc->ctrls->mk_interval;

      if (desc->ctrls->mk_time_to_max <= 0)
        desc->ctrls->mk_time_to_max = 1;

      desc->ctrls->mk_delay = kbd_a11y_settings->mousekeys_init_delay;
    }

  /* slow keys */
  if (set_xkb_ctrl (desc, kbd_a11y_settings->controls,
                    CLUTTER_A11Y_SLOW_KEYS_ENABLED, XkbSlowKeysMask))
    {
      desc->ctrls->ax_options =
        set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_SLOW_KEYS_BEEP_PRESS,
                        desc->ctrls->ax_options, XkbAccessXFeedbackMask | XkbAX_SKPressFBMask);
      desc->ctrls->ax_options =
        set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_SLOW_KEYS_BEEP_ACCEPT,
                        desc->ctrls->ax_options, XkbAccessXFeedbackMask | XkbAX_SKAcceptFBMask);
      desc->ctrls->ax_options =
        set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_SLOW_KEYS_BEEP_REJECT,
                        desc->ctrls->ax_options, XkbAccessXFeedbackMask | XkbAX_SKRejectFBMask);
      desc->ctrls->slow_keys_delay = kbd_a11y_settings->slowkeys_delay;
      /* anything larger than 500 seems to loose all keyboard input */
      if (desc->ctrls->slow_keys_delay > 500)
        desc->ctrls->slow_keys_delay = 500;
    }

  /* sticky keys */
  if (set_xkb_ctrl (desc, kbd_a11y_settings->controls,
                    CLUTTER_A11Y_STICKY_KEYS_ENABLED, XkbStickyKeysMask))
  {
    desc->ctrls->ax_options |= XkbAX_LatchToLockMask;
    desc->ctrls->ax_options =
      set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_STICKY_KEYS_TWO_KEY_OFF,
                      desc->ctrls->ax_options, XkbAccessXFeedbackMask | XkbAX_TwoKeysMask);
    desc->ctrls->ax_options =
      set_value_mask (kbd_a11y_settings->controls &  CLUTTER_A11Y_STICKY_KEYS_BEEP,
                      desc->ctrls->ax_options, XkbAccessXFeedbackMask | XkbAX_StickyKeysFBMask);
  }

  /* toggle keys */
  desc->ctrls->ax_options =
    set_value_mask (kbd_a11y_settings->controls & CLUTTER_A11Y_TOGGLE_KEYS_ENABLED,
                    desc->ctrls->ax_options, XkbAccessXFeedbackMask | XkbAX_IndicatorFBMask);

  set_xkb_desc_rec (xdisplay, desc);
  XkbFreeKeyboard (desc, XkbAllComponentsMask, TRUE);
}

gboolean
meta_seat_x11_a11y_init (ClutterSeat *seat)
{
  Display *xdisplay = clutter_x11_get_default_display ();
  guint event_mask;

  if (!is_xkb_available (xdisplay))
    return FALSE;

  event_mask = XkbControlsNotifyMask | XkbAccessXNotifyMask;

  XkbSelectEvents (xdisplay, XkbUseCoreKbd, event_mask, event_mask);

  clutter_x11_add_filter (xkb_a11y_event_filter, seat);

  return TRUE;
}
