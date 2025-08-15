/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:compositor
 * @Title: MetaCompositor
 * @Short_Description: Compositor API
 *
 * At a high-level, a window is not-visible or visible. When a
 * window is added (with meta_compositor_add_window()) it is not visible.
 * meta_compositor_show_window() indicates a transition from not-visible to
 * visible. Some of the reasons for this:
 *
 * - Window newly created
 * - Window is unminimized
 * - Window is moved to the current desktop
 * - Window was made sticky
 *
 * meta_compositor_hide_window() indicates that the window has transitioned from
 * visible to not-visible. Some reasons include:
 *
 * - Window was destroyed
 * - Window is minimized
 * - Window is moved to a different desktop
 * - Window no longer sticky.
 *
 * Note that combinations are possible - a window might have first
 * been minimized and then moved to a different desktop. The 'effect' parameter
 * to meta_compositor_show_window() and meta_compositor_hide_window() is a hint
 * as to the appropriate effect to show the user and should not
 * be considered to be indicative of a state change.
 *
 * When the active workspace is changed, meta_compositor_switch_workspace() is
 * called first, then meta_compositor_show_window() and
 * meta_compositor_hide_window() are called individually for each window
 * affected, with an effect of META_COMP_EFFECT_NONE.
 * If hiding windows will affect the switch workspace animation, the
 * compositor needs to delay hiding the windows until the switch
 * workspace animation completes.
 *
 * # Containers #
 *
 * There's two containers in the stage that are used to place window actors, here
 * are listed in the order in which they are painted:
 *
 * - window group, accessible with meta_get_window_group_for_display()
 * - top window group, accessible with meta_get_top_window_group_for_display()
 *
 * Mutter will place actors representing windows in the window group, except for
 * override-redirect windows (ie. popups and menus) which will be placed in the
 * top window group.
 */

#include "config.h"

#include "compositor/compositor-private.h"

#include <X11/extensions/Xcomposite.h>

#include "backends/meta-dnd-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "clutter/clutter-muffin.h"
#include "cogl/cogl.h"
#include "compositor/meta-later-private.h"
#include "compositor/meta-window-actor-x11.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-group-private.h"
#include "core/display-private.h"
#include "core/frame.h"
#include "core/util-private.h"
#include "core/window-private.h"
#include "core/meta-workspace-manager-private.h"
#include "meta/compositor-muffin.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "meta/meta-background-actor.h"
#include "meta/meta-background-group.h"
#include "meta/meta-shadow-factory.h"
#include "meta/meta-x11-errors.h"
#include "meta/meta-x11-background-actor.h"
#include "meta/prefs.h"
#include "meta/window.h"
#include "x11/meta-x11-display-private.h"

#ifdef HAVE_WAYLAND
#include "compositor/meta-window-actor-wayland.h"
#include "wayland/meta-wayland-private.h"
#endif

enum
{
  PROP_0,

  PROP_DISPLAY,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { NULL, };

enum
{
  PRE_PAINT,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaCompositorPrivate
{
  GObject parent;

  MetaDisplay *display;

  guint pre_paint_func_id;
  guint post_paint_func_id;

  gulong stage_presented_id;
  gulong stage_after_paint_id;

  ClutterActor *stage;

  ClutterActor *window_group;
  ClutterActor *top_window_group;
  ClutterActor *feedback_group;
  ClutterActor *bottom_window_group;

  ClutterActor *background_actor;
  ClutterActor *desklet_container;

  GList *windows;

  CoglContext *context;

  MetaWindowActor *top_window_actor;
  gulong top_window_actor_destroy_id;

  int disable_unredirect_count;

  int switch_workspace_in_progress;

  MetaPluginManager *plugin_mgr;
  MetaLaters *laters;
} MetaCompositorPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaCompositor, meta_compositor,
                                     G_TYPE_OBJECT)

static void
on_presented (ClutterStage     *stage,
              CoglFrameEvent    event,
              ClutterFrameInfo *frame_info,
              MetaCompositor   *compositor);

static void
on_top_window_actor_destroyed (MetaWindowActor *window_actor,
                               MetaCompositor  *compositor);

static gboolean
is_modal (MetaDisplay *display)
{
  return display->event_route == META_EVENT_ROUTE_COMPOSITOR_GRAB;
}

static void sync_actor_stacking (MetaCompositor *compositor);

static void
meta_finish_workspace_switch (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;

  /* Finish hiding and showing actors for the new workspace */
  for (l = priv->windows; l; l = l->next)
    meta_window_actor_sync_visibility (l->data);

  /* Fix up stacking order. */
  sync_actor_stacking (compositor);
}

void
meta_switch_workspace_completed (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  /* FIXME -- must redo stacking order */
  priv->switch_workspace_in_progress--;
  if (priv->switch_workspace_in_progress < 0)
    {
      g_warning ("Error in workspace_switch accounting!");
      priv->switch_workspace_in_progress = 0;
    }

  if (!priv->switch_workspace_in_progress)
    meta_finish_workspace_switch (compositor);
}

void
meta_compositor_destroy (MetaCompositor *compositor)
{
  g_object_run_dispose (G_OBJECT (compositor));
  g_object_unref (compositor);
}

/* compat helper */
static MetaCompositor *
get_compositor_for_display (MetaDisplay *display)
{
  return display->compositor;
}

/**
 * meta_get_stage_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The #ClutterStage for the display
 */
ClutterActor *
meta_get_stage_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->stage;
}

/**
 * meta_get_window_group_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The window group corresponding to @display
 */
