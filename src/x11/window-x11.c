/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington, Anders Carlsson
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#include "config.h"

#include "x11/window-x11.h"
#include "x11/window-x11-private.h"

#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlibint.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <xcb/res.h>

#include "backends/meta-logical-monitor.h"
#include "backends/x11/meta-backend-x11.h"
#include "core/boxes-private.h"
#include "core/frame.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "core/workspace-private.h"
#include "meta/common.h"
#include "meta/meta-cursor-tracker.h"
#include "meta/meta-x11-errors.h"
#include "meta/prefs.h"
#include "x11/meta-x11-display-private.h"
#include "x11/session.h"
#include "x11/window-props.h"
#include "x11/xprops.h"

#define TAKE_FOCUS_FALLBACK_DELAY_MS 150

enum _MetaGtkEdgeConstraints
{
  META_GTK_EDGE_CONSTRAINT_TOP_TILED = 1 << 0,
  META_GTK_EDGE_CONSTRAINT_TOP_RESIZABLE = 1 << 1,
  META_GTK_EDGE_CONSTRAINT_RIGHT_TILED = 1 << 2,
  META_GTK_EDGE_CONSTRAINT_RIGHT_RESIZABLE = 1 << 3,
  META_GTK_EDGE_CONSTRAINT_BOTTOM_TILED = 1 << 4,
  META_GTK_EDGE_CONSTRAINT_BOTTOM_RESIZABLE = 1 << 5,
  META_GTK_EDGE_CONSTRAINT_LEFT_TILED = 1 << 6,
  META_GTK_EDGE_CONSTRAINT_LEFT_RESIZABLE = 1 << 7
} MetaGtkEdgeConstraints;

G_DEFINE_TYPE_WITH_PRIVATE (MetaWindowX11, meta_window_x11, META_TYPE_WINDOW)

static void
meta_window_x11_maybe_focus_delayed (MetaWindow *window,
                                     GQueue     *other_focus_candidates,
                                     guint32     timestamp);

static void
meta_window_x11_init (MetaWindowX11 *window_x11)
{
  window_x11->priv = meta_window_x11_get_instance_private (window_x11);
}

static void
send_icccm_message (MetaWindow *window,
                    Atom        atom,
                    guint32     timestamp)
{
  /* This comment and code are from twm, copyright
   * Open Group, Evans & Sutherland, etc.
   */

  /*
   * ICCCM Client Messages - Section 4.2.8 of the ICCCM dictates that all
   * client messages will have the following form:
   *
   *     event type	ClientMessage
   *     message type	_XA_WM_PROTOCOLS
   *     window		tmp->w
   *     format		32
   *     data[0]		message atom
   *     data[1]		time stamp
   */

  XClientMessageEvent ev;
  MetaX11Display *x11_display = window->display->x11_display;

  ev.type = ClientMessage;
  ev.window = window->xwindow;
  ev.message_type = x11_display->atom_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = atom;
  ev.data.l[1] = timestamp;

  meta_x11_error_trap_push (x11_display);
  XSendEvent (x11_display->xdisplay,
              window->xwindow, False, 0, (XEvent*) &ev);
  meta_x11_error_trap_pop (x11_display);
}

static Window
read_client_leader (MetaDisplay *display,
                    Window       xwindow)
{
  Window retval = None;

  meta_prop_get_window (display->x11_display, xwindow,
                        display->x11_display->atom_WM_CLIENT_LEADER,
                        &retval);

  return retval;
}

typedef struct
{
  Window leader;
} ClientLeaderData;

static gboolean
find_client_leader_func (MetaWindow *ancestor,
                         void       *data)
{
  ClientLeaderData *d;

  d = data;

  d->leader = read_client_leader (ancestor->display,
                                  ancestor->xwindow);

  /* keep going if no client leader found */
  return d->leader == None;
}

static void
update_sm_hints (MetaWindow *window)
{
  Window leader;

  window->xclient_leader = None;
  window->sm_client_id = NULL;

  /* If not on the current window, we can get the client
   * leader from transient parents. If we find a client
   * leader, we read the SM_CLIENT_ID from it.
   */
  leader = read_client_leader (window->display, window->xwindow);
  if (leader == None)
    {
      ClientLeaderData d;
      d.leader = None;
      meta_window_foreach_ancestor (window, find_client_leader_func,
                                    &d);
      leader = d.leader;
    }

  if (leader != None)
    {
      window->xclient_leader = leader;

      meta_prop_get_latin1_string (window->display->x11_display, leader,
                                   window->display->x11_display->atom_SM_CLIENT_ID,
                                   &window->sm_client_id);
    }
  else
    {
      meta_verbose ("Didn't find a client leader for %s\n", window->desc);

      if (!meta_prefs_get_disable_workarounds ())
        {
          /* Some broken apps (kdelibs fault?) set SM_CLIENT_ID on the app
           * instead of the client leader
           */
          meta_prop_get_latin1_string (window->display->x11_display, window->xwindow,
                                       window->display->x11_display->atom_SM_CLIENT_ID,
                                       &window->sm_client_id);

          if (window->sm_client_id)
            meta_warning ("Window %s sets SM_CLIENT_ID on itself, instead of on the WM_CLIENT_LEADER window as specified in the ICCCM.\n",
                          window->desc);
        }
    }

  meta_verbose ("Window %s client leader: 0x%lx SM_CLIENT_ID: '%s'\n",
                window->desc, window->xclient_leader,
                window->sm_client_id ? window->sm_client_id : "none");
}

static void
send_configure_notify (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  XEvent event;

  g_assert (!window->override_redirect);

  /* from twm */

  event.type = ConfigureNotify;
  event.xconfigure.display = x11_display->xdisplay;
  event.xconfigure.event = window->xwindow;
  event.xconfigure.window = window->xwindow;
  event.xconfigure.x = priv->client_rect.x - priv->border_width;
  event.xconfigure.y = priv->client_rect.y - priv->border_width;
  if (window->frame)
    {
      if (window->withdrawn)
        {
          MetaFrameBorders borders;
          /* We reparent the client window and put it to the position
           * where the visible top-left of the frame window currently is.
           */

          meta_frame_calc_borders (window->frame, &borders);

          event.xconfigure.x = window->frame->rect.x + borders.invisible.left;
          event.xconfigure.y = window->frame->rect.y + borders.invisible.top;
        }
      else
        {
          /* Need to be in root window coordinates */
          event.xconfigure.x += window->frame->rect.x;
          event.xconfigure.y += window->frame->rect.y;
        }
    }
  event.xconfigure.width = priv->client_rect.width;
  event.xconfigure.height = priv->client_rect.height;
  event.xconfigure.border_width = priv->border_width; /* requested not actual */
  event.xconfigure.above = None; /* FIXME */
  event.xconfigure.override_redirect = False;

  meta_topic (META_DEBUG_GEOMETRY,
              "Sending synthetic configure notify to %s with x: %d y: %d w: %d h: %d\n",
              window->desc,
              event.xconfigure.x, event.xconfigure.y,
              event.xconfigure.width, event.xconfigure.height);

  meta_x11_error_trap_push (x11_display);
  XSendEvent (x11_display->xdisplay,
              window->xwindow,
              False, StructureNotifyMask, &event);
  meta_x11_error_trap_pop (x11_display);
}

static void
adjust_for_gravity (MetaWindow        *window,
                    gboolean           coords_assume_border,
                    MetaGravity        gravity,
                    MetaRectangle     *rect)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  int ref_x, ref_y;
  int bw;
  int child_x, child_y;
  int frame_width, frame_height;
  MetaFrameBorders borders;

  /* We're computing position to pass to window_move, which is
   * the position of the client window (META_GRAVITY_STATIC basically)
   *
   * (see WM spec description of gravity computation, but note that
   * their formulas assume we're honoring the border width, rather
   * than compensating for having turned it off)
   */

  if (gravity == META_GRAVITY_STATIC)
    return;

  if (coords_assume_border)
    bw = priv->border_width;
  else
    bw = 0;

  meta_frame_calc_borders (window->frame, &borders);

  child_x = borders.visible.left;
  child_y = borders.visible.top;
  frame_width = child_x + rect->width + borders.visible.right;
  frame_height = child_y + rect->height + borders.visible.bottom;

  /* Calculate the the reference point, which is the corner of the
   * outer window specified by the gravity. So, META_GRAVITY_NORTH_EAST
   * would have the reference point as the top-right corner of the
   * outer window. */
  ref_x = rect->x;
  ref_y = rect->y;

  switch (gravity)
    {
    case META_GRAVITY_NORTH:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_SOUTH:
      ref_x += rect->width / 2 + bw;
      break;
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_SOUTH_EAST:
      ref_x += rect->width + bw * 2;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_EAST:
      ref_y += rect->height / 2 + bw;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      ref_y += rect->height + bw * 2;
      break;
    default:
      break;
    }

  /* Find the top-left corner of the outer window from
   * the reference point. */

  rect->x = ref_x;
  rect->y = ref_y;

  switch (gravity)
    {
    case META_GRAVITY_NORTH:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_SOUTH:
      rect->x -= frame_width / 2;
      break;
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_SOUTH_EAST:
      rect->x -= frame_width;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_EAST:
      rect->y -= frame_height / 2;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      rect->y -= frame_height;
      break;
    default:
      break;
    }

  /* Adjust to get the top-left corner of the inner window. */
  rect->x += child_x;
  rect->y += child_y;
}

static void
meta_window_apply_session_info (MetaWindow *window,
                                const MetaWindowSessionInfo *info)
{
  if (info->stack_position_set)
    {
      meta_topic (META_DEBUG_SM,
                  "Restoring stack position %d for window %s\n",
                  info->stack_position, window->desc);

      /* FIXME well, I'm not sure how to do this. */
    }

  if (info->minimized_set)
    {
      meta_topic (META_DEBUG_SM,
                  "Restoring minimized state %d for window %s\n",
                  info->minimized, window->desc);

      if (info->minimized)
        meta_window_minimize (window);
    }

  if (info->maximized_set)
    {
      meta_topic (META_DEBUG_SM,
                  "Restoring maximized state %d for window %s\n",
                  info->maximized, window->desc);

      if (window->has_maximize_func && info->maximized)
        {
          meta_window_maximize (window, META_MAXIMIZE_BOTH);

          if (info->saved_rect_set)
            {
              meta_topic (META_DEBUG_SM,
                          "Restoring saved rect %d,%d %dx%d for window %s\n",
                          info->saved_rect.x,
                          info->saved_rect.y,
                          info->saved_rect.width,
                          info->saved_rect.height,
                          window->desc);

              window->saved_rect.x = info->saved_rect.x;
              window->saved_rect.y = info->saved_rect.y;
              window->saved_rect.width = info->saved_rect.width;
              window->saved_rect.height = info->saved_rect.height;
            }
	}
    }

  if (info->on_all_workspaces_set)
    {
      window->on_all_workspaces_requested = info->on_all_workspaces;
      meta_window_on_all_workspaces_changed (window);
      meta_topic (META_DEBUG_SM,
                  "Restoring sticky state %d for window %s\n",
                  window->on_all_workspaces_requested, window->desc);
    }

  if (info->workspace_indices)
    {
      GSList *tmp;
      GSList *spaces;

      spaces = NULL;

      tmp = info->workspace_indices;
      while (tmp != NULL)
        {
          MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
          MetaWorkspace *space;

          space =
            meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                           GPOINTER_TO_INT (tmp->data));

          if (space)
            spaces = g_slist_prepend (spaces, space);

          tmp = tmp->next;
        }

      if (spaces)
        {
          /* XXX: What should we do if there's more than one workspace
           * listed? We only support one workspace for each window.
           *
           * For now, just choose the first one.
           */
          MetaWorkspace *workspace = spaces->data;

          meta_window_change_workspace (window, workspace);
          window->initial_workspace_set = TRUE;

          meta_topic (META_DEBUG_SM,
                      "Restoring saved window %s to workspace %d\n",
                      window->desc,
                      meta_workspace_index (workspace));

          g_slist_free (spaces);
        }
    }

  if (info->geometry_set)
    {
      MetaRectangle rect;
      MetaMoveResizeFlags flags;
      MetaGravity gravity;

      window->placed = TRUE; /* don't do placement algorithms later */

      rect.x = info->rect.x;
      rect.y = info->rect.y;

      rect.width = window->size_hints.base_width + info->rect.width * window->size_hints.width_inc;
      rect.height = window->size_hints.base_height + info->rect.height * window->size_hints.height_inc;

      /* Force old gravity, ignoring anything now set */
      window->size_hints.win_gravity = info->gravity;
      gravity = window->size_hints.win_gravity;

      flags = META_MOVE_RESIZE_MOVE_ACTION | META_MOVE_RESIZE_RESIZE_ACTION;

      adjust_for_gravity (window, FALSE, gravity, &rect);
      meta_window_client_rect_to_frame_rect (window, &rect, &rect);
      meta_window_move_resize_internal (window, flags, gravity, rect);
    }
}

static void
meta_window_x11_manage (MetaWindow *window)
{
  MetaDisplay *display = window->display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  meta_icon_cache_init (&priv->icon_cache);

  meta_x11_display_register_x_window (display->x11_display,
                                      &window->xwindow,
                                      window);

  /* assign the window to its group, or create a new group if needed */
  window->group = NULL;
  window->xgroup_leader = None;
  meta_window_compute_group (window);

  meta_window_load_initial_properties (window);

  if (!window->override_redirect)
    update_sm_hints (window); /* must come after transient_for */

  if (window->decorated)
    meta_window_ensure_frame (window);

  /* Now try applying saved stuff from the session */
  {
    const MetaWindowSessionInfo *info;

    info = meta_window_lookup_saved_state (window);

    if (info)
      {
        meta_window_apply_session_info (window, info);
        meta_window_release_saved_state (info);
      }
  }

  /* For override-redirect windows, save the client rect
   * directly. window->rect was assigned from the XWindowAttributes
   * in the main meta_window_shared_new.
   *
   * For normal windows, do a full ConfigureRequest based on the
   * window hints, as that's what the ICCCM says to do.
   */
  priv->client_rect = window->rect;
  window->buffer_rect = window->rect;

  if (!window->override_redirect)
    {
      MetaRectangle rect;
      MetaMoveResizeFlags flags;
      MetaGravity gravity = window->size_hints.win_gravity;

      rect.x = window->size_hints.x;
      rect.y = window->size_hints.y;
      rect.width = window->size_hints.width;
      rect.height = window->size_hints.height;

      flags = META_MOVE_RESIZE_CONFIGURE_REQUEST | META_MOVE_RESIZE_MOVE_ACTION | META_MOVE_RESIZE_RESIZE_ACTION;

      adjust_for_gravity (window, TRUE, gravity, &rect);
      meta_window_client_rect_to_frame_rect (window, &rect, &rect);
      meta_window_move_resize_internal (window, flags, gravity, rect);
    }

  meta_window_x11_update_shape_region (window);
  meta_window_x11_update_input_region (window);
}

