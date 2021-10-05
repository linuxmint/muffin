/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* The file is loosely based on xwayland/selection.c from Weston */

#include "config.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <glib-unix.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixinputstream.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>

#include "meta/meta-x11-errors.h"
#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-xwayland-private.h"
#include "wayland/meta-xwayland-dnd-private.h"
#include "wayland/meta-xwayland.h"
#include "x11/meta-x11-display-private.h"

#define INCR_CHUNK_SIZE (128 * 1024)
#define XDND_VERSION 5

struct _MetaWaylandDataSourceXWayland
{
  MetaWaylandDataSource parent;
  MetaXWaylandDnd *dnd;
  gboolean has_utf8_string_atom;
};

struct _MetaXWaylandDnd
{
  Window owner;
  Time client_message_timestamp;
  MetaWaylandDataSource *source; /* owned by MetaWaylandDataDevice */
  MetaWaylandSurface *focus_surface;
  Window dnd_window; /* Mutter-internal window, acts as peer on wayland drop sites */
  Window dnd_dest; /* X11 drag dest window */
  guint32 last_motion_time;
};

enum
{
  ATOM_DND_SELECTION,
  ATOM_DND_AWARE,
  ATOM_DND_STATUS,
  ATOM_DND_POSITION,
  ATOM_DND_ENTER,
  ATOM_DND_LEAVE,
  ATOM_DND_DROP,
  ATOM_DND_FINISHED,
  ATOM_DND_PROXY,
  ATOM_DND_TYPE_LIST,
  ATOM_DND_ACTION_MOVE,
  ATOM_DND_ACTION_COPY,
  ATOM_DND_ACTION_ASK,
  ATOM_DND_ACTION_PRIVATE,
  N_DND_ATOMS
};

/* Matches order in enum above */
const gchar *atom_names[] = {
  "XdndSelection",
  "XdndAware",
  "XdndStatus",
  "XdndPosition",
  "XdndEnter",
  "XdndLeave",
  "XdndDrop",
  "XdndFinished",
  "XdndProxy",
  "XdndTypeList",
  "XdndActionMove",
  "XdndActionCopy",
  "XdndActionAsk",
  "XdndActionPrivate",
  NULL
};

Atom xdnd_atoms[N_DND_ATOMS];

G_DEFINE_TYPE (MetaWaylandDataSourceXWayland, meta_wayland_data_source_xwayland,
               META_TYPE_WAYLAND_DATA_SOURCE);

/* XDND helpers */
static Atom
action_to_atom (uint32_t action)
{
  if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY)
    return xdnd_atoms[ATOM_DND_ACTION_COPY];
  else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
    return xdnd_atoms[ATOM_DND_ACTION_MOVE];
  else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)
    return xdnd_atoms[ATOM_DND_ACTION_ASK];
  else
    return None;
}

static enum wl_data_device_manager_dnd_action
atom_to_action (Atom atom)
{
  if (atom == xdnd_atoms[ATOM_DND_ACTION_COPY] ||
      atom == xdnd_atoms[ATOM_DND_ACTION_PRIVATE])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  else if (atom == xdnd_atoms[ATOM_DND_ACTION_MOVE])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  else if (atom == xdnd_atoms[ATOM_DND_ACTION_ASK])
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  else
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

static void
xdnd_send_enter (MetaXWaylandDnd *dnd,
                 Window           dest)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  MetaWaylandDataSource *data_source;
  XEvent xev = { 0 };
  gchar **p;
  struct wl_array *source_mime_types;

  meta_x11_error_trap_push (x11_display);

  data_source = compositor->seat->data_device.dnd_data_source;
  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_ENTER];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = x11_display->selection.xwindow;
  xev.xclient.data.l[1] = XDND_VERSION << 24; /* version */
  xev.xclient.data.l[2] = xev.xclient.data.l[3] = xev.xclient.data.l[4] = 0;

  source_mime_types = meta_wayland_data_source_get_mime_types (data_source);
  if (source_mime_types->size <= 3)
    {
      /* The mimetype atoms fit in this same message */
      gint i = 2;

      wl_array_for_each (p, source_mime_types)
        {
          xev.xclient.data.l[i++] = gdk_x11_get_xatom_by_name (*p);
        }
    }
  else
    {
      /* We have more than 3 mimetypes, we must set up
       * the mimetype list as a XdndTypeList property.
       */
      g_autofree Atom *atomlist = NULL;
      gint i = 0;

      xev.xclient.data.l[1] |= 1;
      atomlist = g_new0 (Atom, source_mime_types->size);

      wl_array_for_each (p, source_mime_types)
        {
          atomlist[i++] = gdk_x11_get_xatom_by_name (*p);
        }

      XChangeProperty (xdisplay, x11_display->selection.xwindow,
                       xdnd_atoms[ATOM_DND_TYPE_LIST],
                       XA_ATOM, 32, PropModeReplace,
                       (guchar *) atomlist, i);
    }

  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    g_critical ("Error sending XdndEnter");
}