ClutterActor *
meta_get_window_group_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->window_group;
}

/**
 * meta_get_top_window_group_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The top window group corresponding to @display
 */
ClutterActor *
meta_get_top_window_group_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->top_window_group;
}

/**
 * meta_get_feedback_group_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The feedback group corresponding to @display
 */
ClutterActor *
meta_get_feedback_group_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->feedback_group;
}

/**
 * meta_get_bottom_window_group_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The bottom window group corresponding to @display
 */
ClutterActor *
meta_get_bottom_window_group_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->bottom_window_group;
}

/**
 * meta_get_window_actors:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none) (element-type Clutter.Actor): The set of #MetaWindowActor on @display
 */
GList *
meta_get_window_actors (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->windows;
}

void
meta_focus_stage_window (MetaDisplay *display,
                         guint32      timestamp)
{
  ClutterStage *stage;
  Window window;

  stage = CLUTTER_STAGE (meta_get_stage_for_display (display));
  if (!stage)
    return;

  window = meta_x11_get_stage_window (stage);

  if (window == None)
    return;

  meta_x11_display_set_input_focus_xwindow (display->x11_display,
                                            window,
                                            timestamp);
}

gboolean
meta_stage_is_focused (MetaDisplay *display)
{
  ClutterStage *stage;
  Window window;

  if (meta_is_wayland_compositor ())
    return TRUE;

  stage = CLUTTER_STAGE (meta_get_stage_for_display (display));
  if (!stage)
    return FALSE;

  window = meta_x11_get_stage_window (stage);

  if (window == None)
    return FALSE;

  return (display->x11_display->focus_xwindow == window);
}

static gboolean
grab_devices (MetaModalOptions  options,
              guint32           timestamp)
{
  MetaBackend *backend = META_BACKEND (meta_get_backend ());
  gboolean pointer_grabbed = FALSE;
  gboolean keyboard_grabbed = FALSE;

  if ((options & META_MODAL_POINTER_ALREADY_GRABBED) == 0)
    {
      if (!meta_backend_grab_device (backend, META_VIRTUAL_CORE_POINTER_ID, timestamp))
        goto fail;

      pointer_grabbed = TRUE;
    }

  if ((options & META_MODAL_KEYBOARD_ALREADY_GRABBED) == 0)
    {
      if (!meta_backend_grab_device (backend, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp))
        goto fail;

      keyboard_grabbed = TRUE;
    }

  return TRUE;

 fail:
  if (pointer_grabbed)
    meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_POINTER_ID, timestamp);
  if (keyboard_grabbed)
    meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp);

  return FALSE;
}

gboolean
meta_begin_modal_for_plugin (MetaCompositor   *compositor,
                             MetaPlugin       *plugin,
                             MetaModalOptions  options,
                             guint32           timestamp)
{
  /* To some extent this duplicates code in meta_display_begin_grab_op(), but there
   * are significant differences in how we handle grabs that make it difficult to
   * merge the two.
   */
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaDisplay *display = priv->display;

#ifdef HAVE_WAYLAND
  if (display->grab_op == META_GRAB_OP_WAYLAND_POPUP)
    {
      MetaWaylandSeat *seat = meta_wayland_compositor_get_default ()->seat;
      meta_wayland_pointer_end_popup_grab (seat->pointer);
    }
#endif

  if (is_modal (display) || display->grab_op != META_GRAB_OP_NONE)
    return FALSE;

  if (display->x11_display)
    {
      /* XXX: why is this needed? */
      XIUngrabDevice (display->x11_display->xdisplay,
                      META_VIRTUAL_CORE_POINTER_ID,
                      timestamp);
      XSync (display->x11_display->xdisplay, False);
    }

  if (!grab_devices (options, timestamp))
    return FALSE;

  display->grab_op = META_GRAB_OP_COMPOSITOR;
  display->event_route = META_EVENT_ROUTE_COMPOSITOR_GRAB;
  display->grab_window = NULL;
  display->grab_have_pointer = TRUE;
  display->grab_have_keyboard = TRUE;

  g_signal_emit_by_name (display, "grab-op-begin",
                         meta_plugin_get_display (plugin),
                         display->grab_window, display->grab_op);

  if (meta_is_wayland_compositor ())
    {
      meta_display_sync_wayland_input_focus (display);
      meta_display_cancel_touch (display);

#ifdef HAVE_WAYLAND
      meta_dnd_wayland_handle_begin_modal (compositor);
#endif
    }

  return TRUE;
}

void
meta_end_modal_for_plugin (MetaCompositor *compositor,
                           MetaPlugin     *plugin,
                           guint32         timestamp)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaDisplay *display = priv->display;
  MetaBackend *backend = meta_get_backend ();
  MetaWindow *grab_window = display->grab_window;
  MetaGrabOp grab_op = display->grab_op;

  g_return_if_fail (is_modal (display));

  display->grab_op = META_GRAB_OP_NONE;
  display->event_route = META_EVENT_ROUTE_NORMAL;
  display->grab_window = NULL;
  display->grab_have_pointer = FALSE;
  display->grab_have_keyboard = FALSE;

  meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_POINTER_ID, timestamp);
  meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp);

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
      meta_dnd_wayland_handle_end_modal (compositor);
      meta_display_sync_wayland_input_focus (display);
    }
#endif

  g_signal_emit_by_name (display, "grab-op-end",
                         meta_plugin_get_display (plugin),
                         grab_window, grab_op);
}

