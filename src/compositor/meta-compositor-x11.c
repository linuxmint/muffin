/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "compositor/meta-compositor-x11.h"

#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "clutter/x11/clutter-x11.h"
#include "compositor/meta-sync-ring.h"
#include "compositor/meta-window-actor-x11.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "x11/meta-x11-display-private.h"

struct _MetaCompositorX11
{
  MetaCompositor parent;

  Window output;

  gboolean frame_has_updated_xsurfaces;
  gboolean have_x11_sync_object;

  MetaWindow *unredirected_window;

  gboolean xserver_uses_monotonic_clock;
  int64_t xserver_time_query_time_us;
  int64_t xserver_time_offset_us;

  gboolean randr_scale_disabled;
};

G_DEFINE_TYPE (MetaCompositorX11, meta_compositor_x11, META_TYPE_COMPOSITOR)

static void
process_damage (MetaCompositorX11  *compositor_x11,
                XDamageNotifyEvent *damage_xevent,
                MetaWindow         *window)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);
  MetaWindowActorX11 *window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);

  meta_window_actor_x11_process_damage (window_actor_x11, damage_xevent);

  compositor_x11->frame_has_updated_xsurfaces = TRUE;
}

void
meta_compositor_x11_process_xevent (MetaCompositorX11 *compositor_x11,
                                    XEvent            *xevent,
                                    MetaWindow        *window)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  int damage_event_base;

  damage_event_base = meta_x11_display_get_damage_event_base (x11_display);
  if (xevent->type == damage_event_base + XDamageNotify)
    {
      /*
       * Core code doesn't handle damage events, so we need to extract the
       * MetaWindow ourselves.
       */
      if (!window)
        {
          Window xwindow;

          xwindow = ((XDamageNotifyEvent *) xevent)->drawable;
          window = meta_x11_display_lookup_x_window (x11_display, xwindow);
        }

      if (window)
        process_damage (compositor_x11, (XDamageNotifyEvent *) xevent, window);
    }

  if (compositor_x11->have_x11_sync_object)
    meta_sync_ring_handle_event (xevent);

  /*
   * Clutter needs to know about MapNotify events otherwise it will think the
   * stage is invisible
   */
  if (xevent->type == MapNotify)
    meta_x11_handle_event (xevent);
}

static void
determine_server_clock_source (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  uint32_t server_time_ms;
  int64_t server_time_us;
  int64_t translated_monotonic_now_us;

  server_time_ms = meta_x11_display_get_current_time_roundtrip (x11_display);
  server_time_us = ms2us (server_time_ms);
  translated_monotonic_now_us =
    meta_translate_to_high_res_xserver_time (g_get_monotonic_time ());

  /* If the server time offset is within a second of the monotonic time, we
   * assume that they are identical. This seems like a big margin, but we want
   * to be as robust as possible even if the system is under load and our
   * processing of the server response is delayed.
   */
  if (ABS (server_time_us - translated_monotonic_now_us) < s2us (1))
    compositor_x11->xserver_uses_monotonic_clock = TRUE;
  else
    compositor_x11->xserver_uses_monotonic_clock = FALSE;
}

static void
meta_compositor_x11_manage (MetaCompositor *compositor)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  MetaBackend *backend = meta_get_backend ();
  Window xwindow;

  determine_server_clock_source (compositor_x11);

  meta_x11_display_set_cm_selection (display->x11_display);

  compositor_x11->output = display->x11_display->composite_overlay_window;

  xwindow = meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend));

  XReparentWindow (xdisplay, xwindow, compositor_x11->output, 0, 0);

  meta_x11_display_clear_stage_input_region (display->x11_display);

  /*
   * Make sure there isn't any left-over output shape on the overlay window by
   * setting the whole screen to be an output region.
   *
   * Note: there doesn't seem to be any real chance of that because the X
   * server will destroy the overlay window when the last client using it
   * exits.
   */
  XFixesSetWindowShapeRegion (xdisplay, compositor_x11->output,
                              ShapeBounding, 0, 0, None);

  /*
   * Map overlay window before redirecting windows offscreen so we catch their
   * contents until we show the stage.
   */
  XMapWindow (xdisplay, compositor_x11->output);

  compositor_x11->have_x11_sync_object = meta_sync_ring_init (xdisplay);

  meta_compositor_redirect_x11_windows (META_COMPOSITOR (compositor));
}

static void
meta_compositor_x11_unmanage (MetaCompositor *compositor)
{
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  Window xroot = x11_display->xroot;

  /*
   * This is the most important part of cleanup - we have to do this before
   * giving up the window manager selection or the next window manager won't be
   * able to redirect subwindows
   */
  XCompositeUnredirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
}