static void
xdnd_send_leave (MetaXWaylandDnd *dnd,
                 Window           dest)
{
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_LEAVE];
  xev.xclient.format = 32;
  xev.xclient.window = dest;
  xev.xclient.data.l[0] = x11_display->selection.xwindow;

  meta_x11_error_trap_push (x11_display);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);
  meta_x11_error_trap_pop (x11_display);
}

static void
xdnd_send_position (MetaXWaylandDnd *dnd,
                    Window           dest,
                    uint32_t         time,
                    int              x,
                    int              y)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandDataSource *source = compositor->seat->data_device.dnd_data_source;
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  uint32_t action = 0, user_action, actions;
  XEvent xev = { 0 };

  user_action = meta_wayland_data_source_get_user_action (source);
  meta_wayland_data_source_get_actions (source, &actions);

  if (user_action & actions)
    action = user_action;
  if (!action)
    action = actions;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_POSITION];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = x11_display->selection.xwindow;
  xev.xclient.data.l[1] = 0;
  xev.xclient.data.l[2] = (x << 16) | y;
  xev.xclient.data.l[3] = time;
  xev.xclient.data.l[4] = action_to_atom (action);

  meta_x11_error_trap_push (x11_display);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    g_critical ("Error sending XdndPosition");
}

static void
xdnd_send_drop (MetaXWaylandDnd *dnd,
                Window           dest,
                uint32_t         time)
{
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_DROP];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = x11_display->selection.xwindow;
  xev.xclient.data.l[2] = time;

  meta_x11_error_trap_push (x11_display);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    g_critical ("Error sending XdndDrop");
}

static void
xdnd_send_finished (MetaXWaylandDnd *dnd,
                    Window           dest,
                    gboolean         accepted)
{
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  MetaWaylandDataSource *source = dnd->source;
  uint32_t action = 0;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_FINISHED];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = dnd->dnd_window;

  if (accepted)
    {
      action = meta_wayland_data_source_get_current_action (source);
      xev.xclient.data.l[1] = 1; /* Drop successful */
      xev.xclient.data.l[2] = action_to_atom (action);
    }

  meta_x11_error_trap_push (x11_display);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    g_critical ("Error sending XdndFinished");
}

static void
xdnd_send_status (MetaXWaylandDnd *dnd,
                  Window           dest,
                  uint32_t         action)
{
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = xdnd_atoms[ATOM_DND_STATUS];
  xev.xclient.format = 32;
  xev.xclient.window = dest;

  xev.xclient.data.l[0] = dnd->dnd_window;
  xev.xclient.data.l[1] = 1 << 1; /* Bit 2: dest wants XdndPosition messages */
  xev.xclient.data.l[4] = action_to_atom (action);

  if (xev.xclient.data.l[4])
    xev.xclient.data.l[1] |= 1 << 0; /* Bit 1: dest accepts the drop */

  meta_x11_error_trap_push (x11_display);
  XSendEvent (xdisplay, dest, False, NoEventMask, &xev);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    g_critical ("Error sending Xdndstatus");
}