static void
after_stage_paint (ClutterStage *stage,
                   gpointer      data)
{
  MetaCompositor *compositor = data;
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;

  for (l = priv->windows; l; l = l->next)
    meta_window_actor_post_paint (l->data);
}

static void
redirect_windows (MetaX11Display *x11_display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  Window xroot = meta_x11_display_get_xroot (x11_display);
  int screen_number = meta_x11_display_get_screen_number (x11_display);
  guint n_retries;
  guint max_retries;

  if (meta_get_replace_current_wm ())
    max_retries = 5;
  else
    max_retries = 1;

  n_retries = 0;

  /* Some compositors (like old versions of Mutter) might not properly unredirect
   * subwindows before destroying the WM selection window; so we wait a while
   * for such a compositor to exit before giving up.
   */
  while (TRUE)
    {
      meta_x11_error_trap_push (x11_display);
      XCompositeRedirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
      XSync (xdisplay, FALSE);

      if (!meta_x11_error_trap_pop_with_return (x11_display))
        break;

      if (n_retries == max_retries)
        {
          /* This probably means that a non-WM compositor like xcompmgr is running;
           * we have no way to get it to exit */
          meta_fatal (_("Another compositing manager is already running on screen %i on display “%s”."),
                      screen_number, x11_display->name);
        }

      n_retries++;
      g_usleep (G_USEC_PER_SEC);
    }
}

void
meta_compositor_redirect_x11_windows (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaDisplay *display = priv->display;

  if (display->x11_display)
    redirect_windows (display->x11_display);
}

gboolean
meta_compositor_do_manage (MetaCompositor  *compositor,
                           GError         **error)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaDisplay *display = priv->display;
  MetaBackend *backend = meta_get_backend ();

  priv->stage = meta_backend_get_stage (backend);

  priv->stage_presented_id =
    g_signal_connect (priv->stage, "presented",
                      G_CALLBACK (on_presented),
                      compositor);

  /* We use connect_after() here to accomodate code in GNOME Shell that,
   * when benchmarking drawing performance, connects to ::after-paint
   * and calls glFinish(). The timing information from that will be
   * more accurate if we hold off until that completes before we signal
   * apps to begin drawing the next frame. If there are no other
   * connections to ::after-paint, connect() vs. connect_after() doesn't
   * matter.
   */
  priv->stage_after_paint_id =
    g_signal_connect_after (priv->stage, "after-paint",
                            G_CALLBACK (after_stage_paint), compositor);

  clutter_stage_set_sync_delay (CLUTTER_STAGE (priv->stage), META_SYNC_DELAY);

  priv->window_group = meta_window_group_new (display);
  priv->top_window_group = meta_window_group_new (display);
  priv->bottom_window_group = meta_window_group_new (display);
  priv->feedback_group = meta_window_group_new (display);

  if (!meta_is_wayland_compositor ())
    {
      priv->background_actor = meta_x11_background_actor_new_for_display (display);
      clutter_actor_add_child (priv->window_group, priv->background_actor);
    }

  clutter_actor_add_child (priv->window_group, priv->bottom_window_group);

  // This needs to remain stacked just above the background actor in the window group.
  // So sync_actor_stacking() has to be able to reference it. The deskletManager
  // will take this and finish setting it up.
  priv->desklet_container = clutter_actor_new ();
  clutter_actor_add_child (priv->window_group, priv->desklet_container);
  clutter_actor_add_child (priv->stage, priv->window_group);
  clutter_actor_add_child (priv->stage, priv->top_window_group);
  clutter_actor_add_child (priv->stage, priv->feedback_group);

  if (!META_COMPOSITOR_GET_CLASS (compositor)->manage (compositor, error))
    return FALSE;

  priv->plugin_mgr = meta_plugin_manager_new (compositor);

  return TRUE;
}

void
meta_compositor_manage (MetaCompositor *compositor)
{
  GError *error = NULL;
  if (!meta_compositor_do_manage (compositor, &error))
    g_error ("Compositor failed to manage display: %s", error->message);
}

void
meta_compositor_unmanage (MetaCompositor *compositor)
{
  META_COMPOSITOR_GET_CLASS (compositor)->unmanage (compositor);
}

static void
ensure_tooltip_visible (MetaWindow *window)
{
  const MetaLogicalMonitor *monitor;
  MetaRectangle work_area, win_rect;
  gint new_x, new_y;

  /* Why this is here:
   * As of gtk 3.24, tooltips for GtkStatusIcons began displaying their tool tip
   * off the screen in certain situations.
   *
   * See: https://github.com/GNOME/gtk/commit/14d22cb3233e
   *
   * If the status icon is too small relative to its panel (which has been assigned
   * as a strut to muffin), tooltip positioning fails both tests in gdkwindowimpl.c
   * (maybe_flip_position()) skipping repositioning of the tooltip inside the workarea.
   * This only occurs on bottom panels, and only begins happening when the status icon
   * becomes 10px or more smaller than the panel it's *centered* on.
   *
   * Since the calculations are based upon the monitor's workarea and the status icon
   * plug window's size, there's no way to compensate for or fool gtk into displaying it
   * correctly.  So here, we do our own check and adjustment if a part of the tooltip
   * window falls outside the current monitor's work area.  This is also useful since
   * muffin knows *exactly* the work area for each monitor, whereas gtk only has
   * _NET_WORKAREA to go by, which only keeps track of (primary monitor * n_workspaces),
   * so an odd monitor layout would trip this up anyhow.
   *
   * This may cause regressions - see: https://github.com/linuxmint/muffin/commit/050038690,
   * but without being able to reproduce the issue mentioned there, we'll just have to address
   * it if it appears again as a result of this change. */

  monitor = window->monitor;

  if (!monitor)
    return;

  meta_workspace_get_work_area_for_monitor (meta_workspace_manager_get_active_workspace (window->display->workspace_manager),
                                            monitor->number,
                                            &work_area);

  meta_window_get_buffer_rect (window, &win_rect);

  new_x = win_rect.x;
  new_y = win_rect.y;

  if (win_rect.y < work_area.y)
    {
      new_y = work_area.y;
    }
  else if (win_rect.y + win_rect.height > work_area.y + work_area.height)
    {
      new_y = (work_area.y + work_area.height - win_rect.height);
    }

  if (win_rect.x < work_area.x)
    {
      new_x = work_area.x;
    }
  else if (win_rect.x + win_rect.width > work_area.x + work_area.width)
    {
      new_x = (work_area.x + work_area.width - win_rect.width);
    }

  if (new_x != win_rect.x || new_y != win_rect.y)
    {
      meta_window_move_frame (window, FALSE, new_x, new_y);
    }
}