static void
meta_window_x11_unmanage (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  meta_x11_error_trap_push (x11_display);

  meta_window_x11_destroy_sync_request_alarm (window);

  if (window->withdrawn)
    {
      /* We need to clean off the window's state so it
       * won't be restored if the app maps it again.
       */
      meta_verbose ("Cleaning state from window %s\n", window->desc);
      XDeleteProperty (x11_display->xdisplay,
                       window->xwindow,
                       x11_display->atom__NET_WM_DESKTOP);
      XDeleteProperty (x11_display->xdisplay,
                       window->xwindow,
                       x11_display->atom__NET_WM_STATE);
      XDeleteProperty (x11_display->xdisplay,
                       window->xwindow,
                       x11_display->atom__NET_WM_FULLSCREEN_MONITORS);
      meta_window_x11_set_wm_state (window);
    }
  else
    {
      /* We need to put WM_STATE so that others will understand it on
       * restart.
       */
      if (!window->minimized)
        meta_window_x11_set_wm_state (window);

      /* If we're unmanaging a window that is not withdrawn, then
       * either (a) mutter is exiting, in which case we need to map
       * the window so the next WM will know that it's not Withdrawn,
       * or (b) we want to create a new MetaWindow to replace the
       * current one, which will happen automatically if we re-map
       * the X Window.
       */
      XMapWindow (x11_display->xdisplay,
                  window->xwindow);
    }

  meta_x11_display_unregister_x_window (x11_display, window->xwindow);

  /* Put back anything we messed up */
  if (priv->border_width != 0)
    XSetWindowBorderWidth (x11_display->xdisplay,
                           window->xwindow,
                           priv->border_width);

  /* No save set */
  XRemoveFromSaveSet (x11_display->xdisplay,
                      window->xwindow);

  /* Even though the window is now unmanaged, we can't unselect events. This
   * window might be a window from this process, like a GdkMenu, in
   * which case it will have pointer events and so forth selected
   * for it by GDK. There's no way to disentangle those events from the events
   * we've selected. Even for a window from a different X client,
   * GDK could also have selected events for it for IPC purposes, so we
   * can't unselect in that case either.
   *
   * Similarly, we can't unselected for events on window->user_time_window.
   * It might be our own GDK focus window, or it might be a window that a
   * different client is using for multiple different things:
   * _NET_WM_USER_TIME_WINDOW and IPC, perhaps.
   */

  if (window->user_time_window != None)
    {
      meta_x11_display_unregister_x_window (x11_display,
                                            window->user_time_window);
      window->user_time_window = None;
    }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    XShapeSelectInput (x11_display->xdisplay, window->xwindow, NoEventMask);

  meta_window_ungrab_keys (window);
  meta_display_ungrab_window_buttons (window->display, window->xwindow);
  meta_display_ungrab_focus_window_button (window->display, window);

  meta_x11_error_trap_pop (x11_display);

  if (window->frame)
    {
      /* The XReparentWindow call in meta_window_destroy_frame() moves the
       * window so we need to send a configure notify; see bug 399552.  (We
       * also do this just in case a window got unmaximized.)
       */
      send_configure_notify (window);

      meta_window_destroy_frame (window);
    }
}

void
meta_window_x11_set_wm_ping (MetaWindow *window,
                             gboolean    ping)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  priv->wm_ping = ping;
}

static gboolean
meta_window_x11_can_ping (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return priv->wm_ping;
}

static void
meta_window_x11_ping (MetaWindow *window,
                      guint32     serial)
{
  MetaDisplay *display = window->display;

  send_icccm_message (window, display->x11_display->atom__NET_WM_PING, serial);
}

void
meta_window_x11_set_wm_delete_window (MetaWindow *window,
                                      gboolean    delete_window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  priv->wm_delete_window = delete_window;
}

static void
meta_window_x11_delete (MetaWindow *window,
                        guint32     timestamp)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);
  MetaX11Display *x11_display = window->display->x11_display;

  meta_x11_error_trap_push (x11_display);
  if (priv->wm_delete_window)
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Deleting %s with delete_window request\n",
                  window->desc);
      send_icccm_message (window, x11_display->atom_WM_DELETE_WINDOW, timestamp);
    }
  else
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Deleting %s with explicit kill\n",
                  window->desc);
      XKillClient (x11_display->xdisplay, window->xwindow);
    }
  meta_x11_error_trap_pop (x11_display);
}

static void
meta_window_x11_kill (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Disconnecting %s with XKillClient()\n",
              window->desc);

  meta_x11_error_trap_push (x11_display);
  XKillClient (x11_display->xdisplay, window->xwindow);
  meta_x11_error_trap_pop (x11_display);
}

static void
request_take_focus (MetaWindow *window,
                    guint32     timestamp)
{
  MetaDisplay *display = window->display;

  meta_topic (META_DEBUG_FOCUS, "WM_TAKE_FOCUS(%s, %u)\n",
              window->desc, timestamp);

  send_icccm_message (window, display->x11_display->atom_WM_TAKE_FOCUS, timestamp);
}

typedef struct
{
  MetaWindow *window;
  GQueue *pending_focus_candidates;
  guint32 timestamp;
  guint timeout_id;
  gulong unmanaged_id;
  gulong focused_changed_id;
} MetaWindowX11DelayedFocusData;

static void
disconnect_pending_focus_window_signals (MetaWindow *window,
                                         GQueue     *focus_candidates)
{
  g_signal_handlers_disconnect_by_func (window, g_queue_remove,
                                        focus_candidates);
}

static void
meta_window_x11_delayed_focus_data_free (MetaWindowX11DelayedFocusData *data)
{
  g_clear_signal_handler (&data->unmanaged_id, data->window);
  g_clear_signal_handler (&data->focused_changed_id, data->window->display);

  if (data->pending_focus_candidates)
    {
      g_queue_foreach (data->pending_focus_candidates,
                       (GFunc) disconnect_pending_focus_window_signals,
                       data->pending_focus_candidates);
      g_queue_free (data->pending_focus_candidates);
    }

  g_clear_handle_id (&data->timeout_id, g_source_remove);
  g_free (data);
}

static void
focus_candidates_maybe_take_and_focus_next (GQueue  **focus_candidates_ptr,
                                            guint32   timestamp)
{
  MetaWindow *focus_window;
  GQueue *focus_candidates;

  g_assert (*focus_candidates_ptr);

  if (g_queue_is_empty (*focus_candidates_ptr))
    return;

  focus_candidates = g_steal_pointer (focus_candidates_ptr);
  focus_window = g_queue_pop_head (focus_candidates);

  disconnect_pending_focus_window_signals (focus_window, focus_candidates);
  meta_window_x11_maybe_focus_delayed (focus_window, focus_candidates, timestamp);
}

static gboolean
focus_window_delayed_timeout (gpointer user_data)
{
  MetaWindowX11DelayedFocusData *data = user_data;
  MetaWindow *window = data->window;
  guint32 timestamp = data->timestamp;

  focus_candidates_maybe_take_and_focus_next (&data->pending_focus_candidates,
                                              timestamp);

  data->timeout_id = 0;
  meta_window_x11_delayed_focus_data_free (data);

  meta_window_focus (window, timestamp);

  return G_SOURCE_REMOVE;
}

static void
meta_window_x11_maybe_focus_delayed (MetaWindow *window,
                                     GQueue     *other_focus_candidates,
                                     guint32     timestamp)
{
  MetaWindowX11DelayedFocusData *data;

  data = g_new0 (MetaWindowX11DelayedFocusData, 1);
  data->window = window;
  data->timestamp = timestamp;
  data->pending_focus_candidates = other_focus_candidates;

  meta_topic (META_DEBUG_FOCUS,
              "Requesting delayed focus to %s\n", window->desc);

  data->unmanaged_id =
    g_signal_connect_swapped (window, "unmanaged",
                              G_CALLBACK (meta_window_x11_delayed_focus_data_free),
                              data);

  data->focused_changed_id =
    g_signal_connect_swapped (window->display, "notify::focus-window",
                              G_CALLBACK (meta_window_x11_delayed_focus_data_free),
                              data);

  data->timeout_id = g_timeout_add (TAKE_FOCUS_FALLBACK_DELAY_MS,
                                    focus_window_delayed_timeout, data);
}

static void
maybe_focus_default_window (MetaDisplay *display,
                            MetaWindow  *not_this_one,
                            guint32      timestamp)
{
  MetaWorkspace *workspace;
  MetaStack *stack = display->stack;
  g_autoptr (GList) focusable_windows = NULL;
  g_autoptr (GQueue) focus_candidates = NULL;
  GList *l;

  if (not_this_one && not_this_one->workspace)
    workspace = not_this_one->workspace;
  else
    workspace = display->workspace_manager->active_workspace;

   /* Go through all the focusable windows and try to focus them
    * in order, waiting for a delay. The first one that replies to
    * the request (in case of take focus windows) changing the display
    * focused window, will stop the chained requests.
    */
  focusable_windows =
    meta_stack_get_default_focus_candidates (stack, workspace);
  focus_candidates = g_queue_new ();

  for (l = g_list_last (focusable_windows); l; l = l->prev)
    {
      MetaWindow *focus_window = l->data;

      if (focus_window == not_this_one)
        continue;

      g_queue_push_tail (focus_candidates, focus_window);
      g_signal_connect_swapped (focus_window, "unmanaged",
                                G_CALLBACK (g_queue_remove),
                                focus_candidates);

      if (!META_IS_WINDOW_X11 (focus_window))
        break;

      if (focus_window->input)
        break;

      if (focus_window->shaded && focus_window->frame)
        break;
    }

  focus_candidates_maybe_take_and_focus_next (&focus_candidates, timestamp);
}

static void
meta_window_x11_focus (MetaWindow *window,
                       guint32     timestamp)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);
  /* For output-only or shaded windows, focus the frame.
   * This seems to result in the client window getting key events
   * though, so I don't know if it's icccm-compliant.
   *
   * Still, we have to do this or keynav breaks for these windows.
   */
  if (window->frame &&
      (window->shaded || !meta_window_is_focusable (window)))
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Focusing frame of %s\n", window->desc);
      meta_display_set_input_focus (window->display,
                                    window,
                                    TRUE,
                                    timestamp);
    }
  else
    {
      if (window->input)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Setting input focus on %s since input = true\n",
                      window->desc);
          meta_display_set_input_focus (window->display,
                                        window,
                                        FALSE,
                                        timestamp);
        }

      if (priv->wm_take_focus)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Sending WM_TAKE_FOCUS to %s since take_focus = true\n",
                      window->desc);

          if (!window->input)
            {
              /* The "Globally Active Input" window case, where the window
               * doesn't want us to call XSetInputFocus on it, but does
               * want us to send a WM_TAKE_FOCUS.
               *
               * Normally, we want to just leave the focus undisturbed until
               * the window responds to WM_TAKE_FOCUS, but if we're unmanaging
               * the current focus window we *need* to move the focus away, so
               * we focus the no focus window before sending WM_TAKE_FOCUS,
               * and eventually the default focus window excluding this one,
               * if meanwhile we don't get any focus request.
               */
              if (window->display->focus_window != NULL &&
                  window->display->focus_window->unmanaging)
                {
                  meta_display_unset_input_focus (window->display, timestamp);
                  maybe_focus_default_window (window->display, window,
                                              timestamp);
                }
            }

          request_take_focus (window, timestamp);
        }
    }
}

static void
meta_window_get_client_root_coords (MetaWindow    *window,
                                    MetaRectangle *rect)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  *rect = priv->client_rect;

  if (window->frame)
    {
      rect->x += window->frame->rect.x;
      rect->y += window->frame->rect.y;
    }
}

static void
meta_window_refresh_resize_popup (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->showing_resize_popup)
    {
      MetaRectangle rect;
      int display_w, display_h;

      meta_window_get_client_root_coords (window, &rect);

      display_w = (rect.width - window->size_hints.base_width);
      if (window->size_hints.width_inc > 0)
        display_w /= window->size_hints.width_inc;

      display_h = (rect.height - window->size_hints.base_height);
      if (window->size_hints.height_inc > 0)
        display_h /= window->size_hints.height_inc;

      meta_display_show_resize_popup (window->display, TRUE, &rect, display_w, display_h);
    }
  else
    {
      meta_display_show_resize_popup (window->display, FALSE, NULL, 0, 0);
    }
}

static void
meta_window_x11_grab_op_began (MetaWindow *window,
                               MetaGrabOp  op)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (meta_grab_op_is_resizing (op))
    {
      if (window->sync_request_counter != None)
        meta_window_x11_create_sync_request_alarm (window);

      if (window->size_hints.width_inc > 2 || window->size_hints.height_inc > 2)
        {
          priv->showing_resize_popup = TRUE;
          meta_window_refresh_resize_popup (window);
        }
    }

  META_WINDOW_CLASS (meta_window_x11_parent_class)->grab_op_began (window, op);
}

static void
meta_window_x11_grab_op_ended (MetaWindow *window,
                               MetaGrabOp  op)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  if (priv->showing_resize_popup)
    {
      priv->showing_resize_popup = FALSE;
      meta_window_refresh_resize_popup (window);
    }

  META_WINDOW_CLASS (meta_window_x11_parent_class)->grab_op_ended (window, op);
}