static void
meta_xwayland_end_dnd_grab (MetaWaylandDataDevice *data_device,
                            gboolean               success)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;
  MetaWaylandDragGrab *drag_grab = compositor->seat->data_device.current_grab;
  MetaXWaylandDnd *dnd = manager->dnd;

  if (drag_grab)
    {
      if (!success && dnd->source)
        meta_wayland_data_source_set_current_offer (dnd->source, NULL);

      meta_wayland_data_device_end_drag (data_device);
    }

  XMoveResizeWindow (xdisplay, dnd->dnd_window, -1, -1, 1, 1);
  XUnmapWindow (xdisplay, dnd->dnd_window);
}

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *res,
             GOutputStream *stream)
{
  GError *error = NULL;

  if (!meta_selection_transfer_finish (selection, res, &error))
    {
      g_warning ("Could not transfer DnD selection: %s\n", error->message);
      g_error_free (error);
    }

  g_output_stream_close (stream, NULL, NULL);
  g_object_unref (stream);
}

static void
meta_x11_source_send (MetaWaylandDataSource *source,
                      const gchar           *mime_type,
                      gint                   fd)
{
  MetaDisplay *display = meta_get_display ();
  GOutputStream *stream;

  stream = g_unix_output_stream_new (fd, TRUE);
  meta_selection_transfer_async (meta_display_get_selection (display),
                                 META_SELECTION_DND,
                                 mime_type,
                                 -1,
                                 stream,
                                 NULL,
                                 (GAsyncReadyCallback) transfer_cb,
                                 stream);
}

static void
meta_x11_source_target (MetaWaylandDataSource *source,
                        const gchar           *mime_type)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;
  uint32_t action = 0;

  if (mime_type)
    action = meta_wayland_data_source_get_current_action (source);

  xdnd_send_status (dnd, dnd->owner, action);
}

static void
meta_x11_source_cancel (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;

  xdnd_send_finished (dnd, dnd->owner, FALSE);
}

static void
meta_x11_source_action (MetaWaylandDataSource *source,
                        uint32_t               action)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;

  if (!meta_wayland_data_source_has_target (source))
    action = 0;

  xdnd_send_status (dnd, dnd->owner, action);
}

static void
meta_x11_source_drop_performed (MetaWaylandDataSource *source)
{
}

static void
meta_x11_source_drag_finished (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  MetaXWaylandDnd *dnd = source_xwayland->dnd;
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  uint32_t action = meta_wayland_data_source_get_current_action (source);

  if (action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE)
    {
      Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

      /* Request data deletion on the drag source */
      XConvertSelection (xdisplay,
                         xdnd_atoms[ATOM_DND_SELECTION],
                         gdk_x11_get_xatom_by_name ("DELETE"),
                         gdk_x11_get_xatom_by_name ("_META_SELECTION"),
                         x11_display->selection.xwindow,
                         META_CURRENT_TIME);
    }

  xdnd_send_finished (dnd, dnd->owner, TRUE);
}

static void
meta_wayland_data_source_xwayland_init (MetaWaylandDataSourceXWayland *source_xwayland)
{
}

static void
meta_wayland_data_source_xwayland_class_init (MetaWaylandDataSourceXWaylandClass *klass)
{
  MetaWaylandDataSourceClass *data_source_class =
    META_WAYLAND_DATA_SOURCE_CLASS (klass);

  data_source_class->send = meta_x11_source_send;
  data_source_class->target = meta_x11_source_target;
  data_source_class->cancel = meta_x11_source_cancel;
  data_source_class->action = meta_x11_source_action;
  data_source_class->drop_performed = meta_x11_source_drop_performed;
  data_source_class->drag_finished = meta_x11_source_drag_finished;
}

