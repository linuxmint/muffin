/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * SECTION:window-props
 * @short_description: #MetaWindow property handling
 *
 * A system which can inspect sets of properties of given windows
 * and take appropriate action given their values.
 *
 * Note that all the meta_window_reload_propert* functions require a
 * round trip to the server.
 *
 * The guts of this system are in meta_display_init_window_prop_hooks().
 * Reading this function will give you insight into how this all fits
 * together.
 */

/*
 * Copyright (C) 2001, 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2004, 2005 Elijah Newren
 * Copyright (C) 2009 Thomas Thurman
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
 */

#define _XOPEN_SOURCE 600 /* for gethostname() */

#include "config.h"

#include "x11/window-props.h"

#include <X11/Xatom.h>
#include <unistd.h>
#include <string.h>

#include "core/frame.h"
#include "core/meta-workspace-manager-private.h"
#include "core/util-private.h"
#include "meta/group.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11-private.h"
#include "x11/window-x11.h"
#include "x11/xprops.h"

#ifndef HOST_NAME_MAX
/* Solaris headers apparently don't define this so do so manually; #326745 */
#define HOST_NAME_MAX 255
#endif

typedef void (* ReloadValueFunc) (MetaWindow    *window,
                                  MetaPropValue *value,
                                  gboolean       initial);

typedef enum
{
  NONE       = 0,
  LOAD_INIT  = (1 << 0),
  INCLUDE_OR = (1 << 1),
  INIT_ONLY  = (1 << 2),
  FORCE_INIT = (1 << 3),
} MetaPropHookFlags;

struct _MetaWindowPropHooks
{
  Atom property;
  MetaPropValueType type;
  ReloadValueFunc reload_func;
  MetaPropHookFlags flags;
};

static void init_prop_value            (MetaWindow          *window,
                                        MetaWindowPropHooks *hooks,
                                        MetaPropValue       *value);
static void reload_prop_value          (MetaWindow          *window,
                                        MetaWindowPropHooks *hooks,
                                        MetaPropValue       *value,
                                        gboolean             initial);
static MetaWindowPropHooks *find_hooks (MetaX11Display *x11_display,
                                        Atom            property);


void
meta_window_reload_property_from_xwindow (MetaWindow      *window,
                                          Window           xwindow,
                                          Atom             property,
                                          gboolean         initial)
{
  MetaPropValue value = { 0, };
  MetaWindowPropHooks *hooks;

  hooks = find_hooks (window->display->x11_display, property);
  if (!hooks)
    return;

  if ((hooks->flags & INIT_ONLY) && !initial)
    return;

  init_prop_value (window, hooks, &value);

  meta_prop_get_values (window->display->x11_display, xwindow,
                        &value, 1);

  reload_prop_value (window, hooks, &value,
                     initial);

  meta_prop_free_values (&value, 1);
}

static void
meta_window_reload_property (MetaWindow      *window,
                             Atom             property,
                             gboolean         initial)
{
  meta_window_reload_property_from_xwindow (window,
                                            window->xwindow,
                                            property,
                                            initial);
}

void
meta_window_load_initial_properties (MetaWindow *window)
{
  int i, j;
  MetaPropValue *values;
  int n_properties = 0;
  MetaX11Display *x11_display = window->display->x11_display;

  values = g_new0 (MetaPropValue, x11_display->n_prop_hooks);

  j = 0;
  for (i = 0; i < x11_display->n_prop_hooks; i++)
    {
      MetaWindowPropHooks *hooks = &x11_display->prop_hooks_table[i];
      if (hooks->flags & LOAD_INIT)
        {
          init_prop_value (window, hooks, &values[j]);
          ++j;
        }
    }
  n_properties = j;

  meta_prop_get_values (window->display->x11_display, window->xwindow,
                        values, n_properties);

  j = 0;
  for (i = 0; i < x11_display->n_prop_hooks; i++)
    {
      MetaWindowPropHooks *hooks = &x11_display->prop_hooks_table[i];
      if (hooks->flags & LOAD_INIT)
        {
          /* If we didn't actually manage to load anything then we don't need
           * to call the reload function; this is different from a notification
           * where disappearance of a previously present value is significant.
           */
          if (values[j].type != META_PROP_VALUE_INVALID ||
              hooks->flags & FORCE_INIT)
            reload_prop_value (window, hooks, &values[j], TRUE);
          ++j;
        }
    }

  meta_prop_free_values (values, n_properties);

  g_free (values);
}

/* Fill in the MetaPropValue used to get the value of "property" */
static void
init_prop_value (MetaWindow          *window,
                 MetaWindowPropHooks *hooks,
                 MetaPropValue       *value)
{
  if (!hooks || hooks->type == META_PROP_VALUE_INVALID ||
      (window->override_redirect && !(hooks->flags & INCLUDE_OR)))
    {
      value->type = META_PROP_VALUE_INVALID;
      value->atom = None;
    }
  else
    {
      value->type = hooks->type;
      value->atom = hooks->property;
    }
}

static void
reload_prop_value (MetaWindow          *window,
                   MetaWindowPropHooks *hooks,
                   MetaPropValue       *value,
                   gboolean             initial)
{
  if (!(window->override_redirect && !(hooks->flags & INCLUDE_OR)))
    (* hooks->reload_func) (window, value, initial);
}

static void
reload_wm_client_machine (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  g_free (window->wm_client_machine);
  window->wm_client_machine = NULL;

  if (value->type != META_PROP_VALUE_INVALID)
    window->wm_client_machine = g_strdup (value->v.str);

  meta_verbose ("Window has client machine \"%s\"\n",
                window->wm_client_machine ? window->wm_client_machine : "unset");

  if (window->wm_client_machine == NULL)
    {
      window->is_remote = FALSE;
    }
  else
    {
      char hostname[HOST_NAME_MAX + 1] = "";

      gethostname (hostname, HOST_NAME_MAX + 1);

      window->is_remote = g_strcmp0 (window->wm_client_machine, hostname) != 0;
    }
}

static void
complain_about_broken_client (MetaWindow    *window,
                              MetaPropValue *value,
                              gboolean       initial)
{
  meta_warning ("Broken client! Window %s changed client leader window or SM client ID\n",
                window->desc);
}

static void
reload_net_wm_window_type (MetaWindow    *window,
                           MetaPropValue *value,
                           gboolean       initial)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      int i;

      for (i = 0; i < value->v.atom_list.n_atoms; i++)
        {
          Atom atom = value->v.atom_list.atoms[i];

          /* We break as soon as we find one we recognize,
           * supposed to prefer those near the front of the list
           */
          if (atom == x11_display->atom__NET_WM_WINDOW_TYPE_DESKTOP ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DOCK ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLBAR ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_MENU ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_UTILITY ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_SPLASH ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DIALOG ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DROPDOWN_MENU ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_POPUP_MENU ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLTIP ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_NOTIFICATION ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_COMBO ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_DND ||
              atom == x11_display->atom__NET_WM_WINDOW_TYPE_NORMAL)
            {
              priv->type_atom = atom;
              break;
            }
        }
    }

  meta_window_x11_recalc_window_type (window);
}

static void
reload_icon (MetaWindow    *window,
             Atom           atom)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;

  meta_icon_cache_property_changed (&priv->icon_cache,
                                    window->display->x11_display,
                                    atom);
  meta_window_queue(window, META_QUEUE_UPDATE_ICON);
}

static void
reload_net_wm_icon (MetaWindow    *window,
                    MetaPropValue *value,
                    gboolean       initial)
{
  reload_icon (window, window->display->x11_display->atom__NET_WM_ICON);
}

static void
reload_kwm_win_icon (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  reload_icon (window, window->display->x11_display->atom__KWM_WIN_ICON);
}