static void
update_net_frame_extents (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  unsigned long data[4];
  MetaFrameBorders borders;

  meta_frame_calc_borders (window->frame, &borders);
  /* Left */
  data[0] = borders.visible.left;
  /* Right */
  data[1] = borders.visible.right;
  /* Top */
  data[2] = borders.visible.top;
  /* Bottom */
  data[3] = borders.visible.bottom;

  meta_topic (META_DEBUG_GEOMETRY,
              "Setting _NET_FRAME_EXTENTS on managed window 0x%lx "
 "to left = %lu, right = %lu, top = %lu, bottom = %lu\n",
              window->xwindow, data[0], data[1], data[2], data[3]);

  meta_x11_error_trap_push (x11_display);
  XChangeProperty (x11_display->xdisplay, window->xwindow,
                   x11_display->atom__NET_FRAME_EXTENTS,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 4);
  meta_x11_error_trap_pop (x11_display);
}

static gboolean
is_edge_constraint_resizable (MetaEdgeConstraint constraint)
{
  switch (constraint)
    {
    case META_EDGE_CONSTRAINT_NONE:
    case META_EDGE_CONSTRAINT_WINDOW:
      return TRUE;
    case META_EDGE_CONSTRAINT_MONITOR:
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static gboolean
is_edge_constraint_tiled (MetaEdgeConstraint constraint)
{
  switch (constraint)
    {
    case META_EDGE_CONSTRAINT_NONE:
      return FALSE;
    case META_EDGE_CONSTRAINT_WINDOW:
    case META_EDGE_CONSTRAINT_MONITOR:
      return TRUE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static unsigned long
edge_constraints_to_gtk_edge_constraints (MetaWindow *window)
{
  unsigned long gtk_edge_constraints = 0;

  if (is_edge_constraint_tiled (window->edge_constraints.top))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_TOP_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.top))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_TOP_RESIZABLE;

  if (is_edge_constraint_tiled (window->edge_constraints.right))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_RIGHT_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.right))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_RIGHT_RESIZABLE;

  if (is_edge_constraint_tiled (window->edge_constraints.bottom))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_BOTTOM_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.bottom))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_BOTTOM_RESIZABLE;

  if (is_edge_constraint_tiled (window->edge_constraints.left))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_LEFT_TILED;
  if (is_edge_constraint_resizable (window->edge_constraints.left))
    gtk_edge_constraints |= META_GTK_EDGE_CONSTRAINT_LEFT_RESIZABLE;

  return gtk_edge_constraints;
}

static void
update_gtk_edge_constraints (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  unsigned long data[1];

  data[0] = edge_constraints_to_gtk_edge_constraints (window);

  meta_verbose ("Setting _GTK_EDGE_CONSTRAINTS to %lu\n", data[0]);

  meta_x11_error_trap_push (x11_display);
  XChangeProperty (x11_display->xdisplay,
                   window->xwindow,
                   x11_display->atom__GTK_EDGE_CONSTRAINTS,
                   XA_CARDINAL, 32, PropModeReplace,
                   (guchar*) data, 1);
  meta_x11_error_trap_pop (x11_display);
}

static gboolean
sync_request_timeout (gpointer data)
{
  MetaWindow *window = data;

  window->sync_request_timeout_id = 0;

  /* We have now waited for more than a second for the
   * application to respond to the sync request
   */
  window->disable_sync = TRUE;

  /* Reset the wait serial, so we don't continue freezing
   * window updates
   */
  window->sync_request_wait_serial = 0;
  meta_compositor_sync_updates_frozen (window->display->compositor, window);

  if (window == window->display->grab_window &&
      meta_grab_op_is_resizing (window->display->grab_op))
    {
      meta_window_update_resize (window,
                                 window->display->grab_last_user_action_was_snap,
                                 window->display->grab_latest_motion_x,
                                 window->display->grab_latest_motion_y,
                                 TRUE);
    }

  return FALSE;
}

static void
send_sync_request (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  XClientMessageEvent ev;
  gint64 wait_serial;

  /* For the old style of _NET_WM_SYNC_REQUEST_COUNTER, we just have to
   * increase the value, but for the new "extended" style we need to
   * pick an even (unfrozen) value sufficiently ahead of the last serial
   * that we received from the client; the same code still works
   * for the old style. The increment of 240 is specified by the EWMH
   * and is (1 second) * (60fps) * (an increment of 4 per frame).
   */
  wait_serial = window->sync_request_serial + 240;

  window->sync_request_wait_serial = wait_serial;

  ev.type = ClientMessage;
  ev.window = window->xwindow;
  ev.message_type = x11_display->atom_WM_PROTOCOLS;
  ev.format = 32;
  ev.data.l[0] = x11_display->atom__NET_WM_SYNC_REQUEST;
  /* FIXME: meta_display_get_current_time() is bad, but since calls
   * come from meta_window_move_resize_internal (which in turn come
   * from all over), I'm not sure what we can do to fix it.  Do we
   * want to use _roundtrip, though?
   */
  ev.data.l[1] = meta_display_get_current_time (window->display);
  ev.data.l[2] = wait_serial & G_GUINT64_CONSTANT(0xffffffff);
  ev.data.l[3] = wait_serial >> 32;
  ev.data.l[4] = window->extended_sync_request_counter ? 1 : 0;

  /* We don't need to trap errors here as we are already
   * inside an error_trap_push()/pop() pair.
   */
  XSendEvent (x11_display->xdisplay,
	      window->xwindow, False, 0, (XEvent*) &ev);

  /* We give the window 1 sec to respond to _NET_WM_SYNC_REQUEST;
   * if this time expires, we consider the window unresponsive
   * and resize it unsynchonized.
   */
  window->sync_request_timeout_id = g_timeout_add (1000,
                                                   sync_request_timeout,
                                                   window);
  g_source_set_name_by_id (window->sync_request_timeout_id,
                           "[mutter] sync_request_timeout");

  meta_compositor_sync_updates_frozen (window->display->compositor, window);
}

static unsigned long
meta_window_get_net_wm_desktop (MetaWindow *window)
{
  if (window->on_all_workspaces)
    return 0xFFFFFFFF;
  else
    return meta_workspace_index (window->workspace);
}

static void
meta_window_x11_current_workspace_changed (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  /* FIXME if on more than one workspace, we claim to be "sticky",
   * the WM spec doesn't say what to do here.
   */
  unsigned long data[1];

  if (window->workspace == NULL)
    {
      /* this happens when unmanaging windows */
      return;
    }

  data[0] = meta_window_get_net_wm_desktop (window);

  meta_verbose ("Setting _NET_WM_DESKTOP of %s to %lu\n",
                window->desc, data[0]);

  meta_x11_error_trap_push (x11_display);
  XChangeProperty (x11_display->xdisplay, window->xwindow,
                   x11_display->atom__NET_WM_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_x11_error_trap_pop (x11_display);
}

static void
meta_window_x11_move_resize_internal (MetaWindow                *window,
                                      MetaGravity                gravity,
                                      MetaRectangle              unconstrained_rect,
                                      MetaRectangle              constrained_rect,
                                      MetaRectangle              intermediate_rect,
                                      int                        rel_x,
                                      int                        rel_y,
                                      MetaMoveResizeFlags        flags,
                                      MetaMoveResizeResultFlags *result)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaFrameBorders borders;
  MetaRectangle client_rect;
  int size_dx, size_dy;
  XWindowChanges values;
  unsigned int mask;
  gboolean need_configure_notify;
  gboolean need_move_client = FALSE;
  gboolean need_move_frame = FALSE;
  gboolean need_resize_client = FALSE;
  gboolean need_resize_frame = FALSE;
  gboolean frame_shape_changed = FALSE;
  gboolean configure_frame_first;

  gboolean is_configure_request;

  is_configure_request = (flags & META_MOVE_RESIZE_CONFIGURE_REQUEST) != 0;

  meta_frame_calc_borders (window->frame, &borders);

  size_dx = constrained_rect.x - window->rect.width;
  size_dy = constrained_rect.y - window->rect.height;

  window->rect = constrained_rect;

  if (window->frame)
    {
      int new_w, new_h;
      int new_x, new_y;

      /* Compute new frame size */
      new_w = window->rect.width + borders.invisible.left + borders.invisible.right;

      if (window->shaded)
        new_h = borders.total.top + borders.total.bottom;
      else
        new_h = window->rect.height + borders.invisible.top + borders.invisible.bottom;

      if (new_w != window->frame->rect.width ||
          new_h != window->frame->rect.height)
        {
          need_resize_frame = TRUE;
          window->frame->rect.width = new_w;
          window->frame->rect.height = new_h;
        }

      /* Compute new frame coords */
      new_x = window->rect.x - borders.invisible.left;
      new_y = window->rect.y - borders.invisible.top;

      if (new_x != window->frame->rect.x ||
          new_y != window->frame->rect.y)
        {
          need_move_frame = TRUE;
          window->frame->rect.x = new_x;
          window->frame->rect.y = new_y;
        }
    }

  /* Calculate the new client rect */
  meta_window_frame_rect_to_client_rect (window, &constrained_rect, &client_rect);

  /* The above client_rect is in root window coordinates. The
   * values we need to pass to XConfigureWindow are in parent
   * coordinates, so if the window is in a frame, we need to
   * correct the x/y positions here. */
  if (window->frame)
    {
      client_rect.x = borders.total.left;
      client_rect.y = borders.total.top;
    }

  if (client_rect.x != priv->client_rect.x ||
      client_rect.y != priv->client_rect.y)
    {
      need_move_client = TRUE;
      priv->client_rect.x = client_rect.x;
      priv->client_rect.y = client_rect.y;
    }

  if (client_rect.width != priv->client_rect.width ||
      client_rect.height != priv->client_rect.height)
    {
      need_resize_client = TRUE;
      priv->client_rect.width = client_rect.width;
      priv->client_rect.height = client_rect.height;
    }

  /* If frame extents have changed, fill in other frame fields and
     change frame's extents property. */
  if (window->frame &&
      (window->frame->child_x != borders.total.left ||
       window->frame->child_y != borders.total.top ||
       window->frame->right_width != borders.total.right ||
       window->frame->bottom_height != borders.total.bottom))
    {
      window->frame->child_x = borders.total.left;
      window->frame->child_y = borders.total.top;
      window->frame->right_width = borders.total.right;
      window->frame->bottom_height = borders.total.bottom;

      update_net_frame_extents (window);
    }

  /* See ICCCM 4.1.5 for when to send ConfigureNotify */

  need_configure_notify = FALSE;

  /* If this is a configure request and we change nothing, then we
   * must send configure notify.
   */
  if  (is_configure_request &&
       !(need_move_client || need_move_frame ||
         need_resize_client || need_resize_frame ||
         priv->border_width != 0))
    need_configure_notify = TRUE;

  /* We must send configure notify if we move but don't resize, since
   * the client window may not get a real event
   */
  if ((need_move_client || need_move_frame) &&
      !(need_resize_client || need_resize_frame))
    need_configure_notify = TRUE;

  /* MapRequest events with a PPosition or UPosition hint with a frame
   * are moved by mutter without resizing; send a configure notify
   * in such cases.  See #322840.  (Note that window->constructing is
   * only true iff this call is due to a MapRequest, and when
   * PPosition/UPosition hints aren't set, mutter seems to send a
   * ConfigureNotify anyway due to the above code.)
   */
  if (window->constructing && window->frame &&
      ((window->size_hints.flags & PPosition) ||
       (window->size_hints.flags & USPosition)))
    need_configure_notify = TRUE;

  /* If resizing, freeze commits - This is for Xwayland, and a no-op on Xorg */
  if (need_resize_client || need_resize_frame)
    {
      if (!meta_window_x11_should_thaw_after_paint (window))
        {
          meta_window_x11_set_thaw_after_paint (window, TRUE);
          meta_window_x11_freeze_commits (window);
        }
    }

  /* The rest of this function syncs our new size/pos with X as
   * efficiently as possible
   */

  /* For nice effect, when growing the window we want to move/resize
   * the frame first, when shrinking the window we want to move/resize
   * the client first. If we grow one way and shrink the other,
   * see which way we're moving "more"
   *
   * Mail from Owen subject "Suggestion: Gravity and resizing from the left"
   * http://mail.gnome.org/archives/wm-spec-list/1999-November/msg00088.html
   *
   * An annoying fact you need to know in this code is that META_GRAVITY_STATIC
   * does nothing if you _only_ resize or _only_ move the frame;
   * it must move _and_ resize, otherwise you get META_GRAVITY_NORTH_WEST
   * behavior. The move and resize must actually occur, it is not
   * enough to set CWX | CWWidth but pass in the current size/pos.
   */

  /* Normally, we configure the frame first depending on whether
   * we grow the frame more than we shrink. The idea is to avoid
   * messing up the window contents by having a temporary situation
   * where the frame is smaller than the window. However, if we're
   * cooperating with the client to create an atomic frame upate,
   * and the window is redirected, then we should always update
   * the frame first, since updating the frame will force a new
   * backing pixmap to be allocated, and the old backing pixmap
   * will be left undisturbed for us to paint to the screen until
   * the client finishes redrawing.
   */
  if (window->extended_sync_request_counter)
    configure_frame_first = TRUE;
  else
    configure_frame_first = size_dx + size_dy >= 0;

  if (configure_frame_first && window->frame)
    frame_shape_changed = meta_frame_sync_to_window (window->frame, need_resize_frame);

  values.border_width = 0;
  values.x = client_rect.x;
  values.y = client_rect.y;
  values.width = client_rect.width;
  values.height = client_rect.height;

  mask = 0;
  if (is_configure_request && priv->border_width != 0)
    mask |= CWBorderWidth; /* must force to 0 */
  if (need_move_client)
    mask |= (CWX | CWY);
  if (need_resize_client)
    mask |= (CWWidth | CWHeight);

  if (mask != 0)
    {
      meta_x11_error_trap_push (window->display->x11_display);

      if (window == window->display->grab_window &&
          meta_grab_op_is_resizing (window->display->grab_op) &&
          !window->disable_sync &&
          window->sync_request_counter != None &&
          window->sync_request_alarm != None &&
          window->sync_request_timeout_id == 0)
        {
          send_sync_request (window);
        }

      XConfigureWindow (window->display->x11_display->xdisplay,
                        window->xwindow,
                        mask,
                        &values);

      meta_x11_error_trap_pop (window->display->x11_display);
    }

  if (!configure_frame_first && window->frame)
    frame_shape_changed = meta_frame_sync_to_window (window->frame, need_resize_frame);

  if (window->frame)
    window->buffer_rect = window->frame->rect;
  else
    window->buffer_rect = client_rect;

  if (need_configure_notify)
    send_configure_notify (window);

  if (priv->showing_resize_popup)
    meta_window_refresh_resize_popup (window);

  if (frame_shape_changed)
    *result |= META_MOVE_RESIZE_RESULT_FRAME_SHAPE_CHANGED;
  if (need_move_client || need_move_frame)
    *result |= META_MOVE_RESIZE_RESULT_MOVED;
  if (need_resize_client || need_resize_frame)
    *result |= META_MOVE_RESIZE_RESULT_RESIZED;
  if (flags & META_MOVE_RESIZE_STATE_CHANGED)
    *result |= META_MOVE_RESIZE_RESULT_STATE_CHANGED;

  update_gtk_edge_constraints (window);
}