/*
 * Sets an bounding shape on the COW so that the given window
 * is exposed. If window is %NULL it clears the shape again.
 *
 * Used so we can unredirect windows, by shaping away the part
 * of the COW, letting the raw window be seen through below.
 */
static void
shape_cow_for_window (MetaCompositorX11 *compositor_x11,
                      MetaWindow        *window)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (!window)
    {
      XFixesSetWindowShapeRegion (xdisplay, compositor_x11->output,
                                  ShapeBounding, 0, 0, None);
    }
  else
    {
      XserverRegion output_region;
      XRectangle screen_rect, window_bounds;
      int width, height;
      MetaRectangle rect;

      meta_window_get_frame_rect (window, &rect);

      window_bounds.x = rect.x;
      window_bounds.y = rect.y;
      window_bounds.width = rect.width;
      window_bounds.height = rect.height;

      meta_display_get_size (display, &width, &height);
      screen_rect.x = 0;
      screen_rect.y = 0;
      screen_rect.width = width;
      screen_rect.height = height;

      output_region = XFixesCreateRegion (xdisplay, &window_bounds, 1);

      XFixesInvertRegion (xdisplay, output_region, &screen_rect, output_region);
      XFixesSetWindowShapeRegion (xdisplay, compositor_x11->output,
                                  ShapeBounding, 0, 0, output_region);
      XFixesDestroyRegion (xdisplay, output_region);
    }
}

static void
on_redirected_monitor_changed (MetaWindow        *window,
                               int                old_monitor,
                               MetaCompositorX11 *compositor_x11)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (old_monitor >= 0 && window->monitor &&
      window->monitor->number != old_monitor)
    {
      g_signal_handlers_block_by_func (window,
                                       on_redirected_monitor_changed,
                                       compositor_x11);

      if (!compositor_x11->randr_scale_disabled)
        {
          compositor_x11->randr_scale_disabled =
            meta_monitor_manager_disable_scale_for_monitor (monitor_manager,
                                                            window->monitor);
        }

      g_signal_handlers_unblock_by_func (window,
                                         on_redirected_monitor_changed,
                                         compositor_x11);
    }
  else
    shape_cow_for_window (META_COMPOSITOR_X11 (compositor_x11), window);
}

static MetaWindow *
get_unredirectable_window (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaWindowActor *window_actor;
  MetaWindowActorX11 *window_actor_x11;

  window_actor = meta_compositor_get_top_window_actor (compositor);
  if (!window_actor)
    return NULL;

  window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);
  if (!meta_window_actor_x11_should_unredirect (window_actor_x11))
    return NULL;

  return meta_window_actor_get_meta_window (window_actor);
}

static void
set_unredirected_window (MetaCompositorX11 *compositor_x11,
                         MetaWindow        *window)
{
  MetaBackend *backend;
  MetaMonitorManager *monitor_manager;
  MetaWindow *prev_unredirected_window = compositor_x11->unredirected_window;

  if (prev_unredirected_window == window)
    {
      if (!window && compositor_x11->randr_scale_disabled &&
          !get_unredirectable_window (compositor_x11))
        {
          backend = meta_get_backend ();
          monitor_manager = meta_backend_get_monitor_manager (backend);

          compositor_x11->randr_scale_disabled =
            meta_monitor_manager_disable_scale_for_monitor (monitor_manager,
                                                            NULL);
        }

      return;
    }

  backend = meta_get_backend ();
  monitor_manager = meta_backend_get_monitor_manager (backend);

  if (prev_unredirected_window)
    {
      MetaWindowActor *window_actor;
      MetaWindowActorX11 *window_actor_x11;

      g_signal_handlers_disconnect_by_func (prev_unredirected_window,
                                            on_redirected_monitor_changed,
                                            compositor_x11);

      window_actor = meta_window_actor_from_window (prev_unredirected_window);
      window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);
      meta_window_actor_x11_set_unredirected (window_actor_x11, FALSE);
    }

  shape_cow_for_window (compositor_x11, window);
  compositor_x11->unredirected_window = window;

  if (window)
    {
      MetaWindowActor *window_actor;
      MetaWindowActorX11 *window_actor_x11;

      if (!compositor_x11->randr_scale_disabled)
        {
          compositor_x11->randr_scale_disabled =
            meta_monitor_manager_disable_scale_for_monitor (monitor_manager,
                                                            window->monitor);
        }

      g_signal_connect_object (window, "monitor-changed",
                              G_CALLBACK (on_redirected_monitor_changed),
                              compositor_x11, 0);

      window_actor = meta_window_actor_from_window (window);
      window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);
      meta_window_actor_x11_set_unredirected (window_actor_x11, TRUE);
    }
}