static MetaWaylandDataSource *
meta_wayland_data_source_xwayland_new (MetaXWaylandDnd *dnd)
{
  MetaWaylandDataSourceXWayland *source_xwayland;

  source_xwayland = g_object_new (META_TYPE_WAYLAND_DATA_SOURCE_XWAYLAND, NULL);
  source_xwayland->dnd = dnd;

  return META_WAYLAND_DATA_SOURCE (source_xwayland);
}

static void
meta_x11_drag_dest_focus_in (MetaWaylandDataDevice *data_device,
                             MetaWaylandSurface    *surface,
                             MetaWaylandDataOffer  *offer)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;

  dnd->dnd_dest = meta_wayland_surface_get_window (surface)->xwindow;
  xdnd_send_enter (dnd, dnd->dnd_dest);
}

static void
meta_x11_drag_dest_focus_out (MetaWaylandDataDevice *data_device,
                              MetaWaylandSurface    *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;

  xdnd_send_leave (dnd, dnd->dnd_dest);
  dnd->dnd_dest = None;
}

static void
meta_x11_drag_dest_motion (MetaWaylandDataDevice *data_device,
                           MetaWaylandSurface    *surface,
                           const ClutterEvent    *event)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  guint32 time;
  gfloat x, y;

  time = clutter_event_get_time (event);
  clutter_event_get_coords (event, &x, &y);
  xdnd_send_position (dnd, dnd->dnd_dest, time, x, y);
}

static void
meta_x11_drag_dest_drop (MetaWaylandDataDevice *data_device,
                         MetaWaylandSurface    *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;

  xdnd_send_drop (dnd, dnd->dnd_dest,
                  meta_display_get_current_time_roundtrip (meta_get_display ()));
}

static void
meta_x11_drag_dest_update (MetaWaylandDataDevice *data_device,
                           MetaWaylandSurface    *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandSeat *seat = compositor->seat;
  graphene_point_t pos;

  clutter_input_device_get_coords (seat->pointer->device, NULL, &pos);
  xdnd_send_position (dnd, dnd->dnd_dest,
                      clutter_get_current_event_time (),
                      pos.x, pos.y);
}

static const MetaWaylandDragDestFuncs meta_x11_drag_dest_funcs = {
  meta_x11_drag_dest_focus_in,
  meta_x11_drag_dest_focus_out,
  meta_x11_drag_dest_motion,
  meta_x11_drag_dest_drop,
  meta_x11_drag_dest_update
};

const MetaWaylandDragDestFuncs *
meta_xwayland_selection_get_drag_dest_funcs (void)
{
  return &meta_x11_drag_dest_funcs;
}

static gboolean
meta_xwayland_data_source_fetch_mimetype_list (MetaWaylandDataSource *source,
                                               Window                 window,
                                               Atom                   prop)
{
  MetaWaylandDataSourceXWayland *source_xwayland =
    META_WAYLAND_DATA_SOURCE_XWAYLAND (source);
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  gulong nitems_ret, bytes_after_ret, i;
  Atom *atoms, type_ret, utf8_string;
  int format_ret;
  struct wl_array *source_mime_types;

  source_mime_types = meta_wayland_data_source_get_mime_types (source);
  if (source_mime_types->size != 0)
    return TRUE;

  utf8_string = gdk_x11_get_xatom_by_name ("UTF8_STRING");
  XGetWindowProperty (xdisplay, window, prop,
                      0, /* offset */
                      0x1fffffff, /* length */
                      False, /* delete */
                      AnyPropertyType,
                      &type_ret,
                      &format_ret,
                      &nitems_ret,
                      &bytes_after_ret,
                      (guchar **) &atoms);

  if (nitems_ret == 0 || type_ret != XA_ATOM)
    {
      XFree (atoms);
      return FALSE;
    }

  for (i = 0; i < nitems_ret; i++)
    {
      const gchar *mime_type;

      if (atoms[i] == utf8_string)
        {
          meta_wayland_data_source_add_mime_type (source,
                                                  "text/plain;charset=utf-8");
          source_xwayland->has_utf8_string_atom = TRUE;
        }

      mime_type = gdk_x11_get_xatom_name (atoms[i]);
      meta_wayland_data_source_add_mime_type (source, mime_type);
    }

  XFree (atoms);

  return TRUE;
}

