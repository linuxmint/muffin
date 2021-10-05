/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter interface used by GTK+ UI to talk to core */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#include "x11/meta-x11-window-control.h"

#include "core/frame.h"
#include "core/meta-workspace-manager-private.h"
#include "core/util-private.h"
#include "core/workspace-private.h"
#include "meta/meta-x11-errors.h"
#include "meta/prefs.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11-private.h"
#include "x11/window-x11.h"

static MetaWindow *
window_from_frame (MetaX11Display *x11_display,
                   Window          frame_xwindow)
{
  MetaWindow *window;

  window = meta_x11_display_lookup_x_window (x11_display, frame_xwindow);
  if (!window || !window->frame)
    {
      meta_bug ("No such frame window 0x%lx!\n", frame_xwindow);
      return NULL;
    }

  return window;
}

void
meta_x11_wm_queue_frame_resize (MetaX11Display *x11_display,
                                Window          frame_xwindow)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);

  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
  meta_window_frame_size_changed (window);
}

static gboolean
lower_window_and_transients (MetaWindow *window,
                             gpointer   data)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  meta_window_lower (window);

  meta_window_foreach_transient (window, lower_window_and_transients, NULL);

  if (meta_prefs_get_raise_on_click ())
    {
      /* Move window to the back of the focusing workspace's MRU list.
       * Do extra sanity checks to avoid possible race conditions.
       * (Borrowed from window.c.)
       */
      if (workspace_manager->active_workspace &&
          meta_window_located_on_workspace (window,
                                            workspace_manager->active_workspace))
        {
          GList* link;
          link = g_list_find (workspace_manager->active_workspace->mru_list,
                              window);
          g_assert (link);

          workspace_manager->active_workspace->mru_list =
            g_list_remove_link (workspace_manager->active_workspace->mru_list,
                                link);
          g_list_free (link);

          workspace_manager->active_workspace->mru_list =
            g_list_append (workspace_manager->active_workspace->mru_list,
                           window);
        }
    }

  return FALSE;
}

void
meta_x11_wm_user_lower_and_unfocus (MetaX11Display *x11_display,
                                    Window          frame_xwindow,
                                    uint32_t        timestamp)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  lower_window_and_transients (window, NULL);

 /* Rather than try to figure that out whether we just lowered
  * the focus window, assume that's always the case. (Typically,
  * this will be invoked via keyboard action or by a mouse action;
  * in either case the window or a modal child will have been focused.) */
  meta_workspace_focus_default_window (workspace_manager->active_workspace,
                                       NULL,
                                       timestamp);
}

void
meta_x11_wm_toggle_maximize_vertically (MetaX11Display *x11_display,
                                        Window          frame_xwindow)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);

  if (meta_prefs_get_raise_on_click ())
    meta_window_raise (window);

  if (META_WINDOW_MAXIMIZED_VERTICALLY (window))
    meta_window_unmaximize (window, META_MAXIMIZE_VERTICAL);
  else
    meta_window_maximize (window, META_MAXIMIZE_VERTICAL);
}

void
meta_x11_wm_toggle_maximize_horizontally (MetaX11Display *x11_display,
                                          Window          frame_xwindow)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);

  if (meta_prefs_get_raise_on_click ())
    meta_window_raise (window);

  if (META_WINDOW_MAXIMIZED_HORIZONTALLY (window))
    meta_window_unmaximize (window, META_MAXIMIZE_HORIZONTAL);
  else
    meta_window_maximize (window, META_MAXIMIZE_HORIZONTAL);
}

void
meta_x11_wm_toggle_maximize (MetaX11Display *x11_display,
                             Window   frame_xwindow)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);

  if (meta_prefs_get_raise_on_click ())
    meta_window_raise (window);

  if (META_WINDOW_MAXIMIZED (window))
    meta_window_unmaximize (window, META_MAXIMIZE_BOTH);
  else
    meta_window_maximize (window, META_MAXIMIZE_BOTH);
}

void
meta_x11_wm_show_window_menu (MetaX11Display     *x11_display,
                              Window              frame_xwindow,
                              MetaWindowMenuType  menu,
                              int                 root_x,
                              int                 root_y,
                              uint32_t            timestamp)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);

  if (meta_prefs_get_raise_on_click ())
    meta_window_raise (window);
  meta_window_focus (window, timestamp);

  meta_window_show_menu (window, menu, root_x, root_y);
}

void
meta_x11_wm_show_window_menu_for_rect (MetaX11Display     *x11_display,
                                       Window              frame_xwindow,
                                       MetaWindowMenuType  menu,
                                       MetaRectangle      *rect,
                                       uint32_t            timestamp)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);

  if (meta_prefs_get_raise_on_click ())
    meta_window_raise (window);
  meta_window_focus (window, timestamp);

  meta_window_show_menu_for_rect (window, menu, rect);
}

gboolean
meta_x11_wm_begin_grab_op (MetaX11Display *x11_display,
                           Window          frame_xwindow,
                           MetaGrabOp      op,
                           gboolean        pointer_already_grabbed,
                           gboolean        frame_action,
                           int             button,
                           gulong          modmask,
                           uint32_t        timestamp,
                           int             root_x,
                           int             root_y)
{
  MetaWindow *window = window_from_frame (x11_display, frame_xwindow);
  MetaDisplay *display;

  display = meta_x11_display_get_display (x11_display);

  return meta_display_begin_grab_op (display, window,
                                     op, pointer_already_grabbed,
                                     frame_action,
                                     button, modmask,
                                     timestamp, root_x, root_y);
}

void
meta_x11_wm_end_grab_op (MetaX11Display *x11_display,
                         uint32_t        timestamp)
{
  MetaDisplay *display;

  display = meta_x11_display_get_display (x11_display);

  meta_display_end_grab_op (display, timestamp);
}

MetaGrabOp
meta_x11_wm_get_grab_op (MetaX11Display *x11_display)
{
  MetaDisplay *display;

  display = meta_x11_display_get_display (x11_display);

  return display->grab_op;
}

void
meta_x11_wm_grab_buttons  (MetaX11Display *x11_display,
                           Window          frame_xwindow)
{
  MetaDisplay *display;

  display = meta_x11_display_get_display (x11_display);

  meta_verbose ("Grabbing buttons on frame 0x%lx\n", frame_xwindow);
  meta_display_grab_window_buttons (display, frame_xwindow);
}

void
meta_x11_wm_set_screen_cursor (MetaX11Display *x11_display,
                               Window          frame_on_screen,
                               MetaCursor      cursor)
{
  MetaWindow *window = window_from_frame (x11_display, frame_on_screen);

  meta_frame_set_screen_cursor (window->frame, cursor);
}