static void
reload_icon_geometry (MetaWindow    *window,
                      MetaPropValue *value,
                      gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      if (value->v.cardinal_list.n_cardinals != 4)
        {
          meta_verbose ("_NET_WM_ICON_GEOMETRY on %s has %d values instead of 4\n",
                        window->desc, value->v.cardinal_list.n_cardinals);
        }
      else
        {
          MetaRectangle geometry;

          geometry.x = (int)value->v.cardinal_list.cardinals[0];
          geometry.y = (int)value->v.cardinal_list.cardinals[1];
          geometry.width = (int)value->v.cardinal_list.cardinals[2];
          geometry.height = (int)value->v.cardinal_list.cardinals[3];

          meta_window_set_icon_geometry (window, &geometry);
        }
    }
  else
    {
      meta_window_set_icon_geometry (window, NULL);
    }
}

static void
meta_window_set_custom_frame_extents (MetaWindow *window,
                                      GtkBorder  *extents,
                                      gboolean    is_initial)
{
  if (extents)
    {
      if (window->has_custom_frame_extents &&
          memcmp (&window->custom_frame_extents, extents, sizeof (GtkBorder)) == 0)
        return;

      window->has_custom_frame_extents = TRUE;
      window->custom_frame_extents = *extents;

      /* If we're setting the frame extents on map, then this is telling
       * us to adjust our understanding of the frame rect to match what
       * GTK+ thinks it is. Future changes to the frame extents should
       * trigger a resize and send a ConfigureRequest to the application.
       */
      if (is_initial)
        {
          meta_window_client_rect_to_frame_rect (window, &window->rect, &window->rect);
          meta_window_client_rect_to_frame_rect (window, &window->unconstrained_rect, &window->unconstrained_rect);
        }
    }
  else
    {
      if (!window->has_custom_frame_extents)
        return;

      window->has_custom_frame_extents = FALSE;
      memset (&window->custom_frame_extents, 0, sizeof (window->custom_frame_extents));
    }

  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}

static void
reload_gtk_frame_extents (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      if (value->v.cardinal_list.n_cardinals != 4)
        {
          meta_verbose ("_GTK_FRAME_EXTENTS on %s has %d values instead of 4\n",
                        window->desc, value->v.cardinal_list.n_cardinals);
        }
      else
        {
          GtkBorder extents;
          extents.left   = (int)value->v.cardinal_list.cardinals[0];
          extents.right  = (int)value->v.cardinal_list.cardinals[1];
          extents.top    = (int)value->v.cardinal_list.cardinals[2];
          extents.bottom = (int)value->v.cardinal_list.cardinals[3];
          meta_window_set_custom_frame_extents (window, &extents, initial);
        }
    }
  else
    {
      meta_window_set_custom_frame_extents (window, NULL, initial);
    }
}

static void
reload_struts (MetaWindow    *window,
               MetaPropValue *value,
               gboolean       initial)
{
  meta_window_update_struts (window);
}

static void
reload_wm_window_role (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  g_clear_pointer (&window->role, g_free);
  if (value->type != META_PROP_VALUE_INVALID)
    window->role = g_strdup (value->v.str);
}

static void
reload_net_wm_pid (MetaWindow    *window,
                   MetaPropValue *value,
                   gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      uint32_t cardinal = (int) value->v.cardinal;

      if (cardinal <= 0)
        meta_warning ("Application set a bogus _NET_WM_PID %u\n",
                      cardinal);
      else
        {
          window->net_wm_pid = cardinal;
          meta_verbose ("Window has _NET_WM_PID %d\n",
                        window->net_wm_pid);
        }
    }
}

static void
reload_net_wm_user_time (MetaWindow    *window,
                         MetaPropValue *value,
                         gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      uint32_t cardinal = value->v.cardinal;
      meta_window_set_user_time (window, cardinal);
    }
}

static void
reload_net_wm_user_time_window (MetaWindow    *window,
                                MetaPropValue *value,
                                gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      MetaWindow *prev_owner;

      /* Unregister old NET_WM_USER_TIME_WINDOW */
      if (window->user_time_window != None)
        {
          /* See the comment to the meta_display_register_x_window call below. */
          meta_x11_display_unregister_x_window (window->display->x11_display,
                                                window->user_time_window);
          /* Don't get events on not-managed windows */
          XSelectInput (window->display->x11_display->xdisplay,
                        window->user_time_window,
                        NoEventMask);
        }

      /* Ensure the new user time window is not used on another MetaWindow,
       * and unset its user time window if that is the case.
       */
      prev_owner = meta_x11_display_lookup_x_window (window->display->x11_display,
                                                     value->v.xwindow);
      if (prev_owner && prev_owner->user_time_window == value->v.xwindow)
        {
          meta_x11_display_unregister_x_window (window->display->x11_display,
                                               value->v.xwindow);
          prev_owner->user_time_window = None;
        }

      /* Obtain the new NET_WM_USER_TIME_WINDOW and register it */
      window->user_time_window = value->v.xwindow;
      if (window->user_time_window != None)
        {
          /* Kind of a hack; display.c:event_callback() ignores events
           * for unknown windows.  We make window->user_time_window
           * known by registering it with window (despite the fact
           * that window->xwindow is already registered with window).
           * This basically means that property notifies to either the
           * window->user_time_window or window->xwindow will be
           * treated identically and will result in functions for
           * window being called to update it.  Maybe we should ignore
           * any property notifies to window->user_time_window other
           * than atom__NET_WM_USER_TIME ones, but I just don't care
           * and it's not specified in the spec anyway.
           */
          meta_x11_display_register_x_window (window->display->x11_display,
                                              &window->user_time_window,
                                              window);
          /* Just listen for property notify events */
          XSelectInput (window->display->x11_display->xdisplay,
                        window->user_time_window,
                        PropertyChangeMask);

          /* Manually load the _NET_WM_USER_TIME field from the given window
           * at this time as well.  If the user_time_window ever broadens in
           * scope, we'll probably want to load all relevant properties here.
           */
          meta_window_reload_property_from_xwindow (
            window,
            window->user_time_window,
            window->display->x11_display->atom__NET_WM_USER_TIME,
            initial);
        }
    }
}

#define MAX_TITLE_LENGTH 512

/**
 * set_title_text:
 *
 * Called by set_window_title() to set the value of @target to @title.
 * If required and @atom is set, it will update the appropriate property.
 *
 * Returns: %TRUE if a new title was set.
 */
static gboolean
set_title_text (MetaWindow  *window,
                gboolean     previous_was_modified,
                const char  *title,
                Atom         atom,
                char       **target)
{
  gboolean modified = FALSE;

  if (!target)
    return FALSE;

  g_free (*target);

  if (!title)
    *target = g_strdup ("");
  else if (g_utf8_strlen (title, MAX_TITLE_LENGTH + 1) > MAX_TITLE_LENGTH)
    {
      *target = meta_g_utf8_strndup (title, MAX_TITLE_LENGTH);
      modified = TRUE;
    }
  /* if WM_CLIENT_MACHINE indicates this machine is on a remote host
   * lets place that hostname in the title */
  else if (meta_window_is_remote (window))
    {
      *target = g_strdup_printf (_("%s (on %s)"),
                      title, window->wm_client_machine);
      modified = TRUE;
    }
  else
    *target = g_strdup (title);

  if (modified && atom != None)
    meta_prop_set_utf8_string_hint (window->display->x11_display,
                                    window->xwindow,
                                    atom, *target);

  /* Bug 330671 -- Don't forget to clear _NET_WM_VISIBLE_(ICON_)NAME */
  if (!modified && previous_was_modified)
    {
      meta_x11_error_trap_push (window->display->x11_display);
      XDeleteProperty (window->display->x11_display->xdisplay,
                       window->xwindow,
                       atom);
      meta_x11_error_trap_pop (window->display->x11_display);
    }

  return modified;
}

static void
set_window_title (MetaWindow *window,
                  const char *title)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;

  char *new_title = NULL;

  gboolean modified =
    set_title_text (window,
                    priv->using_net_wm_visible_name,
                    title,
                    window->display->x11_display->atom__NET_WM_VISIBLE_NAME,
                    &new_title);
  priv->using_net_wm_visible_name = modified;

  meta_window_set_title (window, new_title);

  g_free (new_title);
}