static MetaWaylandSurface *
pick_drop_surface (MetaWaylandCompositor *compositor,
                   const ClutterEvent    *event)
{
  MetaDisplay *display = meta_get_display ();
  MetaWindow *focus_window = NULL;
  graphene_point_t pos;

  clutter_event_get_coords (event, &pos.x, &pos.y);
  focus_window = meta_stack_get_default_focus_window_at_point (display->stack,
                                                               NULL, NULL,
                                                               pos.x, pos.y);
  return focus_window ? focus_window->surface : NULL;
}

static void
repick_drop_surface (MetaWaylandCompositor *compositor,
                     MetaWaylandDragGrab   *drag_grab,
                     const ClutterEvent    *event)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandSurface *focus = NULL;
  MetaWindow *focus_window;

  focus = pick_drop_surface (compositor, event);
  if (dnd->focus_surface == focus)
    return;

  dnd->focus_surface = focus;

  if (focus)
    focus_window = meta_wayland_surface_get_window (focus);
  else
    focus_window = NULL;

  if (focus_window &&
      focus_window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      XMapRaised (xdisplay, dnd->dnd_window);
      XMoveResizeWindow (xdisplay, dnd->dnd_window,
                         focus_window->rect.x,
                         focus_window->rect.y,
                         focus_window->rect.width,
                         focus_window->rect.height);
    }
  else
    {
      XMoveResizeWindow (xdisplay, dnd->dnd_window, -1, -1, 1, 1);
      XUnmapWindow (xdisplay, dnd->dnd_window);
    }
}

static void
drag_xgrab_focus (MetaWaylandPointerGrab *grab,
                  MetaWaylandSurface     *surface)
{
  /* Do not update the focus here. First, the surface may perfectly
   * be the X11 source DnD icon window's, so we can only be fooled
   * here. Second, delaying focus handling to XdndEnter/Leave
   * makes us do the negotiation orderly on the X11 side.
   */
}

static void
drag_xgrab_motion (MetaWaylandPointerGrab *grab,
                   const ClutterEvent     *event)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandSeat *seat = compositor->seat;

  repick_drop_surface (compositor,
                       (MetaWaylandDragGrab *) grab,
                       event);

  dnd->last_motion_time = clutter_event_get_time (event);
  meta_wayland_pointer_send_motion (seat->pointer, event);
}

static void
drag_xgrab_button (MetaWaylandPointerGrab *grab,
                   const ClutterEvent     *event)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandSeat *seat = compositor->seat;
  MetaWaylandDataSource *data_source;

  meta_wayland_pointer_send_button (seat->pointer, event);
  data_source = compositor->seat->data_device.dnd_data_source;

  if (seat->pointer->button_count == 0 &&
      (!meta_wayland_drag_grab_get_focus ((MetaWaylandDragGrab *) grab) ||
       meta_wayland_data_source_get_current_action (data_source) ==
       WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE))
    meta_xwayland_end_dnd_grab (&seat->data_device, FALSE);
}

static const MetaWaylandPointerGrabInterface drag_xgrab_interface = {
  drag_xgrab_focus,
  drag_xgrab_motion,
  drag_xgrab_button,
};