static gboolean
meta_window_x11_update_struts (MetaWindow *window)
{
  GSList *old_struts;
  GSList *new_struts;
  GSList *old_iter, *new_iter;
  uint32_t *struts = NULL;
  int nitems;
  gboolean changed;

  g_return_val_if_fail (!window->override_redirect, FALSE);

  meta_verbose ("Updating struts for %s\n", window->desc);

  old_struts = window->struts;
  new_struts = NULL;

  if (meta_prop_get_cardinal_list (window->display->x11_display,
                                   window->xwindow,
                                   window->display->x11_display->atom__NET_WM_STRUT_PARTIAL,
                                   &struts, &nitems))
    {
      if (nitems != 12)
        meta_verbose ("_NET_WM_STRUT_PARTIAL on %s has %d values instead "
                      "of 12\n",
                      window->desc, nitems);
      else
        {
          /* Pull out the strut info for each side in the hint */
          int i;
          for (i=0; i<4; i++)
            {
              MetaStrut *temp;
              int thickness, strut_begin, strut_end;

              thickness = struts[i];
              if (thickness == 0)
                continue;
              strut_begin = struts[4+(i*2)];
              strut_end   = struts[4+(i*2)+1];

              temp = g_new0 (MetaStrut, 1);
              temp->side = 1 << i; /* See MetaSide def.  Matches nicely, eh? */
              meta_display_get_size (window->display,
                                     &temp->rect.width, &temp->rect.height);
              switch (temp->side)
                {
                case META_SIDE_RIGHT:
                  temp->rect.x = BOX_RIGHT(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_LEFT:
                  temp->rect.width  = thickness;
                  temp->rect.y      = strut_begin;
                  temp->rect.height = strut_end - strut_begin + 1;
                  break;
                case META_SIDE_BOTTOM:
                  temp->rect.y = BOX_BOTTOM(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_TOP:
                  temp->rect.height = thickness;
                  temp->rect.x      = strut_begin;
                  temp->rect.width  = strut_end - strut_begin + 1;
                  break;
                default:
                  g_assert_not_reached ();
                }

              new_struts = g_slist_prepend (new_struts, temp);
            }

          meta_verbose ("_NET_WM_STRUT_PARTIAL struts %u %u %u %u for "
                        "window %s\n",
                        struts[0], struts[1], struts[2], struts[3],
                        window->desc);
        }
      g_free (struts);
    }
  else
    {
      meta_verbose ("No _NET_WM_STRUT property for %s\n",
                    window->desc);
    }

  if (!new_struts &&
      meta_prop_get_cardinal_list (window->display->x11_display,
                                   window->xwindow,
                                   window->display->x11_display->atom__NET_WM_STRUT,
                                   &struts, &nitems))
    {
      if (nitems != 4)
        meta_verbose ("_NET_WM_STRUT on %s has %d values instead of 4\n",
                      window->desc, nitems);
      else
        {
          /* Pull out the strut info for each side in the hint */
          int i;
          for (i=0; i<4; i++)
            {
              MetaStrut *temp;
              int thickness;

              thickness = struts[i];
              if (thickness == 0)
                continue;

              temp = g_new0 (MetaStrut, 1);
              temp->side = 1 << i;
              meta_display_get_size (window->display,
                                     &temp->rect.width, &temp->rect.height);
              switch (temp->side)
                {
                case META_SIDE_RIGHT:
                  temp->rect.x = BOX_RIGHT(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_LEFT:
                  temp->rect.width  = thickness;
                  break;
                case META_SIDE_BOTTOM:
                  temp->rect.y = BOX_BOTTOM(temp->rect) - thickness;
                  G_GNUC_FALLTHROUGH;
                case META_SIDE_TOP:
                  temp->rect.height = thickness;
                  break;
                default:
                  g_assert_not_reached ();
                }

              new_struts = g_slist_prepend (new_struts, temp);
            }

          meta_verbose ("_NET_WM_STRUT struts %u %u %u %u for window %s\n",
                        struts[0], struts[1], struts[2], struts[3],
                        window->desc);
        }
      g_free (struts);
    }
  else if (!new_struts)
    {
      meta_verbose ("No _NET_WM_STRUT property for %s\n",
                    window->desc);
    }

  /* Determine whether old_struts and new_struts are the same */
  old_iter = old_struts;
  new_iter = new_struts;
  while (old_iter && new_iter)
    {
      MetaStrut *old_strut = (MetaStrut*) old_iter->data;
      MetaStrut *new_strut = (MetaStrut*) new_iter->data;

      if (old_strut->side != new_strut->side ||
          !meta_rectangle_equal (&old_strut->rect, &new_strut->rect))
        break;

      old_iter = old_iter->next;
      new_iter = new_iter->next;
    }
  changed = (old_iter != NULL || new_iter != NULL);

  /* Update appropriately */
  g_slist_free_full (old_struts, g_free);
  window->struts = new_struts;
  return changed;
}

static void
meta_window_x11_get_default_skip_hints (MetaWindow *window,
                                        gboolean   *skip_taskbar_out,
                                        gboolean   *skip_pager_out)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  *skip_taskbar_out = priv->wm_state_skip_taskbar;
  *skip_pager_out = priv->wm_state_skip_pager;
}

static gboolean
meta_window_x11_update_icon (MetaWindow       *window,
                             cairo_surface_t **icon,
                             cairo_surface_t **mini_icon)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return meta_read_icons (window->display->x11_display,
                          window->xwindow,
                          &priv->icon_cache,
                          priv->wm_hints_pixmap,
                          priv->wm_hints_mask,
                          icon,
                          META_ICON_WIDTH, META_ICON_HEIGHT,
                          mini_icon,
                          META_MINI_ICON_WIDTH, META_MINI_ICON_HEIGHT);
}

static void
meta_window_x11_update_main_monitor (MetaWindow                   *window,
                                     MetaWindowUpdateMonitorFlags  flags)
{
  window->monitor = meta_window_calculate_main_logical_monitor (window);
}

static void
meta_window_x11_main_monitor_changed (MetaWindow               *window,
                                      const MetaLogicalMonitor *old)
{
}

static uint32_t
meta_window_x11_get_client_pid (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  xcb_connection_t *xcb = XGetXCBConnection (x11_display->xdisplay);
  xcb_res_client_id_spec_t spec = { 0 };
  xcb_res_query_client_ids_cookie_t cookie;
  xcb_res_query_client_ids_reply_t *reply = NULL;

  spec.client = window->xwindow;
  spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

  cookie = xcb_res_query_client_ids (xcb, 1, &spec);
  reply = xcb_res_query_client_ids_reply (xcb, cookie, NULL);

  if (reply == NULL)
    return 0;

  uint32_t pid = 0, *value;
  xcb_res_client_id_value_iterator_t it;
  for (it = xcb_res_query_client_ids_ids_iterator (reply);
       it.rem;
       xcb_res_client_id_value_next (&it))
    {
      spec = it.data->spec;
      if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID)
        {
          value = xcb_res_client_id_value_value (it.data);
          pid = *value;
          break;
        }
    }

  free (reply);
  return pid;
}

static void
meta_window_x11_force_restore_shortcuts (MetaWindow         *window,
                                         ClutterInputDevice *source)
{
  /*
   * Not needed on X11 because clients can use a keyboard grab
   * to bypass the compositor shortcuts.
   */
}

static gboolean
meta_window_x11_shortcuts_inhibited (MetaWindow         *window,
                                     ClutterInputDevice *source)
{
  /*
   * On X11, we don't use a shortcuts inhibitor, clients just grab
   * the keyboard.
   */
  return FALSE;
}

void
meta_window_x11_set_wm_take_focus (MetaWindow *window,
                                   gboolean    take_focus)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  priv->wm_take_focus = take_focus;
}

static gboolean
meta_window_x11_is_focusable (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return window->input || priv->wm_take_focus;
}

static gboolean
meta_window_x11_is_stackable (MetaWindow *window)
{
  return !window->override_redirect;
}

static gboolean
meta_window_x11_are_updates_frozen (MetaWindow *window)
{
  if (window->extended_sync_request_counter &&
      window->sync_request_serial % 2 == 1)
    return TRUE;

  if (window->sync_request_serial < window->sync_request_wait_serial)
    return TRUE;

  return FALSE;
}

/* Get layer ignoring any transient or group relationships */
static MetaStackLayer
get_standalone_layer (MetaWindow *window)
{
  MetaStackLayer layer;

  switch (window->type)
    {
    case META_WINDOW_DESKTOP:
      layer = META_LAYER_DESKTOP;
      break;

    case META_WINDOW_DOCK:
      if (window->wm_state_below ||
          (window->monitor && window->monitor->in_fullscreen))
        layer = META_LAYER_BOTTOM;
      else
        layer = META_LAYER_DOCK;
      break;

    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_COMBO:
    case META_WINDOW_OVERRIDE_OTHER:
      layer = META_LAYER_OVERRIDE_REDIRECT;
      break;

    default:
      layer = meta_window_get_default_layer (window);
      break;
    }

  return layer;
}

/* Note that this function can never use window->layer only
 * get_standalone_layer, or we'd have issues.
 */
static MetaStackLayer
get_maximum_layer_in_group (MetaWindow *window)
{
  GSList *members;
  MetaGroup *group;
  GSList *tmp;
  MetaStackLayer max;
  MetaStackLayer layer;

  max = META_LAYER_DESKTOP;

  group = meta_window_get_group (window);

  if (group != NULL)
    members = meta_group_list_windows (group);
  else
    members = NULL;

  tmp = members;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      if (!w->override_redirect)
        {
          layer = get_standalone_layer (w);
          if (layer > max)
            max = layer;
        }

      tmp = tmp->next;
    }

  g_slist_free (members);

  return max;
}

static MetaStackLayer
meta_window_x11_calculate_layer (MetaWindow *window)
{
  MetaStackLayer layer = get_standalone_layer (window);

  /* We can only do promotion-due-to-group for dialogs and other
   * transients, or weird stuff happens like the desktop window and
   * nautilus windows getting in the same layer, or all gnome-terminal
   * windows getting in fullscreen layer if any terminal is
   * fullscreen.
   */
  if (layer != META_LAYER_DESKTOP &&
      meta_window_has_transient_type (window) &&
      window->transient_for == NULL)
    {
      /* We only do the group thing if the dialog is NOT transient for
       * a particular window. Imagine a group with a normal window, a dock,
       * and a dialog transient for the normal window; you don't want the dialog
       * above the dock if it wouldn't normally be.
       */

      MetaStackLayer group_max;

      group_max = get_maximum_layer_in_group (window);

      if (group_max > layer)
        {
          meta_topic (META_DEBUG_STACK,
                      "Promoting window %s from layer %u to %u due to group membership\n",
                      window->desc, layer, group_max);
          layer = group_max;
        }
    }

  meta_topic (META_DEBUG_STACK, "Window %s on layer %u type = %u has_focus = %d\n",
              window->desc, layer,
              window->type, window->has_focus);
  return layer;
}

static void
meta_window_x11_impl_freeze_commits (MetaWindow *window)
{
}

static void
meta_window_x11_impl_thaw_commits (MetaWindow *window)
{
}

static void
meta_window_x11_map (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  meta_x11_error_trap_push (x11_display);
  XMapWindow (x11_display->xdisplay, window->xwindow);
  meta_x11_error_trap_pop (x11_display);
}

static void
meta_window_x11_unmap (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  meta_x11_error_trap_push (x11_display);
  XUnmapWindow (x11_display->xdisplay, window->xwindow);
  meta_x11_error_trap_pop (x11_display);
  window->unmaps_pending ++;
}

static gboolean
meta_window_x11_impl_always_update_shape (MetaWindow *window)
{
  return FALSE;
}

static gboolean
meta_window_x11_is_focus_async (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv =
    meta_window_x11_get_instance_private (window_x11);

  return !window->input && priv->wm_take_focus;
}

static void
meta_window_x11_class_init (MetaWindowX11Class *klass)
{
  MetaWindowClass *window_class = META_WINDOW_CLASS (klass);

  window_class->manage = meta_window_x11_manage;
  window_class->unmanage = meta_window_x11_unmanage;
  window_class->ping = meta_window_x11_ping;
  window_class->delete = meta_window_x11_delete;
  window_class->kill = meta_window_x11_kill;
  window_class->focus = meta_window_x11_focus;
  window_class->grab_op_began = meta_window_x11_grab_op_began;
  window_class->grab_op_ended = meta_window_x11_grab_op_ended;
  window_class->current_workspace_changed = meta_window_x11_current_workspace_changed;
  window_class->move_resize_internal = meta_window_x11_move_resize_internal;
  window_class->update_struts = meta_window_x11_update_struts;
  window_class->get_default_skip_hints = meta_window_x11_get_default_skip_hints;
  window_class->update_icon = meta_window_x11_update_icon;
  window_class->update_main_monitor = meta_window_x11_update_main_monitor;
  window_class->main_monitor_changed = meta_window_x11_main_monitor_changed;
  window_class->get_client_pid = meta_window_x11_get_client_pid;
  window_class->force_restore_shortcuts = meta_window_x11_force_restore_shortcuts;
  window_class->shortcuts_inhibited = meta_window_x11_shortcuts_inhibited;
  window_class->is_focusable = meta_window_x11_is_focusable;
  window_class->is_stackable = meta_window_x11_is_stackable;
  window_class->can_ping = meta_window_x11_can_ping;
  window_class->are_updates_frozen = meta_window_x11_are_updates_frozen;
  window_class->calculate_layer = meta_window_x11_calculate_layer;
  window_class->map = meta_window_x11_map;
  window_class->unmap = meta_window_x11_unmap;
  window_class->is_focus_async = meta_window_x11_is_focus_async;

  klass->freeze_commits = meta_window_x11_impl_freeze_commits;
  klass->thaw_commits = meta_window_x11_impl_thaw_commits;
  klass->always_update_shape = meta_window_x11_impl_always_update_shape;
}