static void
reload_net_wm_name (MetaWindow    *window,
                    MetaPropValue *value,
                    gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      set_window_title (window, value->v.str);
      priv->using_net_wm_name = TRUE;

      meta_verbose ("Using _NET_WM_NAME for new title of %s: \"%s\"\n",
                    window->desc, window->title);
    }
  else
    {
      set_window_title (window, NULL);
      priv->using_net_wm_name = FALSE;
      if (!initial)
        meta_window_reload_property (window, XA_WM_NAME, FALSE);
    }
}

static void
reload_wm_name (MetaWindow    *window,
                MetaPropValue *value,
                gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;

  if (priv->using_net_wm_name)
    {
      meta_verbose ("Ignoring WM_NAME \"%s\" as _NET_WM_NAME is set\n",
                    value->v.str);
      return;
    }

  if (value->type != META_PROP_VALUE_INVALID)
    {
      set_window_title (window, value->v.str);

      meta_verbose ("Using WM_NAME for new title of %s: \"%s\"\n",
                    window->desc, window->title);
    }
  else
    {
      set_window_title (window, NULL);
    }
}

static void
meta_window_set_opaque_region (MetaWindow     *window,
                               cairo_region_t *region)
{
  if (cairo_region_equal (window->opaque_region, region))
    return;

  g_clear_pointer (&window->opaque_region, cairo_region_destroy);

  if (region != NULL)
    window->opaque_region = cairo_region_reference (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}

static void
reload_opaque_region (MetaWindow    *window,
                      MetaPropValue *value,
                      gboolean       initial)
{
  cairo_region_t *opaque_region = NULL;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      uint32_t *region = value->v.cardinal_list.cardinals;
      int nitems = value->v.cardinal_list.n_cardinals;

      cairo_rectangle_int_t *rects;
      int i, rect_index, nrects;

      if (nitems % 4 != 0)
        {
          meta_verbose ("_NET_WM_OPAQUE_REGION does not have a list of 4-tuples.");
          goto out;
        }

      /* empty region */
      if (nitems == 0)
        goto out;

      nrects = nitems / 4;

      rects = g_new (cairo_rectangle_int_t, nrects);

      rect_index = 0;
      i = 0;
      while (i < nitems)
        {
          cairo_rectangle_int_t *rect = &rects[rect_index];

          rect->x = region[i++];
          rect->y = region[i++];
          rect->width = region[i++];
          rect->height = region[i++];

          rect_index++;
        }

      opaque_region = cairo_region_create_rectangles (rects, nrects);

      g_free (rects);
    }

 out:
  meta_window_set_opaque_region (window, opaque_region);
  cairo_region_destroy (opaque_region);
}

static void
reload_mutter_hints (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      char     *new_hints = value->v.str;
      char     *old_hints = window->mutter_hints;
      gboolean  changed   = FALSE;

      if (new_hints)
        {
          if (!old_hints || strcmp (new_hints, old_hints))
            changed = TRUE;
        }
      else
        {
          if (old_hints)
            changed = TRUE;
        }

      if (changed)
        {
          g_free (old_hints);

          if (new_hints)
            window->mutter_hints = g_strdup (new_hints);
          else
            window->mutter_hints = NULL;

          g_object_notify (G_OBJECT (window), "mutter-hints");
        }
    }
  else if (window->mutter_hints)
    {
      g_free (window->mutter_hints);
      window->mutter_hints = NULL;

      g_object_notify (G_OBJECT (window), "mutter-hints");
    }
}

static void
reload_net_wm_state (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;

  int i;

  /* We know this is only an initial window creation,
   * clients don't change the property.
   */

  if (!initial) {
    /* no, they DON'T change the property */
    meta_verbose ("Ignoring _NET_WM_STATE: we should be the one who set "
                  "the property in the first place\n");
    return;
  }

  window->shaded = FALSE;
  window->maximized_horizontally = FALSE;
  window->maximized_vertically = FALSE;
  window->fullscreen = FALSE;
  priv->wm_state_modal = FALSE;
  priv->wm_state_skip_taskbar = FALSE;
  priv->wm_state_skip_pager = FALSE;
  window->wm_state_above = FALSE;
  window->wm_state_below = FALSE;
  window->wm_state_demands_attention = FALSE;

  if (value->type == META_PROP_VALUE_INVALID)
    return;

  i = 0;
  while (i < value->v.atom_list.n_atoms)
    {
      if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_SHADED)
        window->shaded = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ)
        window->maximize_horizontally_after_placement = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT)
        window->maximize_vertically_after_placement = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_HIDDEN)
        window->minimize_after_placement = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_MODAL)
        priv->wm_state_modal = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_SKIP_TASKBAR)
        priv->wm_state_skip_taskbar = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_SKIP_PAGER)
        priv->wm_state_skip_pager = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_FULLSCREEN)
        {
          window->fullscreen = TRUE;
          g_object_notify (G_OBJECT (window), "fullscreen");
        }
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_ABOVE)
        window->wm_state_above = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_BELOW)
        window->wm_state_below = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION)
        window->wm_state_demands_attention = TRUE;
      else if (value->v.atom_list.atoms[i] == x11_display->atom__NET_WM_STATE_STICKY)
        window->on_all_workspaces_requested = TRUE;

      ++i;
    }

  meta_verbose ("Reloaded _NET_WM_STATE for %s\n",
                window->desc);

  meta_window_x11_recalc_window_type (window);
  meta_window_recalc_features (window);
}

static void
reload_mwm_hints (MetaWindow    *window,
                  MetaPropValue *value,
                  gboolean       initial)
{
  MotifWmHints *hints;
  gboolean old_decorated = window->decorated;

  window->mwm_decorated = TRUE;
  window->mwm_border_only = FALSE;
  window->mwm_has_close_func = TRUE;
  window->mwm_has_minimize_func = TRUE;
  window->mwm_has_maximize_func = TRUE;
  window->mwm_has_move_func = TRUE;
  window->mwm_has_resize_func = TRUE;

  if (value->type == META_PROP_VALUE_INVALID)
    {
      meta_verbose ("Window %s has no MWM hints\n", window->desc);
      meta_window_recalc_features (window);
      return;
    }

  hints = value->v.motif_hints;

  /* We support those MWM hints deemed non-stupid */

  meta_verbose ("Window %s has MWM hints\n",
                window->desc);

  if (hints->flags & MWM_HINTS_DECORATIONS)
    {
      meta_verbose ("Window %s sets MWM_HINTS_DECORATIONS 0x%x\n",
          window->desc, hints->decorations);

      if (hints->decorations == 0)
        window->mwm_decorated = FALSE;
      /* some input methods use this */
      else if (hints->decorations == MWM_DECOR_BORDER)
        window->mwm_border_only = TRUE;
    }
  else
    meta_verbose ("Decorations flag unset\n");

  if (hints->flags & MWM_HINTS_FUNCTIONS)
    {
      gboolean toggle_value;

      meta_verbose ("Window %s sets MWM_HINTS_FUNCTIONS 0x%x\n",
                    window->desc, hints->functions);

      /* If _ALL is specified, then other flags indicate what to turn off;
       * if ALL is not specified, flags are what to turn on.
       * at least, I think so
       */

      if ((hints->functions & MWM_FUNC_ALL) == 0)
        {
          toggle_value = TRUE;

          meta_verbose ("Window %s disables all funcs then reenables some\n",
                        window->desc);
          window->mwm_has_close_func = FALSE;
          window->mwm_has_minimize_func = FALSE;
          window->mwm_has_maximize_func = FALSE;
          window->mwm_has_move_func = FALSE;
          window->mwm_has_resize_func = FALSE;
        }
      else
        {
          meta_verbose ("Window %s enables all funcs then disables some\n",
                        window->desc);
          toggle_value = FALSE;
        }

      if ((hints->functions & MWM_FUNC_CLOSE) != 0)
        {
          meta_verbose ("Window %s toggles close via MWM hints\n",
                        window->desc);
          window->mwm_has_close_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_MINIMIZE) != 0)
        {
          meta_verbose ("Window %s toggles minimize via MWM hints\n",
                        window->desc);
          window->mwm_has_minimize_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_MAXIMIZE) != 0)
        {
          meta_verbose ("Window %s toggles maximize via MWM hints\n",
                        window->desc);
          window->mwm_has_maximize_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_MOVE) != 0)
        {
          meta_verbose ("Window %s toggles move via MWM hints\n",
                        window->desc);
          window->mwm_has_move_func = toggle_value;
        }
      if ((hints->functions & MWM_FUNC_RESIZE) != 0)
        {
          meta_verbose ("Window %s toggles resize via MWM hints\n",
                        window->desc);
          window->mwm_has_resize_func = toggle_value;
        }
    }
  else
    meta_verbose ("Functions flag unset\n");

  meta_window_recalc_features (window);

  /* We do all this anyhow at the end of meta_window_x11_new() */
  if (!window->constructing)
    {
      if (window->decorated)
        meta_window_ensure_frame (window);
      else
        meta_window_destroy_frame (window);

      meta_window_queue (window,
                         META_QUEUE_MOVE_RESIZE |
                         /* because ensure/destroy frame may unmap: */
                         META_QUEUE_CALC_SHOWING);

      if (old_decorated != window->decorated)
        g_object_notify (G_OBJECT (window), "decorated");
    }
}

