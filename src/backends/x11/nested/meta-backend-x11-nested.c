/*
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
 */

#include "config.h"

#include "backends/x11/nested/meta-backend-x11-nested.h"

#include "backends/meta-monitor-manager-dummy.h"
#include "backends/x11/nested/meta-backend-x11-nested.h"
#include "backends/x11/nested/meta-cursor-renderer-x11-nested.h"
#include "backends/x11/nested/meta-renderer-x11-nested.h"

#include "wayland/meta-wayland.h"

typedef struct _MetaBackendX11NestedPrivate
{
  MetaGpu *gpu;
} MetaBackendX11NestedPrivate;

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackendX11Nested, meta_backend_x11_nested,
                         META_TYPE_BACKEND_X11,
                         G_ADD_PRIVATE (MetaBackendX11Nested)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init));

static MetaRenderer *
meta_backend_x11_nested_create_renderer (MetaBackend *backend,
                                         GError     **error)
{
  return g_object_new (META_TYPE_RENDERER_X11_NESTED,
                       "backend", backend,
                       NULL);
}

static MetaMonitorManager *
meta_backend_x11_nested_create_monitor_manager (MetaBackend *backend,
                                                GError     **error)
{
  return g_object_new (META_TYPE_MONITOR_MANAGER_DUMMY,
                       "backend", backend,
                       NULL);
}

static MetaCursorRenderer *
meta_backend_x11_nested_create_cursor_renderer (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CURSOR_RENDERER_X11_NESTED, NULL);
}

static MetaInputSettings *
meta_backend_x11_nested_create_input_settings (MetaBackend *backend)
{
  return NULL;
}

static void
meta_backend_x11_nested_update_screen_size (MetaBackend *backend,
                                            int          width,
                                            int          height)
{
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  if (meta_is_stage_views_enabled ())
    {
      meta_renderer_rebuild_views (renderer);
      clutter_stage_update_resource_scales (CLUTTER_STAGE (stage));
    }
  clutter_actor_set_size (stage, width, height);
}

static void
meta_backend_x11_nested_select_stage_events (MetaBackend *backend)
{
  MetaBackendX11 *x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (x11);
  Window xwin = meta_backend_x11_get_xwindow (x11);
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);
  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Enter);
  XISetMask (mask.mask, XI_Leave);
  XISetMask (mask.mask, XI_FocusIn);
  XISetMask (mask.mask, XI_FocusOut);
  XISetMask (mask.mask, XI_Motion);

  /*
   * When we're an X11 compositor, we can't take these events or else replaying
   * events from our passive root window grab will cause them to come back to
   * us.
   *
   * When we're a nested application, we want to behave like any other
   * application, so select these events like normal apps do.
   */
  XISetMask (mask.mask, XI_TouchBegin); XISetMask (mask.mask, XI_TouchEnd);
  XISetMask (mask.mask, XI_TouchUpdate);

  XISelectEvents (xdisplay, xwin, &mask, 1);

  /*
   * We have no way of tracking key changes when the stage doesn't have focus,
   * so we select for KeymapStateMask so that we get a complete dump of the
   * keyboard state in a KeymapNotify event that immediately follows each
   * FocusIn (and EnterNotify, but we ignore that.)
   */
  XWindowAttributes xwa;

  XGetWindowAttributes(xdisplay, xwin, &xwa);
  XSelectInput(xdisplay, xwin,
               xwa.your_event_mask | FocusChangeMask | KeymapStateMask);
}

static void
meta_backend_x11_nested_lock_layout_group (MetaBackend *backend,
                                           guint        idx)
{
}

static void
meta_backend_x11_nested_set_keymap (MetaBackend *backend,
                                    const char  *layouts,
                                    const char  *variants,
                                    const char  *options)
{
}

static gboolean
meta_backend_x11_nested_is_lid_closed (MetaBackend *backend)
{
  return FALSE;
}