void
meta_compositor_add_window (MetaCompositor    *compositor,
                            MetaWindow        *window)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *window_actor;
  ClutterActor *window_group;
  GType window_actor_type = G_TYPE_INVALID;

  switch (window->client_type)
    {
    case META_WINDOW_CLIENT_TYPE_X11:
      window_actor_type = META_TYPE_WINDOW_ACTOR_X11;
      break;

#ifdef HAVE_WAYLAND
    case META_WINDOW_CLIENT_TYPE_WAYLAND:
      window_actor_type = META_TYPE_WINDOW_ACTOR_WAYLAND;
      break;
#endif

    default:
      g_return_if_reached ();
    }

  window_actor = g_object_new (window_actor_type,
                               "meta-window", window,
                               "show-on-set-parent", FALSE,
                               NULL);

  if (window->layer == META_LAYER_OVERRIDE_REDIRECT)
    {
      if (window->type == META_WINDOW_TOOLTIP)
        {
          ensure_tooltip_visible (window);
        }

      window_group = priv->top_window_group;
    }

  else if (window->type == META_WINDOW_DESKTOP)
    window_group = priv->bottom_window_group;
  else
    window_group = priv->window_group;

  clutter_actor_add_child (window_group, CLUTTER_ACTOR (window_actor));

  /* Initial position in the stack is arbitrary; stacking will be synced
   * before we first paint.
   */
  priv->windows = g_list_append (priv->windows, window_actor);
  sync_actor_stacking (compositor);
}

static void
meta_compositor_real_remove_window (MetaCompositor *compositor,
                                    MetaWindow     *window)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_queue_destroy (window_actor);
}

void
meta_compositor_remove_window (MetaCompositor *compositor,
                               MetaWindow     *window)
{
  META_COMPOSITOR_GET_CLASS (compositor)->remove_window (compositor, window);
}

void
meta_compositor_remove_window_actor (MetaCompositor  *compositor,
                                     MetaWindowActor *window_actor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  priv->windows = g_list_remove (priv->windows, window_actor);
}

void
meta_compositor_sync_updates_frozen (MetaCompositor *compositor,
                                     MetaWindow     *window)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_sync_updates_frozen (window_actor);
}

void
meta_compositor_queue_frame_drawn (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   gboolean        no_delay_frame)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_queue_frame_drawn (window_actor, no_delay_frame);
}

void
meta_compositor_window_shape_changed (MetaCompositor *compositor,
                                      MetaWindow     *window)
{
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (window);
  if (!window_actor)
    return;
}

void
meta_compositor_window_opacity_changed (MetaCompositor *compositor,
                                        MetaWindow     *window)
{
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (window);
  if (!window_actor)
    return;

  meta_window_actor_update_opacity (window_actor);
}

gboolean
meta_compositor_filter_keybinding (MetaCompositor *compositor,
                                   MetaKeyBinding *binding)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return meta_plugin_manager_filter_keybinding (priv->plugin_mgr, binding);
}

void
meta_compositor_show_window (MetaCompositor *compositor,
                             MetaWindow     *window,
                             MetaCompEffect  effect)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_show (window_actor, effect);
}

void
meta_compositor_hide_window (MetaCompositor *compositor,
                             MetaWindow     *window,
                             MetaCompEffect  effect)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_hide (window_actor, effect);
  meta_stack_tracker_queue_sync_stack (priv->display->stack_tracker);
}

void
meta_compositor_size_change_window (MetaCompositor    *compositor,
                                    MetaWindow        *window,
                                    MetaSizeChange     which_change,
                                    MetaRectangle     *old_frame_rect,
                                    MetaRectangle     *old_buffer_rect)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_size_change (window_actor, which_change, old_frame_rect, old_buffer_rect);
}

void
meta_compositor_switch_workspace (MetaCompositor     *compositor,
                                  MetaWorkspace      *from,
                                  MetaWorkspace      *to,
                                  MetaMotionDirection direction)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  gint to_indx, from_indx;

  to_indx   = meta_workspace_index (to);
  from_indx = meta_workspace_index (from);

  priv->switch_workspace_in_progress++;

  if (!meta_plugin_manager_switch_workspace (priv->plugin_mgr,
                                             from_indx,
                                             to_indx,
                                             direction))
    {
      priv->switch_workspace_in_progress--;

      /* We have to explicitely call this to fix up stacking order of the
       * actors; this is because the abs stacking position of actors does not
       * necessarily change during the window hiding/unhiding, only their
       * relative position toward the destkop window.
       */
      meta_finish_workspace_switch (compositor);
    }
}