static void
reload_wm_class (MetaWindow    *window,
                 MetaPropValue *value,
                 gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      g_autofree gchar *res_class = g_convert (value->v.class_hint.res_class, -1,
                                               "UTF-8", "LATIN1",
                                               NULL, NULL, NULL);
      g_autofree gchar *res_name = g_convert (value->v.class_hint.res_name, -1,
                                              "UTF-8", "LATIN1",
                                              NULL, NULL, NULL);
      meta_window_set_wm_class (window, res_class, res_name);
    }
  else
    {
      meta_window_set_wm_class (window, NULL, NULL);
    }

  meta_verbose ("Window %s class: '%s' name: '%s'\n",
      window->desc,
      window->res_class ? window->res_class : "none",
      window->res_name ? window->res_name : "none");
}

static void
reload_net_wm_desktop (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      window->initial_workspace_set = TRUE;
      window->initial_workspace = value->v.cardinal;
      meta_topic (META_DEBUG_PLACEMENT,
                  "Read initial workspace prop %d for %s\n",
                  window->initial_workspace, window->desc);
    }
}

static void
reload_net_startup_id (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  guint32 timestamp = window->net_wm_user_time;
  MetaWorkspace *workspace = NULL;

  g_free (window->startup_id);

  if (value->type != META_PROP_VALUE_INVALID)
    window->startup_id = g_strdup (value->v.str);
  else
    window->startup_id = NULL;

  /* Update timestamp and workspace on a running window */
  if (!window->constructing)
  {
    window->initial_timestamp_set = 0;
    window->initial_workspace_set = 0;

    if (meta_display_apply_startup_properties (window->display, window))
      {

        if (window->initial_timestamp_set)
          timestamp = window->initial_timestamp;
        if (window->initial_workspace_set)
          workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                                     window->initial_workspace);

        meta_window_activate_with_workspace (window, timestamp, workspace);
      }
  }

  meta_verbose ("New _NET_STARTUP_ID \"%s\" for %s\n",
                window->startup_id ? window->startup_id : "unset",
                window->desc);
}

static void
reload_update_counter (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      meta_window_x11_destroy_sync_request_alarm (window);
      window->sync_request_counter = None;

      if (value->v.xcounter_list.n_counters == 0)
        {
          meta_warning ("_NET_WM_SYNC_REQUEST_COUNTER is empty\n");
          return;
        }

      if (value->v.xcounter_list.n_counters == 1)
        {
          window->sync_request_counter = value->v.xcounter_list.counters[0];
          window->extended_sync_request_counter = FALSE;
        }
      else
        {
          window->sync_request_counter = value->v.xcounter_list.counters[1];
          window->extended_sync_request_counter = TRUE;
        }
      meta_verbose ("Window has _NET_WM_SYNC_REQUEST_COUNTER 0x%lx (extended=%s)\n",
                    window->sync_request_counter,
                    window->extended_sync_request_counter ? "true" : "false");

      if (window->extended_sync_request_counter)
        meta_window_x11_create_sync_request_alarm (window);
    }
}

#define FLAG_IS_ON(hints,flag) \
  (((hints)->flags & (flag)) != 0)

#define FLAG_IS_OFF(hints,flag) \
  (((hints)->flags & (flag)) == 0)

#define FLAG_TOGGLED_ON(old,new,flag) \
  (FLAG_IS_OFF(old,flag) &&           \
   FLAG_IS_ON(new,flag))

#define FLAG_TOGGLED_OFF(old,new,flag) \
  (FLAG_IS_ON(old,flag) &&             \
   FLAG_IS_OFF(new,flag))

#define FLAG_CHANGED(old,new,flag) \
  (FLAG_TOGGLED_ON(old,new,flag) || FLAG_TOGGLED_OFF(old,new,flag))

static void
spew_size_hints_differences (const XSizeHints *old,
                             const XSizeHints *new)
{
  if (FLAG_CHANGED (old, new, USPosition))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: USPosition now %s\n",
                FLAG_TOGGLED_ON (old, new, USPosition) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, USSize))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: USSize now %s\n",
                FLAG_TOGGLED_ON (old, new, USSize) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, PPosition))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PPosition now %s\n",
                FLAG_TOGGLED_ON (old, new, PPosition) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, PSize))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PSize now %s\n",
                FLAG_TOGGLED_ON (old, new, PSize) ? "set" : "unset");
  if (FLAG_CHANGED (old, new, PMinSize))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PMinSize now %s (%d x %d -> %d x %d)\n",
                FLAG_TOGGLED_ON (old, new, PMinSize) ? "set" : "unset",
                old->min_width, old->min_height,
                new->min_width, new->min_height);
  if (FLAG_CHANGED (old, new, PMaxSize))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PMaxSize now %s (%d x %d -> %d x %d)\n",
                FLAG_TOGGLED_ON (old, new, PMaxSize) ? "set" : "unset",
                old->max_width, old->max_height,
                new->max_width, new->max_height);
  if (FLAG_CHANGED (old, new, PResizeInc))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PResizeInc now %s (width_inc %d -> %d height_inc %d -> %d)\n",
                FLAG_TOGGLED_ON (old, new, PResizeInc) ? "set" : "unset",
                old->width_inc, new->width_inc,
                old->height_inc, new->height_inc);
  if (FLAG_CHANGED (old, new, PAspect))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PAspect now %s (min %d/%d -> %d/%d max %d/%d -> %d/%d)\n",
                FLAG_TOGGLED_ON (old, new, PAspect) ? "set" : "unset",
                old->min_aspect.x, old->min_aspect.y,
                new->min_aspect.x, new->min_aspect.y,
                old->max_aspect.x, old->max_aspect.y,
                new->max_aspect.x, new->max_aspect.y);
  if (FLAG_CHANGED (old, new, PBaseSize))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PBaseSize now %s (%d x %d -> %d x %d)\n",
                FLAG_TOGGLED_ON (old, new, PBaseSize) ? "set" : "unset",
                old->base_width, old->base_height,
                new->base_width, new->base_height);
  if (FLAG_CHANGED (old, new, PWinGravity))
    meta_topic (META_DEBUG_GEOMETRY, "XSizeHints: PWinGravity now %s  (%d -> %d)\n",
                FLAG_TOGGLED_ON (old, new, PWinGravity) ? "set" : "unset",
                old->win_gravity, new->win_gravity);
}