void
meta_window_x11_set_net_wm_state (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  int i;
  unsigned long data[13];

  i = 0;
  if (window->shaded)
    {
      data[i] = x11_display->atom__NET_WM_STATE_SHADED;
      ++i;
    }
  if (priv->wm_state_modal)
    {
      data[i] = x11_display->atom__NET_WM_STATE_MODAL;
      ++i;
    }
  if (window->skip_pager)
    {
      data[i] = x11_display->atom__NET_WM_STATE_SKIP_PAGER;
      ++i;
    }
  if (window->skip_taskbar)
    {
      data[i] = x11_display->atom__NET_WM_STATE_SKIP_TASKBAR;
      ++i;
    }
  if (window->maximized_horizontally)
    {
      data[i] = x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ;
      ++i;
    }
  if (window->maximized_vertically)
    {
      data[i] = x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT;
      ++i;
    }
  if (window->fullscreen)
    {
      data[i] = x11_display->atom__NET_WM_STATE_FULLSCREEN;
      ++i;
    }
  if (!meta_window_showing_on_its_workspace (window) || window->shaded)
    {
      data[i] = x11_display->atom__NET_WM_STATE_HIDDEN;
      ++i;
    }
  if (window->wm_state_above)
    {
      data[i] = x11_display->atom__NET_WM_STATE_ABOVE;
      ++i;
    }
  if (window->wm_state_below)
    {
      data[i] = x11_display->atom__NET_WM_STATE_BELOW;
      ++i;
    }
  if (window->wm_state_demands_attention)
    {
      data[i] = x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION;
      ++i;
    }
  if (window->on_all_workspaces_requested)
    {
      data[i] = x11_display->atom__NET_WM_STATE_STICKY;
      ++i;
    }
  if (meta_window_appears_focused (window))
    {
      data[i] = x11_display->atom__NET_WM_STATE_FOCUSED;
      ++i;
    }

  meta_verbose ("Setting _NET_WM_STATE with %d atoms\n", i);

  meta_x11_error_trap_push (x11_display);
  XChangeProperty (x11_display->xdisplay, window->xwindow,
                   x11_display->atom__NET_WM_STATE,
                   XA_ATOM,
                   32, PropModeReplace, (guchar*) data, i);
  meta_x11_error_trap_pop (x11_display);

  if (window->fullscreen)
    {
      if (meta_window_has_fullscreen_monitors (window))
        {
          data[0] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.top);
          data[1] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.bottom);
          data[2] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.left);
          data[3] =
            meta_x11_display_logical_monitor_to_xinerama_index (window->display->x11_display,
                                                                window->fullscreen_monitors.right);

          meta_verbose ("Setting _NET_WM_FULLSCREEN_MONITORS\n");
          meta_x11_error_trap_push (x11_display);
          XChangeProperty (x11_display->xdisplay,
                           window->xwindow,
                           x11_display->atom__NET_WM_FULLSCREEN_MONITORS,
                           XA_CARDINAL, 32, PropModeReplace,
                           (guchar*) data, 4);
          meta_x11_error_trap_pop (x11_display);
        }
      else
        {
          meta_verbose ("Clearing _NET_WM_FULLSCREEN_MONITORS\n");
          meta_x11_error_trap_push (x11_display);
          XDeleteProperty (x11_display->xdisplay,
                           window->xwindow,
                           x11_display->atom__NET_WM_FULLSCREEN_MONITORS);
          meta_x11_error_trap_pop (x11_display);
        }
    }

  /* Edge constraints */
  update_gtk_edge_constraints (window);
}

static cairo_region_t *
region_create_from_x_rectangles (const XRectangle *rects,
                                 int n_rects)
{
  int i;
  cairo_rectangle_int_t *cairo_rects = g_newa (cairo_rectangle_int_t, n_rects);

  for (i = 0; i < n_rects; i ++)
    {
      cairo_rects[i].x = rects[i].x;
      cairo_rects[i].y = rects[i].y;
      cairo_rects[i].width = rects[i].width;
      cairo_rects[i].height = rects[i].height;
    }

  return cairo_region_create_rectangles (cairo_rects, n_rects);
}

static void
meta_window_set_input_region (MetaWindow     *window,
                              cairo_region_t *region)
{
  if (cairo_region_equal (window->input_region, region))
    return;

  g_clear_pointer (&window->input_region, cairo_region_destroy);

  if (region != NULL)
    window->input_region = cairo_region_reference (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}

#if 0
/* Print out a region; useful for debugging */
static void
print_region (cairo_region_t *region)
{
  int n_rects;
  int i;

  n_rects = cairo_region_num_rectangles (region);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle (region, i, &rect);
      g_print ("+%d+%dx%dx%d ",
               rect.x, rect.y, rect.width, rect.height);
    }
  g_print ("]\n");
}
#endif

void
meta_window_x11_update_input_region (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  cairo_region_t *region = NULL;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  /* Decorated windows don't have an input region, because
     we don't shape the frame to match the client windows
     (so the events are blocked by the frame anyway)
  */
  if (window->decorated)
    {
      if (window->input_region)
        meta_window_set_input_region (window, NULL);
      return;
    }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a cairo_region. */
      XRectangle *rects = NULL;
      int n_rects = -1, ordering;

      meta_x11_error_trap_push (x11_display);
      rects = XShapeGetRectangles (x11_display->xdisplay,
                                   window->xwindow,
                                   ShapeInput,
                                   &n_rects,
                                   &ordering);
      meta_x11_error_trap_pop (x11_display);

      /* XXX: The X Shape specification is quite unfortunately specified.
       *
       * By default, the window has a shape the same as its bounding region,
       * which we consider "NULL".
       *
       * If the window sets an empty region, then we'll get n_rects as 0
       * and rects as NULL, which we need to transform back into an empty
       * region.
       *
       * It would be great to have a less-broken extension for this, but
       * hey, it's X11!
       */

      if (n_rects == -1)
        {
          /* We had an error. */
          region = NULL;
        }
      else if (n_rects == 0)
        {
          /* Client set an empty region. */
          region = cairo_region_create ();
        }
      else if (n_rects == 1 &&
               (rects[0].x == 0 &&
                rects[0].y == 0 &&
                rects[0].width == priv->client_rect.width &&
                rects[0].height == priv->client_rect.height))
        {
          /* This is the bounding region case. Keep the
           * region as NULL. */
          region = NULL;
        }
      else
        {
          /* Window has a custom shape. */
          region = region_create_from_x_rectangles (rects, n_rects);
        }

      meta_XFree (rects);
    }

  if (region != NULL)
    {
      cairo_rectangle_int_t client_area;

      client_area.x = 0;
      client_area.y = 0;
      client_area.width = priv->client_rect.width;
      client_area.height = priv->client_rect.height;

      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped. In our case,
       * this is simply the client area.
       */
      cairo_region_intersect_rectangle (region, &client_area);
    }

  meta_window_set_input_region (window, region);
  cairo_region_destroy (region);
}

static void
meta_window_set_shape_region (MetaWindow     *window,
                              cairo_region_t *region)
{
  if (cairo_region_equal (window->shape_region, region))
    return;

  g_clear_pointer (&window->shape_region, cairo_region_destroy);

  if (region != NULL)
    window->shape_region = cairo_region_reference (region);

  meta_compositor_window_shape_changed (window->display->compositor, window);
}

void
meta_window_x11_update_shape_region (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  cairo_region_t *region = NULL;

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a cairo_region. */
      XRectangle *rects = NULL;
      int n_rects, ordering;

      int x_bounding, y_bounding, x_clip, y_clip;
      unsigned w_bounding, h_bounding, w_clip, h_clip;
      int bounding_shaped, clip_shaped;

      meta_x11_error_trap_push (x11_display);
      XShapeQueryExtents (x11_display->xdisplay, window->xwindow,
                          &bounding_shaped, &x_bounding, &y_bounding,
                          &w_bounding, &h_bounding,
                          &clip_shaped, &x_clip, &y_clip,
                          &w_clip, &h_clip);

      if (bounding_shaped)
        {
          rects = XShapeGetRectangles (x11_display->xdisplay,
                                       window->xwindow,
                                       ShapeBounding,
                                       &n_rects,
                                       &ordering);
        }
      meta_x11_error_trap_pop (x11_display);

      if (rects)
        {
          region = region_create_from_x_rectangles (rects, n_rects);
          XFree (rects);
        }
    }

  if (region != NULL)
    {
      cairo_rectangle_int_t client_area;

      client_area.x = 0;
      client_area.y = 0;
      client_area.width = priv->client_rect.width;
      client_area.height = priv->client_rect.height;

      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped. In our case,
       * this is simply the client area.
       */
      cairo_region_intersect_rectangle (region, &client_area);
      /* Some applications might explicitly set their bounding region
       * to the client area. Detect these cases, and throw out the
       * bounding region in this case for decorated windows. */
      if (window->decorated &&
          cairo_region_contains_rectangle (region, &client_area) == CAIRO_REGION_OVERLAP_IN)
        g_clear_pointer (&region, cairo_region_destroy);
    }

  meta_window_set_shape_region (window, region);
  cairo_region_destroy (region);
}

/* Generally meta_window_same_application() is a better idea
 * of "sameness", since it handles the case where multiple apps
 * want to look like the same app or the same app wants to look
 * like multiple apps, but in the case of workarounds for legacy
 * applications (which likely aren't setting the group properly
 * anyways), it may be desirable to check this as well.
 */
static gboolean
meta_window_same_client (MetaWindow *window,
                         MetaWindow *other_window)
{
  int resource_mask = window->display->x11_display->xdisplay->resource_mask;

  return ((window->xwindow & ~resource_mask) ==
          (other_window->xwindow & ~resource_mask));
}

static void
meta_window_move_resize_request (MetaWindow  *window,
                                 guint        value_mask,
                                 MetaGravity  gravity,
                                 int          new_x,
                                 int          new_y,
                                 int          new_width,
                                 int          new_height)
{
  int x, y, width, height;
  gboolean allow_position_change;
  gboolean in_grab_op;
  MetaMoveResizeFlags flags;

  /* We ignore configure requests while the user is moving/resizing
   * the window, since these represent the app sucking and fighting
   * the user, most likely due to a bug in the app (e.g. pfaedit
   * seemed to do this)
   *
   * Still have to do the ConfigureNotify and all, but pretend the
   * app asked for the current size/position instead of the new one.
   */
  in_grab_op = (window->display->grab_window == window &&
                meta_grab_op_is_mouse (window->display->grab_op));

  /* it's essential to use only the explicitly-set fields,
   * and otherwise use our current up-to-date position.
   *
   * Otherwise you get spurious position changes when the app changes
   * size, for example, if window->rect is not in sync with the
   * server-side position in effect when the configure request was
   * generated.
   */
  meta_window_get_gravity_position (window,
                                    gravity,
                                    &x, &y);

  allow_position_change = FALSE;

  if (meta_prefs_get_disable_workarounds ())
    {
      if (window->type == META_WINDOW_DIALOG ||
          window->type == META_WINDOW_MODAL_DIALOG ||
          window->type == META_WINDOW_SPLASHSCREEN)
        ; /* No position change for these */
      else if ((window->size_hints.flags & PPosition) ||
               /* USPosition is just stale if window is placed;
                * no --geometry involved here.
                */
               ((window->size_hints.flags & USPosition) &&
                !window->placed))
        allow_position_change = TRUE;
    }
  else
    {
      allow_position_change = TRUE;
    }

  if (in_grab_op)
    allow_position_change = FALSE;

  if (allow_position_change)
    {
      if (value_mask & CWX)
        x = new_x;
      if (value_mask & CWY)
        y = new_y;
      if (value_mask & (CWX | CWY))
        {
          /* Once manually positioned, windows shouldn't be placed
           * by the window manager.
           */
          window->placed = TRUE;
        }
    }
  else
    {
      meta_topic (META_DEBUG_GEOMETRY,
		  "Not allowing position change for window %s PPosition 0x%lx USPosition 0x%lx type %u\n",
		  window->desc, window->size_hints.flags & PPosition,
		  window->size_hints.flags & USPosition,
		  window->type);
    }

  width = window->rect.width;
  height = window->rect.height;
  if (!in_grab_op)
    {
      if (value_mask & CWWidth)
        width = new_width;

      if (value_mask & CWHeight)
        height = new_height;
    }

  /* ICCCM 4.1.5 */

  /* We're ignoring the value_mask here, since sizes
   * not in the mask will be the current window geometry.
   */
  window->size_hints.x = x;
  window->size_hints.y = y;
  window->size_hints.width = width;
  window->size_hints.height = height;

  /* NOTE: We consider ConfigureRequests to be "user" actions in one
   * way, but not in another.  Explanation of the two cases are in the
   * next two big comments.
   */

  /* The constraints code allows user actions to move windows
   * offscreen, etc., and configure request actions would often send
   * windows offscreen when users don't want it if not constrained
   * (e.g. hitting a dropdown triangle in a fileselector to show more
   * options, which makes the window bigger).  Thus we do not set
   * META_MOVE_RESIZE_USER_ACTION in flags to the
   * meta_window_move_resize_internal() call.
   */
  flags = META_MOVE_RESIZE_CONFIGURE_REQUEST;
  if (value_mask & (CWX | CWY))
    flags |= META_MOVE_RESIZE_MOVE_ACTION;
  if (value_mask & (CWWidth | CWHeight))
    flags |= META_MOVE_RESIZE_RESIZE_ACTION;

  if (flags & (META_MOVE_RESIZE_MOVE_ACTION | META_MOVE_RESIZE_RESIZE_ACTION))
    {
      MetaRectangle rect;

      rect.x = x;
      rect.y = y;
      rect.width = width;
      rect.height = height;

      if (window->monitor)
        {
          MetaRectangle monitor_rect;

          meta_display_get_monitor_geometry (window->display,
                                             window->monitor->number,
                                             &monitor_rect);

          /* Workaround braindead legacy apps that don't know how to
           * fullscreen themselves properly - don't get fooled by
           * windows which hide their titlebar when maximized or which are
           * client decorated; that's not the same as fullscreen, even
           * if there are no struts making the workarea smaller than
           * the monitor.
           */
          if (meta_prefs_get_force_fullscreen() &&
              (window->decorated || !meta_window_is_client_decorated (window)) &&
              meta_rectangle_equal (&rect, &monitor_rect) &&
              window->has_fullscreen_func &&
              !window->fullscreen)
            {
              /*
              meta_topic (META_DEBUG_GEOMETRY,
              */
              meta_warning (
                           "Treating resize request of legacy application %s as a "
                           "fullscreen request\n",
                           window->desc);
              meta_window_make_fullscreen_internal (window);
            }
        }

      adjust_for_gravity (window, TRUE, gravity, &rect);
      meta_window_client_rect_to_frame_rect (window, &rect, &rect);
      meta_window_move_resize_internal (window, flags, gravity, rect);
    }
}