static void
sync_actor_stacking (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *children;
  GList *expected_window_node;
  GList *tmp;
  GList *old;
  GList *backgrounds;
  gboolean has_windows;
  gboolean reordered;

  /* NB: The first entries in the lists are stacked the lowest */

  /* Restacking will trigger full screen redraws, so it's worth a
   * little effort to make sure we actually need to restack before
   * we go ahead and do it */

  children = clutter_actor_get_children (priv->window_group);
  has_windows = FALSE;
  reordered = FALSE;

  /* We allow for actors in the window group other than the actors we
   * know about, but it's up to a plugin to try and keep them stacked correctly
   * (we really need extra API to make that reliable.)
   */

  /* First we collect a list of all backgrounds, and check if they're at the
   * bottom. Then we check if the window actors are in the correct sequence */
  backgrounds = NULL;
  expected_window_node = priv->windows;
  for (old = children; old != NULL; old = old->next)
    {
      ClutterActor *actor = old->data;

      if (META_IS_BACKGROUND_GROUP (actor) ||
          META_IS_BACKGROUND_ACTOR (actor) ||
          META_IS_X11_BACKGROUND_ACTOR (actor))
        {
          backgrounds = g_list_prepend (backgrounds, actor);

          if (has_windows)
            reordered = TRUE;
        }
      else if (META_IS_WINDOW_ACTOR (actor) && !reordered)
        {
          has_windows = TRUE;

          if (expected_window_node != NULL && actor == expected_window_node->data)
            expected_window_node = expected_window_node->next;
          else
            reordered = TRUE;
        }
    }

  g_clear_pointer (&children, g_list_free);

  if (!reordered)
    {
      g_list_free (backgrounds);
      return;
    }

  /* reorder the actors by lowering them in turn to the bottom of the stack.
   * windows first, then background.
   *
   * We reorder the actors even if they're not parented to the window group,
   * to allow stacking to work with intermediate actors (eg during effects)
   */
  for (tmp = g_list_last (priv->windows); tmp != NULL; tmp = tmp->prev)
    {
      ClutterActor *actor = tmp->data, *parent;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }

  // Place the desklet container above or below windows.
  if (priv->display->desklets_above)
    {
      clutter_actor_set_child_above_sibling (priv->window_group, priv->desklet_container, NULL);
    }
  else
    {
      clutter_actor_set_child_below_sibling (priv->window_group, priv->desklet_container, NULL);
    }

  // Then the bottom window group (which META_WINDOW_DESKTOP windows like nemo-desktop's get placed in).
  clutter_actor_set_child_below_sibling (priv->window_group, priv->bottom_window_group, NULL);

  if (meta_is_wayland_compositor ())
    {
      children = clutter_actor_get_children (priv->bottom_window_group);
      for (tmp = children; tmp != NULL; tmp = tmp->next)
        {
          MetaWindowActor *child = tmp->data;
          MetaWindow *mw = meta_window_actor_get_meta_window (child);

          if (mw != NULL)
            {
              // CsdBackground manager sets _NET_WM_STATE_BELOW (gtk_window_set_keep_below)
              // This sets its stack layer to META_LAYER_BOTTOM, so we can keep these below
              // the nemo-desktop, etc..
              MetaStackLayer layer = meta_window_get_default_layer (mw);

              if (layer == META_LAYER_BOTTOM)
                {
                  clutter_actor_set_child_below_sibling (priv->bottom_window_group, CLUTTER_ACTOR (child), NULL);
                }
            }
        }

        g_list_free (children);
    }

  // and finally backgrounds..

  /* we prepended the backgrounds above so the last actor in the list
   * should get lowered to the bottom last.
   */
  for (tmp = backgrounds; tmp != NULL; tmp = tmp->next)
    {
      ClutterActor *actor = tmp->data, *parent;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }
  g_list_free (backgrounds);
}

/*
 * Find the top most window that is visible on the screen. The intention of
 * this is to avoid offscreen windows that isn't actually part of the visible
 * desktop (such as the UI frames override redirect window).
 */
static MetaWindowActor *
get_top_visible_window_actor (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;

  for (l = g_list_last (priv->windows); l; l = l->prev)
    {
      MetaWindowActor *window_actor = l->data;
      MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
      MetaRectangle buffer_rect;
      MetaRectangle display_rect = { 0 };

      if (!window->visible_to_compositor)
        continue;

      meta_window_get_buffer_rect (window, &buffer_rect);
      meta_display_get_size (priv->display,
                             &display_rect.width, &display_rect.height);

      if (meta_rectangle_overlap (&display_rect, &buffer_rect))
        return window_actor;
    }

  return NULL;
}

static void
on_top_window_actor_destroyed (MetaWindowActor *window_actor,
                               MetaCompositor  *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  priv->top_window_actor = NULL;
  priv->top_window_actor_destroy_id = 0;
  priv->windows = g_list_remove (priv->windows, window_actor);

  meta_stack_tracker_queue_sync_stack (priv->display->stack_tracker);
}