static gboolean
hints_have_changed (const XSizeHints *old,
                    const XSizeHints *new)
{
  /* 1. Check if the relevant values have changed if the flag is set. */

  if (FLAG_TOGGLED_ON (old, new, USPosition) ||
      (FLAG_IS_ON (new, USPosition) &&
       (old->x != new->x ||
        old->y != new->y)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, USSize) ||
      (FLAG_IS_ON (new, USSize) &&
       (old->width != new->width ||
        old->height != new->height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PPosition) ||
      (FLAG_IS_ON (new, PPosition) &&
       (old->x != new->x ||
        old->y != new->y)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PSize) ||
      (FLAG_IS_ON (new, PSize) &&
       (old->width != new->width ||
        old->height != new->height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PMinSize) ||
      (FLAG_IS_ON (new, PMinSize) &&
       (old->min_width != new->min_width ||
        old->min_height != new->min_height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PMaxSize) ||
      (FLAG_IS_ON (new, PMaxSize) &&
       (old->max_width != new->max_width ||
        old->max_height != new->max_height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PResizeInc) ||
      (FLAG_IS_ON (new, PResizeInc) &&
       (old->width_inc != new->width_inc ||
        old->height_inc != new->height_inc)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PAspect) ||
      (FLAG_IS_ON (new, PAspect) &&
       (old->min_aspect.x != new->min_aspect.x ||
        old->min_aspect.y != new->min_aspect.y ||
        old->max_aspect.x != new->max_aspect.x ||
        old->max_aspect.y != new->max_aspect.y)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PBaseSize) ||
      (FLAG_IS_ON (new, PBaseSize) &&
       (old->base_width != new->base_width ||
        old->base_height != new->base_height)))
    return TRUE;

  if (FLAG_TOGGLED_ON (old, new, PWinGravity) ||
      (FLAG_IS_ON (new, PWinGravity) &&
       (old->win_gravity != new->win_gravity)))
    return TRUE;

  /* 2. Check if the flags have been unset. */
  return FLAG_TOGGLED_OFF (old, new, USPosition) ||
         FLAG_TOGGLED_OFF (old, new, USSize) ||
         FLAG_TOGGLED_OFF (old, new, PPosition) ||
         FLAG_TOGGLED_OFF (old, new, PSize) ||
         FLAG_TOGGLED_OFF (old, new, PMinSize) ||
         FLAG_TOGGLED_OFF (old, new, PMaxSize) ||
         FLAG_TOGGLED_OFF (old, new, PResizeInc) ||
         FLAG_TOGGLED_OFF (old, new, PAspect) ||
         FLAG_TOGGLED_OFF (old, new, PBaseSize) ||
         FLAG_TOGGLED_OFF (old, new, PWinGravity);
}

void
meta_set_normal_hints (MetaWindow *window,
                       XSizeHints *hints)
{
  int x, y, w, h;
  double minr, maxr;
  /* Some convenience vars */
  int minw, minh, maxw, maxh;   /* min/max width/height                      */
  int basew, baseh, winc, hinc; /* base width/height, width/height increment */

  /* Save the last ConfigureRequest, which we put here.
   * Values here set in the hints are supposed to
   * be ignored.
   */
  x = window->size_hints.x;
  y = window->size_hints.y;
  w = window->size_hints.width;
  h = window->size_hints.height;

  /* as far as I can tell, value->v.size_hints.flags is just to
   * check whether we had old-style normal hints without gravity,
   * base size as returned by XGetNormalHints(), so we don't
   * really use it as we fixup window->size_hints to have those
   * fields if they're missing.
   */

  /*
   * When the window is first created, NULL hints will
   * be passed in which will initialize all of the fields
   * as if flags were zero
   */
  if (hints)
    window->size_hints = *hints;
  else
    window->size_hints.flags = 0;

  /* Put back saved ConfigureRequest. */
  window->size_hints.x = x;
  window->size_hints.y = y;
  window->size_hints.width = w;
  window->size_hints.height = h;

  /* Get base size hints */
  if (window->size_hints.flags & PBaseSize)
    {
      meta_topic (META_DEBUG_GEOMETRY, "Window %s sets base size %d x %d\n",
                  window->desc,
                  window->size_hints.base_width,
                  window->size_hints.base_height);
    }
  else if (window->size_hints.flags & PMinSize)
    {
      window->size_hints.base_width = window->size_hints.min_width;
      window->size_hints.base_height = window->size_hints.min_height;
    }
  else
    {
      window->size_hints.base_width = 0;
      window->size_hints.base_height = 0;
    }
  window->size_hints.flags |= PBaseSize;

  /* Get min size hints */
  if (window->size_hints.flags & PMinSize)
    {
      meta_topic (META_DEBUG_GEOMETRY, "Window %s sets min size %d x %d\n",
                  window->desc,
                  window->size_hints.min_width,
                  window->size_hints.min_height);
    }
  else if (window->size_hints.flags & PBaseSize)
    {
      window->size_hints.min_width = window->size_hints.base_width;
      window->size_hints.min_height = window->size_hints.base_height;
    }
  else
    {
      window->size_hints.min_width = 0;
      window->size_hints.min_height = 0;
    }
  window->size_hints.flags |= PMinSize;

  /* Get max size hints */
  if (window->size_hints.flags & PMaxSize)
    {
      meta_topic (META_DEBUG_GEOMETRY, "Window %s sets max size %d x %d\n",
                  window->desc,
                  window->size_hints.max_width,
                  window->size_hints.max_height);
    }
  else
    {
      window->size_hints.max_width = G_MAXINT;
      window->size_hints.max_height = G_MAXINT;
      window->size_hints.flags |= PMaxSize;
    }

  /* Get resize increment hints */
  if (window->size_hints.flags & PResizeInc)
    {
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets resize width inc: %d height inc: %d\n",
                  window->desc,
                  window->size_hints.width_inc,
                  window->size_hints.height_inc);
    }
  else
    {
      window->size_hints.width_inc = 1;
      window->size_hints.height_inc = 1;
      window->size_hints.flags |= PResizeInc;
    }

  /* Get aspect ratio hints */
  if (window->size_hints.flags & PAspect)
    {
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets min_aspect: %d/%d max_aspect: %d/%d\n",
                  window->desc,
                  window->size_hints.min_aspect.x,
                  window->size_hints.min_aspect.y,
                  window->size_hints.max_aspect.x,
                  window->size_hints.max_aspect.y);
    }
  else
    {
      window->size_hints.min_aspect.x = 1;
      window->size_hints.min_aspect.y = G_MAXINT;
      window->size_hints.max_aspect.x = G_MAXINT;
      window->size_hints.max_aspect.y = 1;
      window->size_hints.flags |= PAspect;
    }

  /* Get gravity hint */
  if (window->size_hints.flags & PWinGravity)
    {
      meta_topic (META_DEBUG_GEOMETRY, "Window %s sets gravity %d\n",
                  window->desc,
                  window->size_hints.win_gravity);
    }
  else
    {
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s doesn't set gravity, using NW\n",
                  window->desc);
      window->size_hints.win_gravity = META_GRAVITY_NORTH_WEST;
      window->size_hints.flags |= PWinGravity;
    }

  /*** Lots of sanity checking ***/

  /* Verify all min & max hints are at least 1 pixel */
  if (window->size_hints.min_width < 1)
    {
      /* someone is on crack */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets min width to 0, which makes no sense\n",
                  window->desc);
      window->size_hints.min_width = 1;
    }
  if (window->size_hints.max_width < 1)
    {
      /* another cracksmoker */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets max width to 0, which makes no sense\n",
                  window->desc);
      window->size_hints.max_width = 1;
    }
  if (window->size_hints.min_height < 1)
    {
      /* another cracksmoker */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets min height to 0, which makes no sense\n",
                  window->desc);
      window->size_hints.min_height = 1;
    }
  if (window->size_hints.max_height < 1)
    {
      /* another cracksmoker */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets max height to 0, which makes no sense\n",
                  window->desc);
      window->size_hints.max_height = 1;
    }

  /* Verify size increment hints are at least 1 pixel */
  if (window->size_hints.width_inc < 1)
    {
      /* app authors find so many ways to smoke crack */
      window->size_hints.width_inc = 1;
      meta_topic (META_DEBUG_GEOMETRY, "Corrected 0 width_inc to 1\n");
    }
  if (window->size_hints.height_inc < 1)
    {
      /* another cracksmoker */
      window->size_hints.height_inc = 1;
      meta_topic (META_DEBUG_GEOMETRY, "Corrected 0 height_inc to 1\n");
    }
  /* divide by 0 cracksmokers; note that x & y in (min|max)_aspect are
   * numerator & denominator
   */
  if (window->size_hints.min_aspect.y < 1)
    window->size_hints.min_aspect.y = 1;
  if (window->size_hints.max_aspect.y < 1)
    window->size_hints.max_aspect.y = 1;

  minw  = window->size_hints.min_width;  minh  = window->size_hints.min_height;
  maxw  = window->size_hints.max_width;  maxh  = window->size_hints.max_height;
  basew = window->size_hints.base_width; baseh = window->size_hints.base_height;
  winc  = window->size_hints.width_inc;  hinc  = window->size_hints.height_inc;

  /* Make sure min and max size hints are consistent with the base + increment
   * size hints.  If they're not, it's not a real big deal, but it means the
   * effective min and max size are more restrictive than the application
   * specified values.
   */
  if ((minw - basew) % winc != 0)
    {
      /* Take advantage of integer division throwing away the remainder... */
      window->size_hints.min_width = basew + ((minw - basew)/winc + 1)*winc;

      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s has width_inc (%d) that does not evenly divide "
                  "min_width - base_width (%d - %d); thus effective "
                  "min_width is really %d\n",
                  window->desc,
                  winc, minw, basew, window->size_hints.min_width);
      minw = window->size_hints.min_width;
    }
  if (maxw != G_MAXINT && (maxw - basew) % winc != 0)
    {
      /* Take advantage of integer division throwing away the remainder... */
      window->size_hints.max_width = basew + ((maxw - basew)/winc)*winc;

      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s has width_inc (%d) that does not evenly divide "
                  "max_width - base_width (%d - %d); thus effective "
                  "max_width is really %d\n",
                  window->desc,
                  winc, maxw, basew, window->size_hints.max_width);
      maxw = window->size_hints.max_width;
    }
  if ((minh - baseh) % hinc != 0)
    {
      /* Take advantage of integer division throwing away the remainder... */
      window->size_hints.min_height = baseh + ((minh - baseh)/hinc + 1)*hinc;

      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s has height_inc (%d) that does not evenly divide "
                  "min_height - base_height (%d - %d); thus effective "
                  "min_height is really %d\n",
                  window->desc,
                  hinc, minh, baseh, window->size_hints.min_height);
      minh = window->size_hints.min_height;
    }
  if (maxh != G_MAXINT && (maxh - baseh) % hinc != 0)
    {
      /* Take advantage of integer division throwing away the remainder... */
      window->size_hints.max_height = baseh + ((maxh - baseh)/hinc)*hinc;

      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s has height_inc (%d) that does not evenly divide "
                  "max_height - base_height (%d - %d); thus effective "
                  "max_height is really %d\n",
                  window->desc,
                  hinc, maxh, baseh, window->size_hints.max_height);
      maxh = window->size_hints.max_height;
    }

  /* make sure maximum size hints are compatible with minimum size hints; min
   * size hints take precedence.
   */
  if (window->size_hints.max_width < window->size_hints.min_width)
    {
      /* another cracksmoker */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets max width %d less than min width %d, "
                  "disabling resize\n",
                  window->desc,
                  window->size_hints.max_width,
                  window->size_hints.min_width);
      maxw = window->size_hints.max_width = window->size_hints.min_width;
    }
  if (window->size_hints.max_height < window->size_hints.min_height)
    {
      /* another cracksmoker */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets max height %d less than min height %d, "
                  "disabling resize\n",
                  window->desc,
                  window->size_hints.max_height,
                  window->size_hints.min_height);
      maxh = window->size_hints.max_height = window->size_hints.min_height;
    }

  /* Make sure the aspect ratio hints are sane. */
  minr =         window->size_hints.min_aspect.x /
         (double)window->size_hints.min_aspect.y;
  maxr =         window->size_hints.max_aspect.x /
         (double)window->size_hints.max_aspect.y;
  if (minr > maxr)
    {
      /* another cracksmoker; not even minimally (self) consistent */
      meta_topic (META_DEBUG_GEOMETRY,
                  "Window %s sets min aspect ratio larger than max aspect "
                  "ratio; disabling aspect ratio constraints.\n",
                  window->desc);
      window->size_hints.min_aspect.x = 1;
      window->size_hints.min_aspect.y = G_MAXINT;
      window->size_hints.max_aspect.x = G_MAXINT;
      window->size_hints.max_aspect.y = 1;
    }
  else /* check consistency of aspect ratio hints with other hints */
    {
      if (minh > 0 && minr > (maxw / (double)minh))
        {
          /* another cracksmoker */
          meta_topic (META_DEBUG_GEOMETRY,
                      "Window %s sets min aspect ratio larger than largest "
                      "aspect ratio possible given min/max size constraints; "
                      "disabling min aspect ratio constraint.\n",
                      window->desc);
          window->size_hints.min_aspect.x = 1;
          window->size_hints.min_aspect.y = G_MAXINT;
        }
      if (maxr < (minw / (double)maxh))
        {
          /* another cracksmoker */
          meta_topic (META_DEBUG_GEOMETRY,
                      "Window %s sets max aspect ratio smaller than smallest "
                      "aspect ratio possible given min/max size constraints; "
                      "disabling max aspect ratio constraint.\n",
                      window->desc);
          window->size_hints.max_aspect.x = G_MAXINT;
          window->size_hints.max_aspect.y = 1;
        }
      /* FIXME: Would be nice to check that aspect ratios are
       * consistent with base and size increment constraints.
       */
    }
}