static void
maybe_unredirect_top_window (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaWindow *window_to_unredirect = NULL;

  if (meta_compositor_is_unredirect_inhibited (compositor))
    goto out;

  window_to_unredirect = get_unredirectable_window (compositor_x11);

out:
  set_unredirected_window (compositor_x11, window_to_unredirect);
}

static void
meta_compositor_x11_pre_paint (MetaCompositor *compositor)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaCompositorClass *parent_class;

  maybe_unredirect_top_window (compositor_x11);

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->pre_paint (compositor);

  if (compositor_x11->frame_has_updated_xsurfaces)
    {
      MetaDisplay *display = meta_compositor_get_display (compositor);

      /*
       * We need to make sure that any X drawing that happens before the
       * XDamageSubtract() for each window above is visible to subsequent GL
       * rendering; the standardized way to do this is GL_EXT_X11_sync_object.
       * Since this isn't implemented yet in mesa, we also have a path that
       * relies on the implementation of the open source drivers.
       *
       * Anything else, we just hope for the best.
       *
       * Xorg and open source driver specifics:
       *
       * The X server makes sure to flush drawing to the kernel before sending
       * out damage events, but since we use DamageReportBoundingBox there may
       * be drawing between the last damage event and the XDamageSubtract()
       * that needs to be flushed as well.
       *
       * Xorg always makes sure that drawing is flushed to the kernel before
       * writing events or responses to the client, so any round trip request
       * at this point is sufficient to flush the GLX buffers.
       */
      if (compositor_x11->have_x11_sync_object)
        compositor_x11->have_x11_sync_object = meta_sync_ring_insert_wait ();
      else
        XSync (display->x11_display->xdisplay, False);
    }
}

static void
meta_compositor_x11_post_paint (MetaCompositor *compositor)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaCompositorClass *parent_class;

  if (compositor_x11->frame_has_updated_xsurfaces)
    {
      if (compositor_x11->have_x11_sync_object)
        compositor_x11->have_x11_sync_object = meta_sync_ring_after_frame ();

      compositor_x11->frame_has_updated_xsurfaces = FALSE;
    }

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->post_paint (compositor);
}

static void
meta_compositor_x11_remove_window (MetaCompositor *compositor,
                                   MetaWindow     *window)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaCompositorClass *parent_class;

  if (compositor_x11->unredirected_window == window)
    set_unredirected_window (compositor_x11, NULL);

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->remove_window (compositor, window);
}

static int64_t
meta_compositor_x11_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                        int64_t         monotonic_time_us)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  int64_t now_us;

  if (compositor_x11->xserver_uses_monotonic_clock)
    return meta_translate_to_high_res_xserver_time (monotonic_time_us);

  now_us = g_get_monotonic_time ();

  if (compositor_x11->xserver_time_query_time_us == 0 ||
      now_us > (compositor_x11->xserver_time_query_time_us + s2us (10)))
    {
      MetaDisplay *display = meta_compositor_get_display (compositor);
      MetaX11Display *x11_display = display->x11_display;
      uint32_t xserver_time_ms;
      int64_t xserver_time_us;

      compositor_x11->xserver_time_query_time_us = now_us;

      xserver_time_ms =
        meta_x11_display_get_current_time_roundtrip (x11_display);
      xserver_time_us = ms2us (xserver_time_ms);
      compositor_x11->xserver_time_offset_us = xserver_time_us - now_us;
    }

  return monotonic_time_us + compositor_x11->xserver_time_offset_us;
}

Window
meta_compositor_x11_get_output_xwindow (MetaCompositorX11 *compositor_x11)
{
  return compositor_x11->output;
}

MetaCompositorX11 *
meta_compositor_x11_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_COMPOSITOR_X11,
                       "display", display,
                       NULL);
}

static void
meta_compositor_x11_dispose (GObject *object)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (object);

  if (compositor_x11->have_x11_sync_object)
    {
      meta_sync_ring_destroy ();
      compositor_x11->have_x11_sync_object = FALSE;
    }

  G_OBJECT_CLASS (meta_compositor_x11_parent_class)->dispose (object);
}

static void
meta_compositor_x11_init (MetaCompositorX11 *compositor_x11)
{
}

static void
meta_compositor_x11_class_init (MetaCompositorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCompositorClass *compositor_class = META_COMPOSITOR_CLASS (klass);

  object_class->dispose = meta_compositor_x11_dispose;

  compositor_class->manage = meta_compositor_x11_manage;
  compositor_class->unmanage = meta_compositor_x11_unmanage;
  compositor_class->pre_paint = meta_compositor_x11_pre_paint;
  compositor_class->post_paint = meta_compositor_x11_post_paint;
  compositor_class->remove_window = meta_compositor_x11_remove_window;
  compositor_class->monotonic_to_high_res_xserver_time =
   meta_compositor_x11_monotonic_to_high_res_xserver_time;
}