void
meta_compositor_sync_stack (MetaCompositor  *compositor,
                            GList           *stack)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *top_window_actor;
  GList *old_stack;

  /* This is painful because hidden windows that we are in the process
   * of animating out of existence. They'll be at the bottom of the
   * stack of X windows, but we want to leave them in their old position
   * until the animation effect finishes.
   */

  /* Sources: first window is the highest */
  stack = g_list_copy (stack); /* The new stack of MetaWindow */
  old_stack = g_list_reverse (priv->windows); /* The old stack of MetaWindowActor */
  priv->windows = NULL;

  while (TRUE)
    {
      MetaWindowActor *old_actor = NULL, *stack_actor = NULL, *actor;
      MetaWindow *old_window = NULL, *stack_window = NULL, *window;

      /* Find the remaining top actor in our existing stack (ignoring
       * windows that have been hidden and are no longer animating) */
      while (old_stack)
        {
          old_actor = old_stack->data;
          old_window = meta_window_actor_get_meta_window (old_actor);

          if ((old_window->hidden || old_window->unmanaging) &&
              !meta_window_actor_effect_in_progress (old_actor))
            {
              old_stack = g_list_delete_link (old_stack, old_stack);
              old_actor = NULL;
            }
          else
            break;
        }

      /* And the remaining top actor in the new stack */
      while (stack)
        {
          stack_window = stack->data;
          stack_actor = meta_window_actor_from_window (stack_window);
          if (!stack_actor)
            {
              meta_verbose ("Failed to find corresponding MetaWindowActor "
                            "for window %s\n", meta_window_get_description (stack_window));
              stack = g_list_delete_link (stack, stack);
            }
          else
            break;
        }

      if (!old_actor && !stack_actor) /* Nothing more to stack */
        break;

      /* We usually prefer the window in the new stack, but if if we
       * found a hidden window in the process of being animated out
       * of existence in the old stack we use that instead. We've
       * filtered out non-animating hidden windows above.
       */
      if (old_actor &&
          (!stack_actor || old_window->hidden || old_window->unmanaging))
        {
          actor = old_actor;
          window = old_window;
        }
      else
        {
          actor = stack_actor;
          window = stack_window;
        }

      /* OK, we know what actor we want next. Add it to our window
       * list, and remove it from both source lists. (It will
       * be at the front of at least one, hopefully it will be
       * near the front of the other.)
       */
      priv->windows = g_list_prepend (priv->windows, actor);

      stack = g_list_remove (stack, window);
      old_stack = g_list_remove (old_stack, actor);
    }

  sync_actor_stacking (compositor);

  top_window_actor = get_top_visible_window_actor (compositor);

  if (priv->top_window_actor == top_window_actor)
    return;

  g_clear_signal_handler (&priv->top_window_actor_destroy_id,
                          priv->top_window_actor);

  priv->top_window_actor = top_window_actor;

  if (priv->top_window_actor)
    priv->top_window_actor_destroy_id =
      g_signal_connect (priv->top_window_actor, "destroy",
                        G_CALLBACK (on_top_window_actor_destroyed),
                        compositor);
}

void
meta_compositor_sync_window_geometry (MetaCompositor *compositor,
                                      MetaWindow     *window,
                                      gboolean        did_placement)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);
  MetaWindowActorChanges changes;

  changes = meta_window_actor_sync_actor_geometry (window_actor, did_placement);

  if (changes & META_WINDOW_ACTOR_CHANGE_SIZE)
    meta_plugin_manager_event_size_changed (priv->plugin_mgr, window_actor);
}

static void
on_presented (ClutterStage     *stage,
              CoglFrameEvent    event,
              ClutterFrameInfo *frame_info,
              MetaCompositor   *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;

  if (event == COGL_FRAME_EVENT_COMPLETE)
    {
      gint64 presentation_time_cogl = frame_info->presentation_time;
      gint64 presentation_time;

      if (presentation_time_cogl != 0)
        {
          /* Cogl reports presentation in terms of its own clock, which is
           * guaranteed to be in nanoseconds but with no specified base. The
           * normal case with the open source GPU drivers on Linux 3.8 and
           * newer is that the base of cogl_get_clock_time() is that of
           * clock_gettime(CLOCK_MONOTONIC), so the same as g_get_monotonic_time),
           * but there's no exposure of that through the API. clock_gettime()
           * is fairly fast, so calling it twice and subtracting to get a
           * nearly-zero number is acceptable, if a litle ugly.
           */
          gint64 current_cogl_time = cogl_get_clock_time (priv->context);
          gint64 current_monotonic_time = g_get_monotonic_time ();

          presentation_time =
            current_monotonic_time + (presentation_time_cogl - current_cogl_time) / 1000;
        }
      else
        {
          presentation_time = 0;
        }

      for (l = priv->windows; l; l = l->next)
        meta_window_actor_frame_complete (l->data, frame_info, presentation_time);
    }
}

static void
meta_compositor_real_pre_paint (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;

  for (l = priv->windows; l; l = l->next)
    meta_window_actor_pre_paint (l->data);
}

static void
meta_compositor_pre_paint (MetaCompositor *compositor)
{
  COGL_TRACE_BEGIN_SCOPED (MetaCompositorPrePaint,
                           "Compositor (pre-paint)");

  g_signal_emit (compositor, signals[PRE_PAINT], 0);
}

static gboolean
meta_pre_paint_func (gpointer data)
{
  MetaCompositor *compositor = data;

  meta_compositor_pre_paint (compositor);

  return TRUE;
}