static void
reload_normal_hints (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  if (value->type != META_PROP_VALUE_INVALID)
    {
      XSizeHints old_hints;
      gboolean hints_have_differences;

      meta_topic (META_DEBUG_GEOMETRY, "Updating WM_NORMAL_HINTS for %s\n", window->desc);

      old_hints = window->size_hints;

      meta_set_normal_hints (window, value->v.size_hints.hints);

      hints_have_differences = hints_have_changed (&old_hints,
                                                   &window->size_hints);
      if (hints_have_differences)
        {
          spew_size_hints_differences (&old_hints, &window->size_hints);
          meta_window_recalc_features (window);

          if (!initial)
            meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
        }
    }
}

static void
reload_wm_protocols (MetaWindow    *window,
                     MetaPropValue *value,
                     gboolean       initial)
{
  int i;

  meta_window_x11_set_wm_take_focus (window, FALSE);
  meta_window_x11_set_wm_ping (window, FALSE);
  meta_window_x11_set_wm_delete_window (window, FALSE);

  if (value->type == META_PROP_VALUE_INVALID)
    return;

  i = 0;
  while (i < value->v.atom_list.n_atoms)
    {
      if (value->v.atom_list.atoms[i] ==
          window->display->x11_display->atom_WM_TAKE_FOCUS)
        meta_window_x11_set_wm_take_focus (window, TRUE);
      else if (value->v.atom_list.atoms[i] ==
               window->display->x11_display->atom_WM_DELETE_WINDOW)
        meta_window_x11_set_wm_delete_window (window, TRUE);
      else if (value->v.atom_list.atoms[i] ==
               window->display->x11_display->atom__NET_WM_PING)
        meta_window_x11_set_wm_ping (window, TRUE);
      ++i;
    }

  meta_verbose ("New _NET_STARTUP_ID \"%s\" for %s\n",
                window->startup_id ? window->startup_id : "unset",
                window->desc);
}

