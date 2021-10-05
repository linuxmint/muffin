/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X window decorations */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2005 Elijah Newren
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

#include "core/frame.h"

#include "backends/x11/meta-backend-x11.h"
#include "core/bell.h"
#include "core/keybindings-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"

#define EVENT_MASK (SubstructureRedirectMask |                     \
                    StructureNotifyMask | SubstructureNotifyMask | \
                    ExposureMask | FocusChangeMask)

void
meta_window_ensure_frame (MetaWindow *window)
{
  MetaFrame *frame;
  XSetWindowAttributes attrs;
  gulong create_serial;
  MetaX11Display *x11_display;

  if (window->frame)
    return;

  x11_display = window->display->x11_display;

  frame = g_new (MetaFrame, 1);

  frame->window = window;
  frame->xwindow = None;

  frame->rect = window->rect;
  frame->child_x = 0;
  frame->child_y = 0;
  frame->bottom_height = 0;
  frame->right_width = 0;
  frame->current_cursor = 0;

  frame->borders_cached = FALSE;

  meta_verbose ("Frame geometry %d,%d  %dx%d\n",
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  frame->ui_frame = meta_ui_create_frame (x11_display->ui,
                                          x11_display->xdisplay,
                                          frame->window,
                                          window->xvisual,
                                          frame->rect.x,
                                          frame->rect.y,
                                          frame->rect.width,
                                          frame->rect.height,
                                          &create_serial);
  frame->xwindow = frame->ui_frame->xwindow;

  meta_stack_tracker_record_add (window->display->stack_tracker,
                                 frame->xwindow,
                                 create_serial);

  meta_verbose ("Frame for %s is 0x%lx\n", frame->window->desc, frame->xwindow);
  attrs.event_mask = EVENT_MASK;
  XChangeWindowAttributes (x11_display->xdisplay,
			   frame->xwindow, CWEventMask, &attrs);

  meta_x11_display_register_x_window (x11_display, &frame->xwindow, window);

  meta_x11_error_trap_push (x11_display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* the reparent will unmap the window,
                               * we don't want to take that as a withdraw
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent\n", window->desc);
      window->unmaps_pending += 1;
    }

  meta_stack_tracker_record_remove (window->display->stack_tracker,
                                    window->xwindow,
                                    XNextRequest (x11_display->xdisplay));
  XReparentWindow (x11_display->xdisplay,
                   window->xwindow,
                   frame->xwindow,
                   frame->child_x,
                   frame->child_y);
  window->reparents_pending += 1;
  /* FIXME handle this error */
  meta_x11_error_trap_pop (x11_display);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  /* stick frame to the window */
  window->frame = frame;

  /* Now that frame->xwindow is registered with window, we can set its
   * style and background.
   */
  meta_frame_update_style (frame);
  meta_frame_update_title (frame);

  meta_ui_map_frame (x11_display->ui, frame->xwindow);

  {
    MetaBackend *backend = meta_get_backend ();
    if (META_IS_BACKEND_X11 (backend))
      {
        Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

        /* Since the backend selects for events on another connection,
         * make sure to sync the GTK+ connection to ensure that the
         * frame window has been created on the server at this point. */
        XSync (x11_display->xdisplay, False);

        unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
        XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

        XISelectEvents (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                        frame->xwindow, &mask, 1);

        XISetMask (mask.mask, XI_ButtonPress);
        XISetMask (mask.mask, XI_ButtonRelease);
        XISetMask (mask.mask, XI_Motion);
        XISetMask (mask.mask, XI_Enter);
        XISetMask (mask.mask, XI_Leave);

        XISelectEvents (xdisplay, frame->xwindow, &mask, 1);
      }
  }

  /* Move keybindings to frame instead of window */
  meta_window_grab_keys (window);
}

void
meta_window_destroy_frame (MetaWindow *window)
{
  MetaFrame *frame;
  MetaFrameBorders borders;
  MetaX11Display *x11_display;

  if (window->frame == NULL)
    return;

  x11_display = window->display->x11_display;

  meta_verbose ("Unframing window %s\n", window->desc);

  frame = window->frame;

  meta_frame_calc_borders (frame, &borders);

  /* Unparent the client window; it may be destroyed,
   * thus the error trap.
   */
  meta_x11_error_trap_push (x11_display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* Keep track of unmapping it, so we
                               * can identify a withdraw initiated
                               * by the client.
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent back to root\n", window->desc);
      window->unmaps_pending += 1;
    }

  if (!x11_display->closing)
    {
      meta_stack_tracker_record_add (window->display->stack_tracker,
                                     window->xwindow,
                                     XNextRequest (x11_display->xdisplay));

      XReparentWindow (x11_display->xdisplay,
                       window->xwindow,
                       x11_display->xroot,
                       /* Using anything other than client root window coordinates
                        * coordinates here means we'll need to ensure a configure
                        * notify event is sent; see bug 399552.
                        */
                       window->frame->rect.x + borders.invisible.left,
                       window->frame->rect.y + borders.invisible.top);
      window->reparents_pending += 1;
    }

  meta_x11_error_trap_pop (x11_display);

  meta_ui_frame_unmanage (frame->ui_frame);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  meta_x11_display_unregister_x_window (x11_display, frame->xwindow);

  window->frame = NULL;
  if (window->frame_bounds)
    {
      cairo_region_destroy (window->frame_bounds);
      window->frame_bounds = NULL;
    }

  /* Move keybindings to window instead of frame */
  meta_window_grab_keys (window);

  g_free (frame);

  /* Put our state back where it should be */
  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}


MetaFrameFlags
meta_frame_get_flags (MetaFrame *frame)
{
  MetaFrameFlags flags;

  flags = 0;

  if (frame->window->border_only)
    {
      ; /* FIXME this may disable the _function_ as well as decor
         * in some cases, which is sort of wrong.
         */
    }
  else
    {
      flags |= META_FRAME_ALLOWS_MENU;

      if (frame->window->has_close_func)
        flags |= META_FRAME_ALLOWS_DELETE;

      if (frame->window->has_maximize_func)
        flags |= META_FRAME_ALLOWS_MAXIMIZE;

      if (frame->window->has_minimize_func)
        flags |= META_FRAME_ALLOWS_MINIMIZE;

      if (frame->window->has_shade_func)
        flags |= META_FRAME_ALLOWS_SHADE;
    }

  if (META_WINDOW_ALLOWS_MOVE (frame->window))
    flags |= META_FRAME_ALLOWS_MOVE;

  if (META_WINDOW_ALLOWS_HORIZONTAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_HORIZONTAL_RESIZE;

  if (META_WINDOW_ALLOWS_VERTICAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_VERTICAL_RESIZE;

  if (meta_window_appears_focused (frame->window))
    flags |= META_FRAME_HAS_FOCUS;

  if (frame->window->shaded)
    flags |= META_FRAME_SHADED;

  if (frame->window->on_all_workspaces_requested)
    flags |= META_FRAME_STUCK;

  /* FIXME: Should we have some kind of UI for windows that are just vertically
   * maximized or just horizontally maximized?
   */
  if (META_WINDOW_MAXIMIZED (frame->window))
    flags |= META_FRAME_MAXIMIZED;

  if (META_WINDOW_TILED_LEFT (frame->window))
    flags |= META_FRAME_TILED_LEFT;

  if (META_WINDOW_TILED_RIGHT (frame->window))
    flags |= META_FRAME_TILED_RIGHT;

  if (frame->window->fullscreen)
    flags |= META_FRAME_FULLSCREEN;

  if (frame->window->wm_state_above)
    flags |= META_FRAME_ABOVE;

  return flags;
}

void
meta_frame_borders_clear (MetaFrameBorders *self)
{
  self->visible.top    = self->invisible.top    = self->total.top    = 0;
  self->visible.bottom = self->invisible.bottom = self->total.bottom = 0;
  self->visible.left   = self->invisible.left   = self->total.left   = 0;
  self->visible.right  = self->invisible.right  = self->total.right  = 0;
}

void
meta_frame_calc_borders (MetaFrame        *frame,
                         MetaFrameBorders *borders)
{
  /* Save on if statements and potential uninitialized values
   * in callers -- if there's no frame, then zero the borders. */
  if (frame == NULL)
    meta_frame_borders_clear (borders);
  else
    {
      if (!frame->borders_cached)
        {
          meta_ui_frame_get_borders (frame->ui_frame, &frame->cached_borders);
          frame->borders_cached = TRUE;
        }

      *borders = frame->cached_borders;
    }
}

void
meta_frame_clear_cached_borders (MetaFrame *frame)
{
  frame->borders_cached = FALSE;
}

gboolean
meta_frame_sync_to_window (MetaFrame *frame,
                           gboolean   need_resize)
{
  meta_topic (META_DEBUG_GEOMETRY,
              "Syncing frame geometry %d,%d %dx%d (SE: %d,%d)\n",
              frame->rect.x, frame->rect.y,
              frame->rect.width, frame->rect.height,
              frame->rect.x + frame->rect.width,
              frame->rect.y + frame->rect.height);

  meta_ui_frame_move_resize (frame->ui_frame,
			     frame->rect.x,
			     frame->rect.y,
			     frame->rect.width,
			     frame->rect.height);

  return need_resize;
}

cairo_region_t *
meta_frame_get_frame_bounds (MetaFrame *frame)
{
  return meta_ui_frame_get_bounds (frame->ui_frame);
}

void
meta_frame_get_mask (MetaFrame             *frame,
                     cairo_rectangle_int_t *frame_rect,
                     cairo_t               *cr)
{
  meta_ui_frame_get_mask (frame->ui_frame, frame_rect, cr);
}

void
meta_frame_queue_draw (MetaFrame *frame)
{
  meta_ui_frame_queue_draw (frame->ui_frame);
}

void
meta_frame_set_screen_cursor (MetaFrame	*frame,
			      MetaCursor cursor)
{
  MetaX11Display *x11_display;
  Cursor xcursor;
  if (cursor == frame->current_cursor)
    return;

  frame->current_cursor = cursor;
  x11_display = frame->window->display->x11_display;

  if (cursor == META_CURSOR_DEFAULT)
    XUndefineCursor (x11_display->xdisplay, frame->xwindow);
  else
    {
      xcursor = meta_x11_display_create_x_cursor (x11_display, cursor);
      XDefineCursor (x11_display->xdisplay, frame->xwindow, xcursor);
      XFlush (x11_display->xdisplay);
      XFreeCursor (x11_display->xdisplay, xcursor);
    }
}

Window
meta_frame_get_xwindow (MetaFrame *frame)
{
  return frame->xwindow;
}

void
meta_frame_update_style (MetaFrame *frame)
{
  meta_ui_frame_update_style (frame->ui_frame);
}

void
meta_frame_update_title (MetaFrame *frame)
{
  if (frame->window->title)
    meta_ui_frame_set_title (frame->ui_frame, frame->window->title);
}