static void
meta_compositor_real_post_paint (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  CoglGraphicsResetStatus status;

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    meta_wayland_compositor_paint_finished (meta_wayland_compositor_get_default ());
#endif

  status = cogl_get_graphics_reset_status (priv->context);
  switch (status)
    {
    case COGL_GRAPHICS_RESET_STATUS_NO_ERROR:
      break;

    case COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET:
      g_signal_emit_by_name (priv->display, "gl-video-memory-purged");
      clutter_actor_queue_redraw (CLUTTER_ACTOR (priv->stage));
      break;

    default:
      /* The ARB_robustness spec says that, on error, the application
         should destroy the old context and create a new one. Since we
         don't have the necessary plumbing to do this we'll simply
         restart the process. Obviously we can't do this when we are
         a wayland compositor but in that case we shouldn't get here
         since we don't enable robustness in that case. */
      g_assert (!meta_is_wayland_compositor ());
      meta_restart (NULL);
      break;
    }
}

static void
meta_compositor_post_paint (MetaCompositor *compositor)
{
  COGL_TRACE_BEGIN_SCOPED (MetaCompositorPostPaint,
                           "Compositor (post-paint)");
  META_COMPOSITOR_GET_CLASS (compositor)->post_paint (compositor);
}

static gboolean
meta_post_paint_func (gpointer data)
{
  MetaCompositor *compositor = data;

  meta_compositor_post_paint (compositor);

  return TRUE;
}

static void
meta_compositor_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_compositor_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_compositor_init (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);

  priv->context = clutter_backend->cogl_context;

  priv->pre_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT,
                                           meta_pre_paint_func,
                                           compositor,
                                           NULL);
  priv->post_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                           meta_post_paint_func,
                                           compositor,
                                           NULL);

  priv->laters = meta_laters_new (compositor);
}

static void
meta_compositor_dispose (GObject *object)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  g_clear_pointer (&priv->laters, meta_laters_free);

  g_clear_signal_handler (&priv->stage_after_paint_id, priv->stage);
  g_clear_signal_handler (&priv->stage_presented_id, priv->stage);

  g_clear_handle_id (&priv->pre_paint_func_id,
                     clutter_threads_remove_repaint_func);
  g_clear_handle_id (&priv->post_paint_func_id,
                     clutter_threads_remove_repaint_func);

  g_clear_signal_handler (&priv->top_window_actor_destroy_id,
                          priv->top_window_actor);

  g_clear_pointer (&priv->background_actor, clutter_actor_destroy);
  g_clear_pointer (&priv->bottom_window_group, clutter_actor_destroy);
  g_clear_pointer (&priv->desklet_container, clutter_actor_destroy);
  g_clear_pointer (&priv->window_group, clutter_actor_destroy);
  g_clear_pointer (&priv->top_window_group, clutter_actor_destroy);
  g_clear_pointer (&priv->feedback_group, clutter_actor_destroy);
  g_clear_pointer (&priv->windows, g_list_free);

  G_OBJECT_CLASS (meta_compositor_parent_class)->dispose (object);
}

static void
meta_compositor_class_init (MetaCompositorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_compositor_set_property;
  object_class->get_property = meta_compositor_get_property;
  object_class->dispose = meta_compositor_dispose;

  klass->remove_window = meta_compositor_real_remove_window;
  klass->pre_paint = meta_compositor_real_pre_paint;
  klass->post_paint = meta_compositor_real_post_paint;

  obj_props[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "display",
                         "MetaDisplay",
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[PRE_PAINT] =
    g_signal_new ("pre-paint",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaCompositorClass, pre_paint),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

/**
 * meta_disable_unredirect_for_display:
 * @display: a #MetaDisplay
 *
 * Disables unredirection, can be usefull in situations where having
 * unredirected windows is undesireable like when recording a video.
 *
 */
void
meta_disable_unredirect_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  if (display->closing)
    return;

  compositor = get_compositor_for_display (display);
  priv = meta_compositor_get_instance_private (compositor);

  priv->disable_unredirect_count++;
}

/**
 * meta_enable_unredirect_for_display:
 * @display: a #MetaDisplay
 *
 * Enables unredirection which reduces the overhead for apps like games.
 *
 */
void
meta_enable_unredirect_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  if (display->closing)
    return;

  compositor = get_compositor_for_display (display);
  priv = meta_compositor_get_instance_private (compositor);

  if (priv->disable_unredirect_count == 0)
    g_warning ("Called enable_unredirect_for_display while unredirection is enabled.");
  if (priv->disable_unredirect_count > 0)
    priv->disable_unredirect_count--;
}

gboolean
meta_compositor_is_unredirect_inhibited (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->disable_unredirect_count > 0;
}

#define FLASH_TIME_MS 50

static void
flash_out_completed (ClutterTimeline *timeline,
                     gboolean         is_finished,
                     gpointer         user_data)
{
  ClutterActor *flash = CLUTTER_ACTOR (user_data);
  clutter_actor_destroy (flash);
}

void
meta_compositor_flash_display (MetaCompositor *compositor,
                               MetaDisplay    *display)
{
  ClutterActor *stage;
  ClutterActor *flash;
  ClutterTransition *transition;
  gfloat width, height;

  stage = meta_get_stage_for_display (display);
  clutter_actor_get_size (stage, &width, &height);

  flash = clutter_actor_new ();
  clutter_actor_set_background_color (flash, CLUTTER_COLOR_Black);
  clutter_actor_set_size (flash, width, height);
  clutter_actor_set_opacity (flash, 0);
  clutter_actor_add_child (stage, flash);

  clutter_actor_save_easing_state (flash);
  clutter_actor_set_easing_mode (flash, CLUTTER_EASE_IN_QUAD);
  clutter_actor_set_easing_duration (flash, FLASH_TIME_MS);
  clutter_actor_set_opacity (flash, 192);

  transition = clutter_actor_get_transition (flash, "opacity");
  clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
  clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);

  g_signal_connect (transition, "stopped",
                    G_CALLBACK (flash_out_completed), flash);

  clutter_actor_restore_easing_state (flash);
}