static void
reload_wm_hints (MetaWindow    *window,
                 MetaPropValue *value,
                 gboolean       initial)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = window_x11->priv;
  Window old_group_leader;
  gboolean urgent;

  old_group_leader = window->xgroup_leader;

  /* Fill in defaults */
  window->input = TRUE;
  window->initially_iconic = FALSE;
  window->xgroup_leader = None;
  priv->wm_hints_pixmap = None;
  priv->wm_hints_mask = None;
  urgent = FALSE;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      const XWMHints *hints = value->v.wm_hints;

      if (hints->flags & InputHint)
        window->input = hints->input;

      if (hints->flags & StateHint)
        window->initially_iconic = (hints->initial_state == IconicState);

      if (hints->flags & WindowGroupHint)
        window->xgroup_leader = hints->window_group;

      if (hints->flags & IconPixmapHint)
        priv->wm_hints_pixmap = hints->icon_pixmap;

      if (hints->flags & IconMaskHint)
        priv->wm_hints_mask = hints->icon_mask;

      if (hints->flags & XUrgencyHint)
        urgent = TRUE;

      meta_verbose ("Read WM_HINTS input: %d iconic: %d group leader: 0x%lx pixmap: 0x%lx mask: 0x%lx\n",
                    window->input, window->initially_iconic,
                    window->xgroup_leader,
                    priv->wm_hints_pixmap,
                    priv->wm_hints_mask);
    }

  if (window->xgroup_leader != old_group_leader)
    {
      meta_verbose ("Window %s changed its group leader to 0x%lx\n",
                    window->desc, window->xgroup_leader);

      meta_window_group_leader_changed (window);
    }

  meta_window_set_urgent (window, urgent);

  meta_icon_cache_property_changed (&priv->icon_cache,
                                    window->display->x11_display,
                                    XA_WM_HINTS);

  meta_window_queue (window, META_QUEUE_UPDATE_ICON | META_QUEUE_MOVE_RESIZE);
}

static gboolean
check_xtransient_for_loop (MetaWindow *window,
                           MetaWindow *parent)
{
  while (parent)
    {
      if (parent == window)
        return TRUE;

      parent = meta_x11_display_lookup_x_window (parent->display->x11_display,
                                                 parent->xtransient_for);
    }

  return FALSE;
}

static void
reload_transient_for (MetaWindow    *window,
                      MetaPropValue *value,
                      gboolean       initial)
{
  MetaWindow *parent = NULL;
  Window transient_for;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      transient_for = value->v.xwindow;

      parent = meta_x11_display_lookup_x_window (window->display->x11_display,
                                                 transient_for);
      if (!parent)
        {
          meta_warning ("Invalid WM_TRANSIENT_FOR window 0x%lx specified for %s.\n",
                        transient_for, window->desc);
          transient_for = None;
        }
      else if (parent->override_redirect)
        {
          const gchar *window_kind = window->override_redirect ?
            "override-redirect" : "top-level";

          if (parent->xtransient_for != None)
            {
              /* We don't have to go through the parents, as per this code it is
               * not possible that a window has the WM_TRANSIENT_FOR set to an
               * override-redirect window anyways */
              meta_warning ("WM_TRANSIENT_FOR window %s for %s window %s is an "
                            "override-redirect window and this is not correct "
                            "according to the standard, so we'll fallback to "
                            "the first non-override-redirect window 0x%lx.\n",
                            parent->desc, window->desc, window_kind,
                            parent->xtransient_for);
              transient_for = parent->xtransient_for;
              parent =
                meta_x11_display_lookup_x_window (parent->display->x11_display,
                                                  transient_for);
            }
          else
            {
              meta_warning ("WM_TRANSIENT_FOR window %s for %s window %s is an "
                            "override-redirect window and this is not correct "
                            "according to the standard, so we'll fallback to "
                            "the root window.\n", parent->desc, window_kind,
                            window->desc);
              transient_for = parent->display->x11_display->xroot;
              parent = NULL;
            }
        }

      /* Make sure there is not a loop */
      if (check_xtransient_for_loop (window, parent))
        {
          meta_warning ("WM_TRANSIENT_FOR window 0x%lx for %s would create a "
                        "loop.\n", transient_for, window->desc);
          transient_for = None;
        }
    }
  else
    transient_for = None;

  if (transient_for == window->xtransient_for)
    return;

  window->xtransient_for = transient_for;

  if (window->xtransient_for != None)
    meta_verbose ("Window %s transient for 0x%lx\n", window->desc, window->xtransient_for);
  else
    meta_verbose ("Window %s is not transient\n", window->desc);

  if (window->xtransient_for == None ||
      window->xtransient_for == window->display->x11_display->xroot)
    meta_window_set_transient_for (window, NULL);
  else
    {
      meta_window_set_transient_for (window, parent);
    }
}

static void
reload_gtk_theme_variant (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  char     *requested_variant = NULL;
  char     *current_variant   = window->gtk_theme_variant;

  if (value->type != META_PROP_VALUE_INVALID)
    {
      requested_variant = value->v.str;
      meta_verbose ("Requested \"%s\" theme variant for window %s.\n",
                    requested_variant, window->desc);
    }

  if (g_strcmp0 (requested_variant, current_variant) != 0)
    {
      g_free (current_variant);

      window->gtk_theme_variant = g_strdup (requested_variant);

      if (window->frame)
        meta_frame_update_style (window->frame);
    }
}

static void
reload_bypass_compositor (MetaWindow    *window,
                          MetaPropValue *value,
                          gboolean       initial)
{
  int requested_value = 0;
  int current_value = window->bypass_compositor;

  if (value->type != META_PROP_VALUE_INVALID)
      requested_value = (int) value->v.cardinal;

  if (requested_value == current_value)
    return;

  if (requested_value == _NET_WM_BYPASS_COMPOSITOR_HINT_ON)
    meta_verbose ("Request to bypass compositor for window %s.\n", window->desc);
  else if (requested_value == _NET_WM_BYPASS_COMPOSITOR_HINT_OFF)
    meta_verbose ("Request to don't bypass compositor for window %s.\n", window->desc);
  else if (requested_value != _NET_WM_BYPASS_COMPOSITOR_HINT_AUTO)
    return;

  window->bypass_compositor = requested_value;
}

static void
reload_window_opacity (MetaWindow    *window,
                       MetaPropValue *value,
                       gboolean       initial)

{
  guint8 opacity = 0xFF;

  if (value->type != META_PROP_VALUE_INVALID)
    opacity = (guint8)((gfloat)value->v.cardinal * 255.0 / ((gfloat)0xffffffff));

  meta_window_set_opacity (window, opacity);
}

#define RELOAD_STRING(var_name, propname) \
  static void                                       \
  reload_ ## var_name (MetaWindow    *window,       \
                       MetaPropValue *value,        \
                       gboolean       initial)      \
  {                                                 \
    g_free (window->var_name);                      \
                                                    \
    if (value->type != META_PROP_VALUE_INVALID)     \
      window->var_name = g_strdup (value->v.str);   \
    else                                            \
      window->var_name = NULL;                      \
                                                    \
    g_object_notify (G_OBJECT (window), propname);  \
  }

RELOAD_STRING (gtk_unique_bus_name,         "gtk-unique-bus-name")
RELOAD_STRING (gtk_application_id,          "gtk-application-id")
RELOAD_STRING (gtk_application_object_path, "gtk-application-object-path")
RELOAD_STRING (gtk_window_object_path,      "gtk-window-object-path")
RELOAD_STRING (gtk_app_menu_object_path,    "gtk-app-menu-object-path")
RELOAD_STRING (gtk_menubar_object_path,     "gtk-menubar-object-path")

#undef RELOAD_STRING

/**
 * meta_x11_display_init_window_prop_hooks:
 * @x11_display: The #MetaDX11isplay
 *
 * Initialises the property hooks system.  Each row in the table named "hooks"
 * represents an action to take when a property is found on a newly-created
 * window, or when a property changes its value.
 *
 * The first column shows which atom the row concerns.
 * The second gives the type of the property data.  The property will be
 * queried for its new value, unless the type is given as
 * META_PROP_VALUE_INVALID, in which case nothing will be queried.
 * The third column gives the name of a callback which gets called with the
 * new value.  (If the new value was not retrieved because the second column
 * was META_PROP_VALUE_INVALID, the callback still gets called anyway.)
 * This value may be NULL, in which case no callback will be called.
 */