static void
restack_window (MetaWindow *window,
                MetaWindow *sibling,
                int         direction)
{
 switch (direction)
   {
   case Above:
     if (sibling)
       meta_window_stack_just_above (window, sibling);
     else
       meta_window_raise (window);
     break;
   case Below:
     if (sibling)
       meta_window_stack_just_below (window, sibling);
     else
       meta_window_lower (window);
     break;
   case TopIf:
   case BottomIf:
   case Opposite:
     break;
   }
}

gboolean
meta_window_x11_configure_request (MetaWindow *window,
                                   XEvent     *event)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  /* Note that x, y is the corner of the window border,
   * and width, height is the size of the window inside
   * its border, but that we always deny border requests
   * and give windows a border of 0. But we save the
   * requested border here.
   */
  if (event->xconfigurerequest.value_mask & CWBorderWidth)
    priv->border_width = event->xconfigurerequest.border_width;

  meta_window_move_resize_request(window,
                                  event->xconfigurerequest.value_mask,
                                  window->size_hints.win_gravity,
                                  event->xconfigurerequest.x,
                                  event->xconfigurerequest.y,
                                  event->xconfigurerequest.width,
                                  event->xconfigurerequest.height);

  /* Handle stacking. We only handle raises/lowers, mostly because
   * stack.c really can't deal with anything else.  I guess we'll fix
   * that if a client turns up that really requires it. Only a very
   * few clients even require the raise/lower (and in fact all client
   * attempts to deal with stacking order are essentially broken,
   * since they have no idea what other clients are involved or how
   * the stack looks).
   *
   * I'm pretty sure no interesting client uses TopIf, BottomIf, or
   * Opposite anyway.
   */
  if (event->xconfigurerequest.value_mask & CWStackMode)
    {
      MetaWindow *active_window;
      active_window = window->display->focus_window;
      if (meta_prefs_get_disable_workarounds ())
        {
          meta_topic (META_DEBUG_STACK,
                      "%s sent an xconfigure stacking request; this is "
                      "broken behavior and the request is being ignored.\n",
                      window->desc);
        }
      else if (active_window &&
               !meta_window_same_application (window, active_window) &&
               !meta_window_same_client (window, active_window) &&
               XSERVER_TIME_IS_BEFORE (window->net_wm_user_time,
                                       active_window->net_wm_user_time))
        {
          meta_topic (META_DEBUG_STACK,
                      "Ignoring xconfigure stacking request from %s (with "
                      "user_time %u); currently active application is %s (with "
                      "user_time %u).\n",
                      window->desc,
                      window->net_wm_user_time,
                      active_window->desc,
                      active_window->net_wm_user_time);
          if (event->xconfigurerequest.detail == Above)
            meta_window_set_demands_attention(window);
        }
      else
        {
          MetaWindow *sibling = NULL;
          /* Handle Above/Below with a sibling set */
          if (event->xconfigurerequest.above != None)
            {
              MetaDisplay *display;

              display = meta_window_get_display (window);
              sibling = meta_x11_display_lookup_x_window (display->x11_display,
                                                          event->xconfigurerequest.above);
              if (sibling == NULL)
                return TRUE;

              meta_topic (META_DEBUG_STACK,
                      "xconfigure stacking request from window %s sibling %s stackmode %d\n",
                      window->desc, sibling->desc, event->xconfigurerequest.detail);
            }
          restack_window (window, sibling, event->xconfigurerequest.detail);
        }
    }

  return TRUE;
}

static gboolean
process_property_notify (MetaWindow     *window,
                         XPropertyEvent *event)
{
  Window xid = window->xwindow;

  if (meta_is_verbose ()) /* avoid looking up the name if we don't have to */
    {
      char *property_name = XGetAtomName (window->display->x11_display->xdisplay,
                                          event->atom);

      meta_verbose ("Property notify on %s for %s\n",
                    window->desc, property_name);
      XFree (property_name);
    }

  if (event->atom == window->display->x11_display->atom__NET_WM_USER_TIME &&
      window->user_time_window)
    {
        xid = window->user_time_window;
    }

  meta_window_reload_property_from_xwindow (window, xid, event->atom, FALSE);

  return TRUE;
}

gboolean
meta_window_x11_property_notify (MetaWindow *window,
                                 XEvent     *event)
{
  return process_property_notify (window, &event->xproperty);
}

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10
#define _NET_WM_MOVERESIZE_CANCEL           11

static int
query_pressed_buttons (MetaWindow *window)
{
  MetaCursorTracker *tracker = meta_cursor_tracker_get_for_display (window->display);
  ClutterModifierType mods;
  int button = 0;

  meta_cursor_tracker_get_pointer (tracker, NULL, NULL, &mods);

  if (mods & CLUTTER_BUTTON1_MASK)
    button |= 1 << 1;
  if (mods & CLUTTER_BUTTON2_MASK)
    button |= 1 << 2;
  if (mods & CLUTTER_BUTTON3_MASK)
    button |= 1 << 3;

  return button;
}

static void
handle_net_restack_window (MetaDisplay *display,
                           XEvent      *event)
{
  MetaWindow *window, *sibling = NULL;

  /* Ignore if this does not come from a pager, see the WM spec
   */
  if (event->xclient.data.l[0] != 2)
    return;

  window = meta_x11_display_lookup_x_window (display->x11_display,
                                             event->xclient.window);

  if (window)
    {
      if (event->xclient.data.l[1])
        sibling = meta_x11_display_lookup_x_window (display->x11_display,
                                                    event->xclient.data.l[1]);

      restack_window (window, sibling, event->xclient.data.l[2]);
    }
}

gboolean
meta_window_x11_client_message (MetaWindow *window,
                                XEvent     *event)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaDisplay *display;

  display = window->display;

  if (window->override_redirect)
    {
      /* Don't warn here: we could warn on any of the messages below,
       * but we might also receive other client messages that are
       * part of protocols we don't know anything about. So, silently
       * ignoring is simplest.
       */
      return FALSE;
    }

  if (event->xclient.message_type ==
      x11_display->atom__NET_CLOSE_WINDOW)
    {
      guint32 timestamp;

      if (event->xclient.data.l[0] != 0)
	timestamp = event->xclient.data.l[0];
      else
        {
          meta_warning ("Receiving a NET_CLOSE_WINDOW message for %s without "
                        "a timestamp!  This means some buggy (outdated) "
                        "application is on the loose!\n",
                        window->desc);
          timestamp = meta_display_get_current_time (window->display);
        }

      meta_window_delete (window, timestamp);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_DESKTOP)
    {
      int space;
      MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
      MetaWorkspace *workspace;

      space = event->xclient.data.l[0];

      meta_verbose ("Request to move %s to workspace %d\n",
                    window->desc, space);

      workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       space);

      if (workspace)
        meta_window_change_workspace (window, workspace);
      else if (space == (int) 0xFFFFFFFF)
        meta_window_stick (window);
      else
        meta_verbose ("No such workspace %d for screen\n", space);

      meta_verbose ("Window %s now on_all_workspaces = %d\n",
                    window->desc, window->on_all_workspaces);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_STATE)
    {
      gulong action;
      Atom first;
      Atom second;

      action = event->xclient.data.l[0];
      first = event->xclient.data.l[1];
      second = event->xclient.data.l[2];

      if (meta_is_verbose ())
        {
          char *str1;
          char *str2;

          meta_x11_error_trap_push (x11_display);
          str1 = XGetAtomName (x11_display->xdisplay, first);
          if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
            str1 = NULL;

          meta_x11_error_trap_push (x11_display);
          str2 = XGetAtomName (x11_display->xdisplay, second);
          if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
            str2 = NULL;

          meta_verbose ("Request to change _NET_WM_STATE action %lu atom1: %s atom2: %s\n",
                        action,
                        str1 ? str1 : "(unknown)",
                        str2 ? str2 : "(unknown)");

          meta_XFree (str1);
          meta_XFree (str2);
        }

      if (first == x11_display->atom__NET_WM_STATE_SHADED ||
          second == x11_display->atom__NET_WM_STATE_SHADED)
        {
          gboolean shade;
          guint32 timestamp;

          /* Stupid protocol has no timestamp; of course, shading
           * sucks anyway so who really cares that we're forced to do
           * a roundtrip here?
           */
          timestamp = meta_display_get_current_time_roundtrip (window->display);

          shade = (action == _NET_WM_STATE_ADD ||
                   (action == _NET_WM_STATE_TOGGLE && !window->shaded));
          if (shade && window->has_shade_func)
            meta_window_shade (window, timestamp);
          else
            meta_window_unshade (window, timestamp);
        }

      if (first == x11_display->atom__NET_WM_STATE_FULLSCREEN ||
          second == x11_display->atom__NET_WM_STATE_FULLSCREEN)
        {
          gboolean make_fullscreen;

          make_fullscreen = (action == _NET_WM_STATE_ADD ||
                             (action == _NET_WM_STATE_TOGGLE && !window->fullscreen));
          if (make_fullscreen && window->has_fullscreen_func)
            meta_window_make_fullscreen (window);
          else
            meta_window_unmake_fullscreen (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
          second == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
          first == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT ||
          second == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT)
        {
          gboolean max;
          MetaMaximizeFlags directions = 0;

          max = (action == _NET_WM_STATE_ADD ||
                 (action == _NET_WM_STATE_TOGGLE &&
                  !window->maximized_horizontally));

          if (first == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ ||
              second == x11_display->atom__NET_WM_STATE_MAXIMIZED_HORZ)
            directions |= META_MAXIMIZE_HORIZONTAL;

          if (first == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT ||
              second == x11_display->atom__NET_WM_STATE_MAXIMIZED_VERT)
            directions |= META_MAXIMIZE_VERTICAL;

          if (max && window->has_maximize_func)
            {
              if (meta_prefs_get_raise_on_click ())
                meta_window_raise (window);
              meta_window_maximize (window, directions);
            }
          else
            {
              if (meta_prefs_get_raise_on_click ())
                meta_window_raise (window);
              meta_window_unmaximize (window, directions);
            }
        }

      if (first == x11_display->atom__NET_WM_STATE_MODAL ||
          second == x11_display->atom__NET_WM_STATE_MODAL)
        {
          priv->wm_state_modal =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !priv->wm_state_modal);

          meta_window_x11_recalc_window_type (window);
          meta_window_queue(window, META_QUEUE_MOVE_RESIZE);
        }

      if (first == x11_display->atom__NET_WM_STATE_SKIP_PAGER ||
          second == x11_display->atom__NET_WM_STATE_SKIP_PAGER)
        {
          priv->wm_state_skip_pager =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->skip_pager);

          meta_window_recalc_features (window);
          meta_window_x11_set_net_wm_state (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_SKIP_TASKBAR ||
          second == x11_display->atom__NET_WM_STATE_SKIP_TASKBAR)
        {
          priv->wm_state_skip_taskbar =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->skip_taskbar);

          meta_window_recalc_features (window);
          meta_window_x11_set_net_wm_state (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_ABOVE ||
          second == x11_display->atom__NET_WM_STATE_ABOVE)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->wm_state_demands_attention))
            meta_window_make_above (window);
          else
            meta_window_unmake_above (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_BELOW ||
          second == x11_display->atom__NET_WM_STATE_BELOW)
        {
          window->wm_state_below =
            (action == _NET_WM_STATE_ADD) ||
            (action == _NET_WM_STATE_TOGGLE && !window->wm_state_below);

          meta_window_update_layer (window);
          meta_window_x11_set_net_wm_state (window);
        }

      if (first == x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION ||
          second == x11_display->atom__NET_WM_STATE_DEMANDS_ATTENTION)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->wm_state_demands_attention))
            meta_window_set_demands_attention (window);
          else
            meta_window_unset_demands_attention (window);
        }

       if (first == x11_display->atom__NET_WM_STATE_STICKY ||
          second == x11_display->atom__NET_WM_STATE_STICKY)
        {
          if ((action == _NET_WM_STATE_ADD) ||
              (action == _NET_WM_STATE_TOGGLE && !window->on_all_workspaces_requested))
            meta_window_stick (window);
          else
            meta_window_unstick (window);
        }

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom_WM_CHANGE_STATE)
    {
      meta_verbose ("WM_CHANGE_STATE client message, state: %ld\n",
                    event->xclient.data.l[0]);
      if (event->xclient.data.l[0] == IconicState)
        meta_window_minimize (window);

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_MOVERESIZE)
    {
      int x_root;
      int y_root;
      int action;
      MetaGrabOp op;
      int button;
      guint32 timestamp;

      /* _NET_WM_MOVERESIZE messages are almost certainly going to come from
       * clients when users click on the fake "frame" that the client has,
       * thus we should also treat such messages as though it were a
       * "frame action".
       */
      gboolean const frame_action = TRUE;

      x_root = event->xclient.data.l[0];
      y_root = event->xclient.data.l[1];
      action = event->xclient.data.l[2];
      button = event->xclient.data.l[3];

      /* FIXME: What a braindead protocol; no timestamp?!? */
      timestamp = meta_display_get_current_time_roundtrip (display);
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Received _NET_WM_MOVERESIZE message on %s, %d,%d action = %d, button %d\n",
                  window->desc,
                  x_root, y_root, action, button);

      op = META_GRAB_OP_NONE;
      switch (action)
        {
        case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
          op = META_GRAB_OP_RESIZING_NW;
          break;
        case _NET_WM_MOVERESIZE_SIZE_TOP:
          op = META_GRAB_OP_RESIZING_N;
          break;
        case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
          op = META_GRAB_OP_RESIZING_NE;
          break;
        case _NET_WM_MOVERESIZE_SIZE_RIGHT:
          op = META_GRAB_OP_RESIZING_E;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
          op = META_GRAB_OP_RESIZING_SE;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
          op = META_GRAB_OP_RESIZING_S;
          break;
        case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
          op = META_GRAB_OP_RESIZING_SW;
          break;
        case _NET_WM_MOVERESIZE_SIZE_LEFT:
          op = META_GRAB_OP_RESIZING_W;
          break;
        case _NET_WM_MOVERESIZE_MOVE:
          op = META_GRAB_OP_MOVING;
          break;
        case _NET_WM_MOVERESIZE_SIZE_KEYBOARD:
          op = META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN;
          break;
        case _NET_WM_MOVERESIZE_MOVE_KEYBOARD:
          op = META_GRAB_OP_KEYBOARD_MOVING;
          break;
        case _NET_WM_MOVERESIZE_CANCEL:
          /* handled below */
          break;
        default:
          break;
        }

      if (action == _NET_WM_MOVERESIZE_CANCEL)
        {
          meta_display_end_grab_op (window->display, timestamp);
        }
      else if (op != META_GRAB_OP_NONE &&
          ((window->has_move_func && op == META_GRAB_OP_KEYBOARD_MOVING) ||
           (window->has_resize_func && op == META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN)))
        {
          meta_window_begin_grab_op (window, op, frame_action, timestamp);
        }
      else if (op != META_GRAB_OP_NONE &&
               ((window->has_move_func && op == META_GRAB_OP_MOVING) ||
               (window->has_resize_func &&
                (op != META_GRAB_OP_MOVING &&
                 op != META_GRAB_OP_KEYBOARD_MOVING))))
        {
          int button_mask;

          meta_topic (META_DEBUG_WINDOW_OPS,
                      "Beginning move/resize with button = %d\n", button);
          meta_display_begin_grab_op (window->display,
                                      window,
                                      op,
                                      FALSE,
                                      frame_action,
                                      button, 0,
                                      timestamp,
                                      x_root,
                                      y_root);

          button_mask = query_pressed_buttons (window);

          if (button == 0)
            {
              /*
               * the button SHOULD already be included in the message
               */
              if ((button_mask & (1 << 1)) != 0)
                button = 1;
              else if ((button_mask & (1 << 2)) != 0)
                button = 2;
              else if ((button_mask & (1 << 3)) != 0)
                button = 3;

              if (button != 0)
                window->display->grab_button = button;
              else
                meta_display_end_grab_op (window->display,
                                          timestamp);
            }
          else
            {
              /* There is a potential race here. If the user presses and
               * releases their mouse button very fast, it's possible for
               * both the ButtonPress and ButtonRelease to be sent to the
               * client before it can get a chance to send _NET_WM_MOVERESIZE
               * to us. When that happens, we'll become stuck in a grab
               * state, as we haven't received a ButtonRelease to cancel the
               * grab.
               *
               * We can solve this by querying after we take the explicit
               * pointer grab -- if the button isn't pressed, we cancel the
               * drag immediately.
               */

              if ((button_mask & (1 << button)) == 0)
                meta_display_end_grab_op (window->display, timestamp);
            }
        }

      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_MOVERESIZE_WINDOW)
    {
      MetaGravity gravity;
      guint value_mask;

      gravity = (MetaGravity) (event->xclient.data.l[0] & 0xff);
      value_mask = (event->xclient.data.l[0] & 0xf00) >> 8;
      /* source = (event->xclient.data.l[0] & 0xf000) >> 12; */

      if (gravity == 0)
        gravity = window->size_hints.win_gravity;

      meta_window_move_resize_request(window,
                                      value_mask,
                                      gravity,
                                      event->xclient.data.l[1],  /* x */
                                      event->xclient.data.l[2],  /* y */
                                      event->xclient.data.l[3],  /* width */
                                      event->xclient.data.l[4]); /* height */
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_ACTIVE_WINDOW)
    {
      MetaClientType source_indication;
      guint32        timestamp;

      meta_verbose ("_NET_ACTIVE_WINDOW request for window '%s', activating\n",
                    window->desc);

      source_indication = event->xclient.data.l[0];
      timestamp = event->xclient.data.l[1];

      if (source_indication > META_CLIENT_TYPE_MAX_RECOGNIZED)
        source_indication = META_CLIENT_TYPE_UNKNOWN;

      if (timestamp == 0)
        {
          /* Client using older EWMH _NET_ACTIVE_WINDOW without a timestamp */
          meta_warning ("Buggy client sent a _NET_ACTIVE_WINDOW message with a "
                        "timestamp of 0 for %s\n",
                        window->desc);
          timestamp = meta_display_get_current_time (display);
        }

      meta_window_activate_full (window, timestamp, source_indication, NULL);
      return TRUE;
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_WM_FULLSCREEN_MONITORS)
    {
      MetaLogicalMonitor *top, *bottom, *left, *right;

      meta_verbose ("_NET_WM_FULLSCREEN_MONITORS request for window '%s'\n",
                    window->desc);

      top =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[0]);
      bottom =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[1]);
      left =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[2]);
      right =
        meta_x11_display_xinerama_index_to_logical_monitor (window->display->x11_display,
                                                            event->xclient.data.l[3]);
      /* source_indication = event->xclient.data.l[4]; */

      meta_window_update_fullscreen_monitors (window, top, bottom, left, right);
    }
  else if (event->xclient.message_type ==
           x11_display->atom__GTK_SHOW_WINDOW_MENU)
    {
      gulong x, y;

      /* l[0] is device_id, which we don't use */
      x = event->xclient.data.l[1];
      y = event->xclient.data.l[2];

      meta_window_show_menu (window, META_WINDOW_MENU_WM, x, y);
    }
  else if (event->xclient.message_type ==
           x11_display->atom__NET_RESTACK_WINDOW)
    {
      handle_net_restack_window (display, event);
    }

  return FALSE;
}