static void
window_flash_out_completed (ClutterTimeline *timeline,
                            gboolean         is_finished,
                            gpointer         user_data)
{
  ClutterActor *flash = CLUTTER_ACTOR (user_data);
  clutter_actor_destroy (flash);
}

void
meta_compositor_flash_window (MetaCompositor *compositor,
                              MetaWindow     *window)
{
  ClutterActor *window_actor =
    CLUTTER_ACTOR (meta_window_actor_from_window (window));
  ClutterActor *flash;
  ClutterTransition *transition;

  flash = clutter_actor_new ();
  clutter_actor_set_background_color (flash, CLUTTER_COLOR_Black);
  clutter_actor_set_size (flash, window->rect.width, window->rect.height);
  clutter_actor_set_position (flash,
                              window->custom_frame_extents.left,
                              window->custom_frame_extents.top);
  clutter_actor_set_opacity (flash, 0);
  clutter_actor_add_child (window_actor, flash);

  clutter_actor_save_easing_state (flash);
  clutter_actor_set_easing_mode (flash, CLUTTER_EASE_IN_QUAD);
  clutter_actor_set_easing_duration (flash, FLASH_TIME_MS);
  clutter_actor_set_opacity (flash, 192);

  transition = clutter_actor_get_transition (flash, "opacity");
  clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
  clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);

  g_signal_connect (transition, "stopped",
                    G_CALLBACK (window_flash_out_completed), flash);

  clutter_actor_restore_easing_state (flash);
}

/**
 * meta_compositor_monotonic_to_high_res_xserver_time:
 * @display: a #MetaDisplay
 * @monotonic_time: time in the units of g_get_monotonic_time()
 *
 * _NET_WM_FRAME_DRAWN and _NET_WM_FRAME_TIMINGS messages represent time
 * as a "high resolution server time" - this is the server time interpolated
 * to microsecond resolution. The advantage of this time representation
 * is that if  X server is running on the same computer as a client, and
 * the Xserver uses 'clock_gettime(CLOCK_MONOTONIC, ...)' for the server
 * time, the client can detect this, and all such clients will share a
 * a time representation with high accuracy. If there is not a common
 * time source, then the time synchronization will be less accurate.
 */
int64_t
meta_compositor_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                   int64_t         monotonic_time_us)
{
  MetaCompositorClass *klass = META_COMPOSITOR_GET_CLASS (compositor);

  return klass->monotonic_to_high_res_xserver_time (compositor, monotonic_time_us);
}

void
meta_compositor_show_tile_preview (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   MetaRectangle  *tile_rect,
                                   int             tile_monitor_number)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_show_tile_preview (priv->plugin_mgr,
                                         window, tile_rect, tile_monitor_number);
}

void
meta_compositor_hide_tile_preview (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_hide_tile_preview (priv->plugin_mgr);
}

void
meta_compositor_show_window_menu (MetaCompositor     *compositor,
                                  MetaWindow         *window,
                                  MetaWindowMenuType  menu,
                                  int                 x,
                                  int                 y)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_show_window_menu (priv->plugin_mgr, window, menu, x, y);
}

void
meta_compositor_show_window_menu_for_rect (MetaCompositor     *compositor,
                                           MetaWindow         *window,
                                           MetaWindowMenuType  menu,
                                           MetaRectangle      *rect)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_show_window_menu_for_rect (priv->plugin_mgr, window, menu, rect);
}

MetaCloseDialog *
meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                     MetaWindow     *window)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return meta_plugin_manager_create_close_dialog (priv->plugin_mgr,
                                                  window);
}

MetaInhibitShortcutsDialog *
meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                 MetaWindow     *window)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return meta_plugin_manager_create_inhibit_shortcuts_dialog (priv->plugin_mgr,
                                                              window);
}

void
meta_compositor_locate_pointer (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_locate_pointer (priv->plugin_mgr);
}

MetaPluginManager *
meta_compositor_get_plugin_manager (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->plugin_mgr;
}

MetaDisplay *
meta_compositor_get_display (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->display;
}

ClutterStage *
meta_compositor_get_stage (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return CLUTTER_STAGE (priv->stage);
}

MetaWindowActor *
meta_compositor_get_top_window_actor (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->top_window_actor;
}

gboolean
meta_compositor_is_switching_workspace (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->switch_workspace_in_progress > 0;
}

MetaLaters *
meta_compositor_get_laters (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->laters;
}

/**
 * meta_get_x11_background_actor_for_display:
 * @display: a #MetaDisplay
 *
 * Gets the actor that draws the root window background under the windows.
 * The root window background automatically tracks the image or color set
 * by the environment.
 *
 * Returns: (transfer none): The background actor corresponding to @display
 */
ClutterActor *
meta_get_x11_background_actor_for_display (MetaDisplay *display)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (display->compositor);

  if (meta_is_wayland_compositor ())
    {
      return NULL;
    }

  return priv->background_actor;
}

/**
 * meta_get_desklet_container_for_display:
 * @display: a #MetaDisplay
 *
 * Returns the desklet container actor.
 *
 * Return value: (transfer none): The desklet container actor.
 */
ClutterActor *
meta_get_desklet_container_for_display (MetaDisplay *display)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (display->compositor);

  return priv->desklet_container;
}

void
meta_update_desklet_stacking (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_stack_tracker_queue_sync_stack (priv->display->stack_tracker);
}