void
meta_x11_display_init_window_prop_hooks (MetaX11Display *x11_display)
{
  /* The ordering here is significant for the properties we load
   * initially: they are roughly ordered in the order we want them to
   * be gotten. We want to get window name and class first so we can
   * use them in error messages and such. However, name is modified
   * depending on wm_client_machine, so push it slightly sooner.
   *
   * For override-redirect windows, we pay attention to:
   *
   *  - properties that identify the window: useful for debugging
   *    purposes.
   *  - NET_WM_WINDOW_TYPE: can be used to do appropriate handling
   *    for different types of override-redirect windows.
   */
  MetaWindowPropHooks hooks[] = {
    { x11_display->atom_WM_CLIENT_MACHINE, META_PROP_VALUE_STRING,   reload_wm_client_machine, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_NAME,      META_PROP_VALUE_UTF8,     reload_net_wm_name,       LOAD_INIT | INCLUDE_OR },
    { XA_WM_CLASS,                         META_PROP_VALUE_CLASS_HINT, reload_wm_class,        LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_PID,       META_PROP_VALUE_CARDINAL, reload_net_wm_pid,        LOAD_INIT | INCLUDE_OR },
    { XA_WM_NAME,                          META_PROP_VALUE_TEXT_PROPERTY, reload_wm_name,      LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__MUTTER_HINTS,     META_PROP_VALUE_TEXT_PROPERTY, reload_mutter_hints, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_OPAQUE_REGION, META_PROP_VALUE_CARDINAL_LIST, reload_opaque_region, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_DESKTOP,   META_PROP_VALUE_CARDINAL, reload_net_wm_desktop,    LOAD_INIT | INIT_ONLY },
    { x11_display->atom__NET_STARTUP_ID,   META_PROP_VALUE_UTF8,     reload_net_startup_id,    LOAD_INIT },
    { x11_display->atom__NET_WM_SYNC_REQUEST_COUNTER, META_PROP_VALUE_SYNC_COUNTER_LIST, reload_update_counter, LOAD_INIT | INCLUDE_OR },
    { XA_WM_NORMAL_HINTS,                  META_PROP_VALUE_SIZE_HINTS, reload_normal_hints,    LOAD_INIT },
    { x11_display->atom_WM_PROTOCOLS,      META_PROP_VALUE_ATOM_LIST, reload_wm_protocols,     LOAD_INIT },
    { XA_WM_HINTS,                         META_PROP_VALUE_WM_HINTS,  reload_wm_hints,         LOAD_INIT },
    { x11_display->atom__NET_WM_USER_TIME, META_PROP_VALUE_CARDINAL, reload_net_wm_user_time,  LOAD_INIT },
    { x11_display->atom__NET_WM_STATE,     META_PROP_VALUE_ATOM_LIST, reload_net_wm_state,     LOAD_INIT | INIT_ONLY },
    { x11_display->atom__MOTIF_WM_HINTS,   META_PROP_VALUE_MOTIF_HINTS, reload_mwm_hints,      LOAD_INIT },
    { XA_WM_TRANSIENT_FOR,                 META_PROP_VALUE_WINDOW,    reload_transient_for,    LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__GTK_THEME_VARIANT, META_PROP_VALUE_UTF8,     reload_gtk_theme_variant, LOAD_INIT },
    { x11_display->atom__GTK_APPLICATION_ID,               META_PROP_VALUE_UTF8,         reload_gtk_application_id,               LOAD_INIT },
    { x11_display->atom__GTK_UNIQUE_BUS_NAME,              META_PROP_VALUE_UTF8,         reload_gtk_unique_bus_name,              LOAD_INIT },
    { x11_display->atom__GTK_APPLICATION_OBJECT_PATH,      META_PROP_VALUE_UTF8,         reload_gtk_application_object_path,      LOAD_INIT },
    { x11_display->atom__GTK_WINDOW_OBJECT_PATH,           META_PROP_VALUE_UTF8,         reload_gtk_window_object_path,           LOAD_INIT },
    { x11_display->atom__GTK_APP_MENU_OBJECT_PATH,         META_PROP_VALUE_UTF8,         reload_gtk_app_menu_object_path,         LOAD_INIT },
    { x11_display->atom__GTK_MENUBAR_OBJECT_PATH,          META_PROP_VALUE_UTF8,         reload_gtk_menubar_object_path,          LOAD_INIT },
    { x11_display->atom__GTK_FRAME_EXTENTS,                META_PROP_VALUE_CARDINAL_LIST,reload_gtk_frame_extents,                LOAD_INIT },
    { x11_display->atom__NET_WM_USER_TIME_WINDOW, META_PROP_VALUE_WINDOW, reload_net_wm_user_time_window, LOAD_INIT },
    { x11_display->atom__NET_WM_ICON,      META_PROP_VALUE_INVALID,  reload_net_wm_icon,  NONE },
    { x11_display->atom__KWM_WIN_ICON,     META_PROP_VALUE_INVALID,  reload_kwm_win_icon, NONE },
    { x11_display->atom__NET_WM_ICON_GEOMETRY, META_PROP_VALUE_CARDINAL_LIST, reload_icon_geometry, LOAD_INIT },
    { x11_display->atom_WM_CLIENT_LEADER,  META_PROP_VALUE_INVALID, complain_about_broken_client, NONE },
    { x11_display->atom_SM_CLIENT_ID,      META_PROP_VALUE_INVALID, complain_about_broken_client, NONE },
    { x11_display->atom_WM_WINDOW_ROLE,    META_PROP_VALUE_STRING, reload_wm_window_role, LOAD_INIT | FORCE_INIT },
    { x11_display->atom__NET_WM_WINDOW_TYPE, META_PROP_VALUE_ATOM_LIST, reload_net_wm_window_type, LOAD_INIT | INCLUDE_OR | FORCE_INIT },
    { x11_display->atom__NET_WM_STRUT,         META_PROP_VALUE_INVALID, reload_struts, NONE },
    { x11_display->atom__NET_WM_STRUT_PARTIAL, META_PROP_VALUE_INVALID, reload_struts, NONE },
    { x11_display->atom__NET_WM_BYPASS_COMPOSITOR, META_PROP_VALUE_CARDINAL,  reload_bypass_compositor, LOAD_INIT | INCLUDE_OR },
    { x11_display->atom__NET_WM_WINDOW_OPACITY, META_PROP_VALUE_CARDINAL, reload_window_opacity, LOAD_INIT | INCLUDE_OR },
    { 0 },
  };

  MetaWindowPropHooks *table = g_memdup (hooks, sizeof (hooks)),
    *cursor = table;

  g_assert (x11_display->prop_hooks == NULL);

  x11_display->prop_hooks_table = (gpointer) table;
  x11_display->prop_hooks = g_hash_table_new (NULL, NULL);

  while (cursor->property)
    {
      /* Doing initial loading doesn't make sense if we just want notification */
      g_assert (!((cursor->flags & LOAD_INIT) && cursor->type == META_PROP_VALUE_INVALID));

      /* Forcing initialization doesn't make sense if not loading initially */
      g_assert ((cursor->flags & LOAD_INIT) || !(cursor->flags & FORCE_INIT));

      /* Atoms are safe to use with GINT_TO_POINTER because it's safe with
       * anything 32 bits or less, and atoms are 32 bits with the top three
       * bits clear.  (Scheifler & Gettys, 2e, p372)
       */
      g_hash_table_insert (x11_display->prop_hooks,
                           GINT_TO_POINTER (cursor->property),
                           cursor);
      cursor++;
    }
  x11_display->n_prop_hooks = cursor - table;
}

void
meta_x11_display_free_window_prop_hooks (MetaX11Display *x11_display)
{
  g_hash_table_unref (x11_display->prop_hooks);
  x11_display->prop_hooks = NULL;

  g_free (x11_display->prop_hooks_table);
  x11_display->prop_hooks_table = NULL;
}

static MetaWindowPropHooks *
find_hooks (MetaX11Display *x11_display,
            Atom            property)
{
  return g_hash_table_lookup (x11_display->prop_hooks,
                              GINT_TO_POINTER (property));
}