static void
set_wm_state_on_xwindow (MetaDisplay *display,
                         Window       xwindow,
                         int          state)
{
  unsigned long data[2];

  /* Mutter doesn't use icon windows, so data[1] should be None
   * according to the ICCCM 2.0 Section 4.1.3.1.
   */
  data[0] = state;
  data[1] = None;

  meta_x11_error_trap_push (display->x11_display);
  XChangeProperty (display->x11_display->xdisplay, xwindow,
                   display->x11_display->atom_WM_STATE,
                   display->x11_display->atom_WM_STATE,
                   32, PropModeReplace, (guchar*) data, 2);
  meta_x11_error_trap_pop (display->x11_display);
}

void
meta_window_x11_set_wm_state (MetaWindow *window)
{
  int state;

  if (window->withdrawn)
    state = WithdrawnState;
  else if (window->iconic)
    state = IconicState;
  else
    state = NormalState;

  set_wm_state_on_xwindow (window->display, window->xwindow, state);
}

/* The MUTTER_WM_CLASS_FILTER environment variable is designed for
 * performance and regression testing environments where we want to do
 * tests with only a limited set of windows and ignore all other windows
 *
 * When it is set to a comma separated list of WM_CLASS class names, all
 * windows not matching the list will be ignored.
 *
 * Returns TRUE if window has been filtered out and should be ignored.
 */
static gboolean
maybe_filter_xwindow (MetaDisplay       *display,
                      Window             xwindow,
                      gboolean           must_be_viewable,
                      XWindowAttributes *attrs)
{
  static char **filter_wm_classes = NULL;
  static gboolean initialized = FALSE;
  XClassHint class_hint;
  gboolean filtered;
  Status success;
  int i;

  if (!initialized)
    {
      const char *filter_string = g_getenv ("MUTTER_WM_CLASS_FILTER");
      if (filter_string)
        filter_wm_classes = g_strsplit (filter_string, ",", -1);
      initialized = TRUE;
    }

  if (!filter_wm_classes || !filter_wm_classes[0])
    return FALSE;

  filtered = TRUE;

  meta_x11_error_trap_push (display->x11_display);
  success = XGetClassHint (display->x11_display->xdisplay,
                           xwindow, &class_hint);

  if (success)
    {
      for (i = 0; filter_wm_classes[i]; i++)
        {
          if (strcmp (class_hint.res_class, filter_wm_classes[i]) == 0)
            {
              filtered = FALSE;
              break;
            }
        }

      XFree (class_hint.res_name);
      XFree (class_hint.res_class);
    }

  if (filtered)
    {
      /* We want to try and get the window managed by the next WM that come along,
       * so we need to make sure that windows that are requested to be mapped while
       * Mutter is running (!must_be_viewable), or windows already viewable at startup
       * get a non-withdrawn WM_STATE property. Previously unmapped windows are left
       * with whatever WM_STATE property they had.
       */
      if (!must_be_viewable || attrs->map_state == IsViewable)
        {
          uint32_t old_state;

          if (!meta_prop_get_cardinal_with_atom_type (display->x11_display, xwindow,
                                                      display->x11_display->atom_WM_STATE,
                                                      display->x11_display->atom_WM_STATE,
                                                      &old_state))
            old_state = WithdrawnState;

          if (old_state == WithdrawnState)
            set_wm_state_on_xwindow (display, xwindow, NormalState);
        }

      /* Make sure filtered windows are hidden from view */
      XUnmapWindow (display->x11_display->xdisplay, xwindow);
    }

  meta_x11_error_trap_pop (display->x11_display);

  return filtered;
}

static gboolean
is_our_xwindow (MetaX11Display    *x11_display,
                Window             xwindow,
                XWindowAttributes *attrs)
{
  if (xwindow == x11_display->no_focus_window)
    return TRUE;

  if (xwindow == x11_display->wm_sn_selection_window)
    return TRUE;

  if (xwindow == x11_display->wm_cm_selection_window)
    return TRUE;

  if (xwindow == x11_display->guard_window)
    return TRUE;

  if (xwindow == x11_display->composite_overlay_window)
    return TRUE;

  {
    MetaBackend *backend = meta_get_backend ();

    if (META_IS_BACKEND_X11 (backend))
      {
        if (xwindow == meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend)))
          return TRUE;
      }
  }

  /* Any windows created via meta_create_offscreen_window */
  if (attrs->override_redirect && attrs->x == -100 && attrs->y == -100 && attrs->width == 1 && attrs->height == 1)
    return TRUE;

  return FALSE;
}

#ifdef WITH_VERBOSE_MODE
static const char*
wm_state_to_string (int state)
{
  switch (state)
    {
    case NormalState:
      return "NormalState";
    case IconicState:
      return "IconicState";
    case WithdrawnState:
      return "WithdrawnState";
    }

  return "Unknown";
}
#endif

MetaWindow *
meta_window_x11_new (MetaDisplay       *display,
                     Window             xwindow,
                     gboolean           must_be_viewable,
                     MetaCompEffect     effect)
{
  MetaX11Display *x11_display = display->x11_display;
  XWindowAttributes attrs;
  gulong existing_wm_state;
  MetaWindow *window = NULL;
  gulong event_mask;

  meta_verbose ("Attempting to manage 0x%lx\n", xwindow);

  if (meta_x11_display_xwindow_is_a_no_focus_window (x11_display, xwindow))
    {
      meta_verbose ("Not managing no_focus_window 0x%lx\n",
                    xwindow);
      return NULL;
    }

  meta_x11_error_trap_push (x11_display); /* Push a trap over all of window
                                       * creation, to reduce XSync() calls
                                       */
  /*
   * This function executes without any server grabs held. This means that
   * the window could have already gone away, or could go away at any point,
   * so we must be careful with X error handling.
   */

  if (!XGetWindowAttributes (x11_display->xdisplay, xwindow, &attrs))
    {
      meta_verbose ("Failed to get attributes for window 0x%lx\n",
                    xwindow);
      goto error;
    }

  if (attrs.root != x11_display->xroot)
    {
      meta_verbose ("Not on our screen\n");
      goto error;
    }

  if (attrs.class == InputOnly)
    {
      meta_verbose ("Not managing InputOnly windows\n");
      goto error;
    }

  if (is_our_xwindow (x11_display, xwindow, &attrs))
    {
      meta_verbose ("Not managing our own windows\n");
      goto error;
    }

  if (maybe_filter_xwindow (display, xwindow, must_be_viewable, &attrs))
    {
      meta_verbose ("Not managing filtered window\n");
      goto error;
    }

  existing_wm_state = WithdrawnState;
  if (must_be_viewable && attrs.map_state != IsViewable)
    {
      /* Only manage if WM_STATE is IconicState or NormalState */
      uint32_t state;

      /* WM_STATE isn't a cardinal, it's type WM_STATE, but is an int */
      if (!(meta_prop_get_cardinal_with_atom_type (x11_display, xwindow,
                                                   x11_display->atom_WM_STATE,
                                                   x11_display->atom_WM_STATE,
                                                   &state) &&
            (state == IconicState || state == NormalState)))
        {
          meta_verbose ("Deciding not to manage unmapped or unviewable window 0x%lx\n", xwindow);
          goto error;
        }

      existing_wm_state = state;
      meta_verbose ("WM_STATE of %lx = %s\n", xwindow,
                    wm_state_to_string (existing_wm_state));
    }

  /*
   * XAddToSaveSet can only be called on windows created by a different
   * client.  with Mutter we want to be able to create manageable windows
   * from within the process (such as a dummy desktop window). As we do not
   * want this call failing to prevent the window from being managed, we
   * call this before creating the return-checked error trap.
   */
  XAddToSaveSet (x11_display->xdisplay, xwindow);

  meta_x11_error_trap_push (x11_display);

  event_mask = PropertyChangeMask;
  if (attrs.override_redirect)
    event_mask |= StructureNotifyMask;

  /* If the window is from this client (a menu, say) we need to augment
   * the event mask, not replace it. For windows from other clients,
   * attrs.your_event_mask will be empty at this point.
   */
  XSelectInput (x11_display->xdisplay, xwindow, attrs.your_event_mask | event_mask);

  {
    unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    XISetMask (mask.mask, XI_Enter);
    XISetMask (mask.mask, XI_Leave);
    XISetMask (mask.mask, XI_FocusIn);
    XISetMask (mask.mask, XI_FocusOut);

    XISelectEvents (x11_display->xdisplay, xwindow, &mask, 1);
  }

  if (META_X11_DISPLAY_HAS_SHAPE (x11_display))
    XShapeSelectInput (x11_display->xdisplay, xwindow, ShapeNotifyMask);

  /* Get rid of any borders */
  if (attrs.border_width != 0)
    XSetWindowBorderWidth (x11_display->xdisplay, xwindow, 0);

  /* Get rid of weird gravities */
  if (attrs.win_gravity != NorthWestGravity)
    {
      XSetWindowAttributes set_attrs;

      set_attrs.win_gravity = NorthWestGravity;

      XChangeWindowAttributes (x11_display->xdisplay,
                               xwindow,
                               CWWinGravity,
                               &set_attrs);
    }

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    {
      meta_verbose ("Window 0x%lx disappeared just as we tried to manage it\n",
                    xwindow);
      goto error;
    }

  window = _meta_window_shared_new (display,
                                    META_WINDOW_CLIENT_TYPE_X11,
                                    NULL,
                                    xwindow,
                                    existing_wm_state,
                                    effect,
                                    &attrs);

  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  priv->border_width = attrs.border_width;

  meta_window_grab_keys (window);
  if (window->type != META_WINDOW_DOCK && !window->override_redirect)
    {
      meta_display_grab_window_buttons (window->display, window->xwindow);
      meta_display_grab_focus_window_button (window->display, window);
    }

  meta_x11_error_trap_pop (x11_display); /* pop the XSync()-reducing trap */
  return window;

error:
  meta_x11_error_trap_pop (x11_display);
  return NULL;
}