static gboolean
meta_backend_x11_nested_handle_host_xevent (MetaBackendX11 *x11,
                                            XEvent         *event)
{
#ifdef HAVE_WAYLAND
  if (event->type == FocusIn)
    {
      Window xwin = meta_backend_x11_get_xwindow (x11);
      XEvent xev;

      if (event->xfocus.window == xwin)
        {
          MetaWaylandCompositor *compositor =
            meta_wayland_compositor_get_default ();
          Display *xdisplay = meta_backend_x11_get_xdisplay (x11);

          /*
           * Since we've selected for KeymapStateMask, every FocusIn is
           * followed immediately by a KeymapNotify event.
           */
          XMaskEvent (xdisplay, KeymapStateMask, &xev);
          meta_wayland_compositor_update_key_state (compositor,
                                                    xev.xkeymap.key_vector,
                                                    32, 8);
        }
    }
#endif

  return FALSE;
}

static void
meta_backend_x11_nested_translate_device_event (MetaBackendX11 *x11,
                                                XIDeviceEvent  *device_event)
{
  /* This codepath should only ever trigger as an X11 compositor,
   * and never under nested, as under nested all backend events
   * should be reported with respect to the stage window.
   */
  g_assert (device_event->event == meta_backend_x11_get_xwindow (x11));
}

static void
meta_backend_x11_nested_real_init_gpus (MetaBackendX11Nested *backend_x11_nested)
{
  MetaBackendX11NestedPrivate *priv =
    meta_backend_x11_nested_get_instance_private (backend_x11_nested);

  priv->gpu = g_object_new (META_TYPE_GPU_DUMMY,
                            "backend", backend_x11_nested,
                            NULL);
  meta_backend_add_gpu (META_BACKEND (backend_x11_nested), priv->gpu);
}

static void
meta_backend_x11_nested_post_init (MetaBackend *backend)
{
  MetaBackendClass *backend_class =
    META_BACKEND_CLASS (meta_backend_x11_nested_parent_class);

  backend_class->post_init (backend);

#ifdef HAVE_WAYLAND
  meta_backend_init_wayland (backend);
#endif
}

static gboolean
meta_backend_x11_nested_initable_init (GInitable     *initable,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
#ifdef HAVE_WAYLAND
  meta_backend_init_wayland_display (META_BACKEND (initable));
#endif

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_backend_x11_nested_initable_init;
}

static void
meta_backend_x11_nested_constructed (GObject *object)
{
  MetaBackendX11Nested *backend_x11_nested = META_BACKEND_X11_NESTED (object);
  MetaBackendX11NestedClass *backend_x11_nested_class =
    META_BACKEND_X11_NESTED_GET_CLASS (backend_x11_nested);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (meta_backend_x11_nested_parent_class);

  parent_class->constructed (object);

  backend_x11_nested_class->init_gpus (backend_x11_nested);
}

static void
meta_backend_x11_nested_init (MetaBackendX11Nested *backend_x11_nested)
{
}

static void
meta_backend_x11_nested_class_init (MetaBackendX11NestedClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  MetaBackendX11Class *backend_x11_class = META_BACKEND_X11_CLASS (klass);

  object_class->constructed = meta_backend_x11_nested_constructed;

  backend_class->post_init = meta_backend_x11_nested_post_init;
  backend_class->create_renderer = meta_backend_x11_nested_create_renderer;
  backend_class->create_monitor_manager = meta_backend_x11_nested_create_monitor_manager;
  backend_class->create_cursor_renderer = meta_backend_x11_nested_create_cursor_renderer;
  backend_class->create_input_settings = meta_backend_x11_nested_create_input_settings;
  backend_class->update_screen_size = meta_backend_x11_nested_update_screen_size;
  backend_class->select_stage_events = meta_backend_x11_nested_select_stage_events;
  backend_class->lock_layout_group = meta_backend_x11_nested_lock_layout_group;
  backend_class->set_keymap = meta_backend_x11_nested_set_keymap;
  backend_class->is_lid_closed = meta_backend_x11_nested_is_lid_closed;

  backend_x11_class->handle_host_xevent = meta_backend_x11_nested_handle_host_xevent;
  backend_x11_class->translate_device_event = meta_backend_x11_nested_translate_device_event;

  klass->init_gpus = meta_backend_x11_nested_real_init_gpus;
}