static gboolean
meta_xwayland_dnd_handle_client_message (MetaWaylandCompositor *compositor,
                                         XEvent                *xevent)
{
  XClientMessageEvent *event = (XClientMessageEvent *) xevent;
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandSeat *seat = compositor->seat;
  MetaX11Display *x11_display = meta_get_display ()->x11_display;

  /* Source side messages */
  if (event->window == x11_display->selection.xwindow)
    {
      MetaWaylandDataSource *data_source;
      uint32_t action = 0;

      data_source = compositor->seat->data_device.dnd_data_source;

      if (!data_source)
        return FALSE;

      if (event->message_type == xdnd_atoms[ATOM_DND_STATUS])
        {
          /* The first bit in data.l[1] is set if the drag was accepted */
          meta_wayland_data_source_set_has_target (data_source,
                                                   (event->data.l[1] & 1) != 0);

          /* data.l[4] contains the action atom */
          if (event->data.l[4])
            action = atom_to_action ((Atom) event->data.l[4]);

          meta_wayland_data_source_set_current_action (data_source, action);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_FINISHED])
        {
          /* Reject messages mid-grab */
          if (compositor->seat->data_device.current_grab)
            return FALSE;

          meta_wayland_data_source_notify_finish (data_source);
          return TRUE;
        }
    }
  /* Dest side messages */
  else if (dnd->source &&
           compositor->seat->data_device.current_grab &&
           (Window) event->data.l[0] == dnd->owner)
    {
      MetaWaylandDragGrab *drag_grab = compositor->seat->data_device.current_grab;
      MetaWaylandSurface *drag_focus = meta_wayland_drag_grab_get_focus (drag_grab);

      if (!drag_focus &&
          event->message_type != xdnd_atoms[ATOM_DND_ENTER])
        return FALSE;

      if (event->message_type == xdnd_atoms[ATOM_DND_ENTER])
        {
          /* Bit 1 in data.l[1] determines whether there's 3 or less mimetype
           * atoms (and are thus contained in this same message), or whether
           * there's more than 3 and we need to check the XdndTypeList property
           * for the full list.
           */
          if (!(event->data.l[1] & 1))
            {
              /* Mimetypes are contained in this message */
              const gchar *mimetype;
              gint i;
              struct wl_array *source_mime_types;

              /* We only need to fetch once */
              source_mime_types =
                meta_wayland_data_source_get_mime_types (dnd->source);
              if (source_mime_types->size == 0)
                {
                  for (i = 2; i <= 4; i++)
                    {
                      if (event->data.l[i] == None)
                        break;

                      mimetype = gdk_x11_get_xatom_name (event->data.l[i]);
                      meta_wayland_data_source_add_mime_type (dnd->source,
                                                              mimetype);
                    }
                }
            }
          else
            {
              /* Fetch mimetypes from type list */
              meta_xwayland_data_source_fetch_mimetype_list (dnd->source,
                                                             event->data.l[0],
                                                             xdnd_atoms[ATOM_DND_TYPE_LIST]);
            }

          meta_wayland_data_source_set_actions (dnd->source,
                                                WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                                WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                                                WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK);
          meta_wayland_drag_grab_set_focus (drag_grab, dnd->focus_surface);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_POSITION])
        {
          ClutterEvent *motion;
          graphene_point_t pos;
          uint32_t action = 0;

          dnd->client_message_timestamp = event->data.l[3];

          motion = clutter_event_new (CLUTTER_MOTION);
          clutter_input_device_get_coords (seat->pointer->device, NULL, &pos);
          clutter_event_set_coords (motion, pos.x, pos.y);
          clutter_event_set_device (motion, seat->pointer->device);
          clutter_event_set_source_device (motion, seat->pointer->device);
          clutter_event_set_time (motion, dnd->last_motion_time);

          action = atom_to_action ((Atom) event->data.l[4]);
          meta_wayland_data_source_set_user_action (dnd->source, action);

          meta_wayland_surface_drag_dest_motion (drag_focus, motion);
          xdnd_send_status (dnd, (Window) event->data.l[0],
                            meta_wayland_data_source_get_current_action (dnd->source));

          clutter_event_free (motion);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_LEAVE])
        {
          meta_wayland_drag_grab_set_focus (drag_grab, NULL);
          return TRUE;
        }
      else if (event->message_type == xdnd_atoms[ATOM_DND_DROP])
        {
          dnd->client_message_timestamp = event->data.l[2];
          meta_wayland_surface_drag_dest_drop (drag_focus);
          meta_xwayland_end_dnd_grab (&seat->data_device, TRUE);
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
meta_xwayland_dnd_handle_xfixes_selection_notify (MetaWaylandCompositor *compositor,
                                                  XEvent                *xevent)
{
  XFixesSelectionNotifyEvent *event = (XFixesSelectionNotifyEvent *) xevent;
  MetaXWaylandDnd *dnd = compositor->xwayland_manager.dnd;
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  MetaWaylandSurface *focus;

  if (event->selection != xdnd_atoms[ATOM_DND_SELECTION])
    return FALSE;

  dnd->owner = event->owner;
  focus = compositor->seat->pointer->focus_surface;

  if (event->owner != None && event->owner != x11_display->selection.xwindow &&
      focus && meta_xwayland_is_xwayland_surface (focus))
    {
      dnd->source = meta_wayland_data_source_xwayland_new (dnd);
      meta_wayland_data_device_set_dnd_source (&compositor->seat->data_device,
                                               dnd->source);

      meta_wayland_data_device_start_drag (data_device,
                                           wl_resource_get_client (focus->resource),
                                           &drag_xgrab_interface,
                                           focus, dnd->source,
                                           NULL);
    }
  else if (event->owner == None)
    {
      meta_xwayland_end_dnd_grab (data_device, FALSE);
      g_clear_object (&dnd->source);
    }

  return FALSE;
}

gboolean
meta_xwayland_dnd_handle_event (XEvent *xevent)
{
  MetaWaylandCompositor *compositor;

  compositor = meta_wayland_compositor_get_default ();

  if (!compositor->xwayland_manager.dnd)
    return FALSE;

  switch (xevent->type)
    {
    case ClientMessage:
      return meta_xwayland_dnd_handle_client_message (compositor, xevent);
    default:
      {
        MetaX11Display *x11_display = meta_get_display ()->x11_display;

        if (xevent->type - x11_display->xfixes_event_base == XFixesSelectionNotify)
          return meta_xwayland_dnd_handle_xfixes_selection_notify (compositor, xevent);

        return FALSE;
      }
    }
}

void
meta_xwayland_init_dnd (Display *xdisplay)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;
  MetaXWaylandDnd *dnd = manager->dnd;
  XSetWindowAttributes attributes;
  guint32 i, version = XDND_VERSION;

  g_assert (manager->dnd == NULL);

  manager->dnd = dnd = g_slice_new0 (MetaXWaylandDnd);

  for (i = 0; i < N_DND_ATOMS; i++)
    xdnd_atoms[i] = gdk_x11_get_xatom_by_name (atom_names[i]);

  attributes.event_mask = PropertyChangeMask | SubstructureNotifyMask;
  attributes.override_redirect = True;

  dnd->dnd_window = XCreateWindow (xdisplay,
                                   gdk_x11_window_get_xid (gdk_get_default_root_window ()),
                                   -1, -1, 1, 1,
                                   0, /* border width */
                                   0, /* depth */
                                   InputOnly, /* class */
                                   CopyFromParent, /* visual */
                                   CWEventMask | CWOverrideRedirect,
                                   &attributes);
  XChangeProperty (xdisplay, dnd->dnd_window,
                   xdnd_atoms[ATOM_DND_AWARE],
                   XA_ATOM, 32, PropModeReplace,
                   (guchar*) &version, 1);
}

void
meta_xwayland_shutdown_dnd (Display *xdisplay)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;
  MetaXWaylandDnd *dnd = manager->dnd;

  g_assert (dnd != NULL);

  XDestroyWindow (xdisplay, dnd->dnd_window);
  dnd->dnd_window = None;

  g_slice_free (MetaXWaylandDnd, dnd);
  manager->dnd = NULL;
}