void
meta_window_x11_recalc_window_type (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);
  MetaWindowType type;

  if (priv->type_atom != None)
    {
      if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DESKTOP)
        type = META_WINDOW_DESKTOP;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DOCK)
        type = META_WINDOW_DOCK;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLBAR)
        type = META_WINDOW_TOOLBAR;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_MENU)
        type = META_WINDOW_MENU;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_UTILITY)
        type = META_WINDOW_UTILITY;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_SPLASH)
        type = META_WINDOW_SPLASHSCREEN;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DIALOG)
        type = META_WINDOW_DIALOG;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_NORMAL)
        type = META_WINDOW_NORMAL;
      /* The below are *typically* override-redirect windows, but the spec does
       * not disallow using them for managed windows.
       */
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DROPDOWN_MENU)
        type = META_WINDOW_DROPDOWN_MENU;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_POPUP_MENU)
        type = META_WINDOW_POPUP_MENU;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_TOOLTIP)
        type = META_WINDOW_TOOLTIP;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_NOTIFICATION)
        type = META_WINDOW_NOTIFICATION;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_COMBO)
        type = META_WINDOW_COMBO;
      else if (priv->type_atom  == x11_display->atom__NET_WM_WINDOW_TYPE_DND)
        type = META_WINDOW_DND;
      else
        {
          char *atom_name;

          /*
           * Fallback on a normal type, and print warning. Don't abort.
           */
          type = META_WINDOW_NORMAL;

          meta_x11_error_trap_push (x11_display);
          atom_name = XGetAtomName (x11_display->xdisplay,
                                    priv->type_atom);
          meta_x11_error_trap_pop (x11_display);

          meta_warning ("Unrecognized type atom [%s] set for %s \n",
                        atom_name ? atom_name : "unknown",
                        window->desc);

          if (atom_name)
            XFree (atom_name);
        }
    }
  else if (window->transient_for != NULL)
    {
      type = META_WINDOW_DIALOG;
    }
  else
    {
      type = META_WINDOW_NORMAL;
    }

  if (type == META_WINDOW_DIALOG && priv->wm_state_modal)
    type = META_WINDOW_MODAL_DIALOG;

  /* We don't want to allow override-redirect windows to have decorated-window
   * types since that's just confusing.
   */
  if (window->override_redirect)
    {
      switch (type)
        {
        /* Decorated types */
        case META_WINDOW_NORMAL:
        case META_WINDOW_DIALOG:
        case META_WINDOW_MODAL_DIALOG:
        case META_WINDOW_MENU:
        case META_WINDOW_UTILITY:
          type = META_WINDOW_OVERRIDE_OTHER;
          break;
        /* Undecorated types, normally not override-redirect */
        case META_WINDOW_DESKTOP:
        case META_WINDOW_DOCK:
        case META_WINDOW_TOOLBAR:
        case META_WINDOW_SPLASHSCREEN:
        /* Undecorated types, normally override-redirect types */
        case META_WINDOW_DROPDOWN_MENU:
        case META_WINDOW_POPUP_MENU:
        case META_WINDOW_TOOLTIP:
        case META_WINDOW_NOTIFICATION:
        case META_WINDOW_COMBO:
        case META_WINDOW_DND:
        /* To complete enum */
        case META_WINDOW_OVERRIDE_OTHER:
          break;
        }
    }

  meta_verbose ("Calculated type %u for %s, old type %u\n",
                type, window->desc, type);
  meta_window_set_type (window, type);
}

/**
 * meta_window_x11_configure_notify: (skip)
 * @window: a #MetaWindow
 * @event: a #XConfigureEvent
 *
 * This is used to notify us of an unrequested configuration
 * (only applicable to override redirect windows)
 */
void
meta_window_x11_configure_notify (MetaWindow      *window,
                                  XConfigureEvent *event)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  g_assert (window->override_redirect);
  g_assert (window->frame == NULL);

  window->rect.x = event->x;
  window->rect.y = event->y;
  window->rect.width = event->width;
  window->rect.height = event->height;

  priv->client_rect = window->rect;
  window->buffer_rect = window->rect;

  meta_window_update_monitor (window, META_WINDOW_UPDATE_MONITOR_FLAGS_NONE);

  /* Whether an override-redirect window is considered fullscreen depends
   * on its geometry.
   */
  if (window->override_redirect)
    meta_display_queue_check_fullscreen (window->display);

  if (!event->override_redirect && !event->send_event)
    meta_warning ("Unhandled change of windows override redirect status\n");

  meta_compositor_sync_window_geometry (window->display->compositor, window, FALSE);
}

void
meta_window_x11_set_allowed_actions_hint (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
#define MAX_N_ACTIONS 12
  unsigned long data[MAX_N_ACTIONS];
  int i;

  i = 0;
  if (window->has_move_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_MOVE;
      ++i;
    }
  if (window->has_resize_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_RESIZE;
      ++i;
    }
  if (window->has_fullscreen_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_FULLSCREEN;
      ++i;
    }
  if (window->has_minimize_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_MINIMIZE;
      ++i;
    }
  if (window->has_shade_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_SHADE;
      ++i;
    }
  /* sticky according to EWMH is different from mutter's sticky;
   * mutter doesn't support EWMH sticky
   */
  if (window->has_maximize_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_MAXIMIZE_HORZ;
      ++i;
      data[i] = x11_display->atom__NET_WM_ACTION_MAXIMIZE_VERT;
      ++i;
    }
  /* We always allow this */
  data[i] = x11_display->atom__NET_WM_ACTION_CHANGE_DESKTOP;
  ++i;
  if (window->has_close_func)
    {
      data[i] = x11_display->atom__NET_WM_ACTION_CLOSE;
      ++i;
    }

  /* I guess we always allow above/below operations */
  data[i] = x11_display->atom__NET_WM_ACTION_ABOVE;
  ++i;
  data[i] = x11_display->atom__NET_WM_ACTION_BELOW;
  ++i;

  g_assert (i <= MAX_N_ACTIONS);

  meta_verbose ("Setting _NET_WM_ALLOWED_ACTIONS with %d atoms\n", i);

  meta_x11_error_trap_push (x11_display);
  XChangeProperty (x11_display->xdisplay, window->xwindow,
                   x11_display->atom__NET_WM_ALLOWED_ACTIONS,
                   XA_ATOM,
                   32, PropModeReplace, (guchar*) data, i);
  meta_x11_error_trap_pop (x11_display);
#undef MAX_N_ACTIONS
}

void
meta_window_x11_create_sync_request_alarm (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  XSyncAlarmAttributes values;
  XSyncValue init;

  if (window->sync_request_counter == None ||
      window->sync_request_alarm != None)
    return;

  meta_x11_error_trap_push (x11_display);

  /* In the new (extended style), the counter value is initialized by
   * the client before mapping the window. In the old style, we're
   * responsible for setting the initial value of the counter.
   */
  if (window->extended_sync_request_counter)
    {
      if (!XSyncQueryCounter(x11_display->xdisplay,
                             window->sync_request_counter,
                             &init))
        {
          meta_x11_error_trap_pop_with_return (x11_display);
          window->sync_request_counter = None;
          return;
        }

      window->sync_request_serial =
        XSyncValueLow32 (init) + ((gint64)XSyncValueHigh32 (init) << 32);
    }
  else
    {
      XSyncIntToValue (&init, 0);
      XSyncSetCounter (x11_display->xdisplay,
                       window->sync_request_counter, init);
      window->sync_request_serial = 0;
    }

  values.trigger.counter = window->sync_request_counter;
  values.trigger.test_type = XSyncPositiveComparison;

  /* Initialize to one greater than the current value */
  values.trigger.value_type = XSyncRelative;
  XSyncIntToValue (&values.trigger.wait_value, 1);

  /* After triggering, increment test_value by this until
   * until the test condition is false */
  XSyncIntToValue (&values.delta, 1);

  /* we want events (on by default anyway) */
  values.events = True;

  window->sync_request_alarm = XSyncCreateAlarm (x11_display->xdisplay,
                                                 XSyncCACounter |
                                                 XSyncCAValueType |
                                                 XSyncCAValue |
                                                 XSyncCATestType |
                                                 XSyncCADelta |
                                                 XSyncCAEvents,
                                                 &values);

  if (meta_x11_error_trap_pop_with_return (x11_display) == Success)
    meta_x11_display_register_sync_alarm (x11_display, &window->sync_request_alarm, window);
  else
    {
      window->sync_request_alarm = None;
      window->sync_request_counter = None;
    }
}

void
meta_window_x11_destroy_sync_request_alarm (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;

  if (window->sync_request_alarm != None)
    {
      /* Has to be unregistered _before_ clearing the structure field */
      meta_x11_display_unregister_sync_alarm (x11_display, window->sync_request_alarm);
      XSyncDestroyAlarm (x11_display->xdisplay,
                         window->sync_request_alarm);
      window->sync_request_alarm = None;
    }
}

void
meta_window_x11_update_sync_request_counter (MetaWindow *window,
                                             gint64      new_counter_value)
{
  gboolean needs_frame_drawn = FALSE;
  gboolean no_delay_frame = FALSE;

  if (window->extended_sync_request_counter && new_counter_value % 2 == 0)
    {
      needs_frame_drawn = TRUE;
      no_delay_frame = new_counter_value == window->sync_request_serial + 1;
    }

  window->sync_request_serial = new_counter_value;
  meta_compositor_sync_updates_frozen (window->display->compositor, window);

  if (new_counter_value >= window->sync_request_wait_serial &&
      window->sync_request_timeout_id)
    {

      if (!window->extended_sync_request_counter ||
          new_counter_value % 2 == 0)
        g_clear_handle_id (&window->sync_request_timeout_id, g_source_remove);

      if (window == window->display->grab_window &&
          meta_grab_op_is_resizing (window->display->grab_op) &&
          (!window->extended_sync_request_counter ||
           new_counter_value % 2 == 0))
        {
          meta_topic (META_DEBUG_RESIZING,
                      "Alarm event received last motion x = %d y = %d\n",
                      window->display->grab_latest_motion_x,
                      window->display->grab_latest_motion_y);

          /* This means we are ready for another configure;
           * no pointer round trip here, to keep in sync */
          meta_window_update_resize (window,
                                     window->display->grab_last_user_action_was_snap,
                                     window->display->grab_latest_motion_x,
                                     window->display->grab_latest_motion_y,
                                     TRUE);
        }
    }

  /* If sync was previously disabled, turn it back on and hope
   * the application has come to its senses (maybe it was just
   * busy with a pagefault or a long computation).
   */
  window->disable_sync = FALSE;

  if (needs_frame_drawn)
    meta_compositor_queue_frame_drawn (window->display->compositor, window,
                                       no_delay_frame);
}

Window
meta_window_x11_get_toplevel_xwindow (MetaWindow *window)
{
  return window->frame ? window->frame->xwindow : window->xwindow;
}

void
meta_window_x11_freeze_commits (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  META_WINDOW_X11_GET_CLASS (window_x11)->freeze_commits (window);
}

void
meta_window_x11_thaw_commits (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  META_WINDOW_X11_GET_CLASS (window_x11)->thaw_commits (window);
}

void
meta_window_x11_set_thaw_after_paint (MetaWindow *window,
                                      gboolean    thaw_after_paint)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  priv->thaw_after_paint = thaw_after_paint;
}

gboolean
meta_window_x11_should_thaw_after_paint (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);
  MetaWindowX11Private *priv = meta_window_x11_get_instance_private (window_x11);

  return priv->thaw_after_paint;
}

gboolean
meta_window_x11_always_update_shape (MetaWindow *window)
{
  MetaWindowX11 *window_x11 = META_WINDOW_X11 (window);

  return META_WINDOW_X11_GET_CLASS (window_x11)->always_update_shape (window);
}

void
meta_window_x11_surface_rect_to_frame_rect (MetaWindow    *window,
                                            MetaRectangle *surface_rect,
                                            MetaRectangle *frame_rect)

{
  MetaFrameBorders borders;

  g_return_if_fail (window->frame);

  meta_frame_calc_borders (window->frame, &borders);

  *frame_rect = *surface_rect;
  frame_rect->x += borders.invisible.left;
  frame_rect->y += borders.invisible.top;
  frame_rect->width -= borders.invisible.left + borders.invisible.right;
  frame_rect->height -= borders.invisible.top + borders.invisible.bottom;
}

void
meta_window_x11_surface_rect_to_client_rect (MetaWindow    *window,
                                             MetaRectangle *surface_rect,
                                             MetaRectangle *client_rect)
{
  MetaFrameBorders borders;

  meta_frame_calc_borders (window->frame, &borders);

  *client_rect = *surface_rect;
  client_rect->x += borders.total.left;
  client_rect->y += borders.total.top;
  client_rect->width -= borders.total.left + borders.total.right;
  client_rect->height -= borders.total.top + borders.total.bottom;
}
