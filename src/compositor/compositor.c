/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:compositor
 * @title: MetaCompositor
 * @short_Description: Compositor API
 *
 * At a high-level, a window is not-visible or visible. When a
 * window is added (with meta_compositor_add_window()) it is not visible.
 * meta_window_actor_show() indicates a transition from not-visible to
 * visible. Some of the reasons for this:
 *
 * - Window newly created
 * - Window is unminimized
 * - Window is moved to the current desktop
 * - Window was made sticky
 *
 * meta_window_actor_hide() indicates that the window has transitioned from
 * visible to not-visible. Some reasons include:
 *
 * - Window was destroyed
 * - Window is minimized
 * - Window is moved to a different desktop
 * - Window no longer sticky.
 *
 * Note that combinations are possible - a window might have first
 * been minimized and then moved to a different desktop. The 'effect' parameter
 * to meta_window_actor_show() and meta_window_actor_hide() is a hint
 * as to the appropriate effect to show the user and should not
 * be considered to be indicative of a state change.
 *
 * When the active workspace is changed, meta_compositor_switch_workspace() is
 * called first, then meta_window_actor_show() and
 * meta_window_actor_hide() are called individually for each window
 * affected, with an effect of META_COMP_EFFECT_NONE.
 * If hiding windows will affect the switch workspace animation, the
 * compositor needs to delay hiding the windows until the switch
 * workspace animation completes.
 *
 * meta_window_actor_maximize() and meta_window_actor_unmaximize()
 * are transitions within the visible state. The window is resized __before__
 * the call, so it may be necessary to readjust the display based on the
 * old_rect to start the animation.
 *
 * # Containers #
 *
 * There's two containers in the stage that are used to place window actors, here
 * are listed in the order in which they are painted:
 *
 * - window group, accessible with meta_get_window_group_for_screen()
 * - top window group, accessible with meta_get_top_window_group_for_screen()
 *
 * Muffin will place actors representing windows in the window group, except for
 * override-redirect windows (ie. popups and menus) which will be placed in the
 * top window group.
 */

#include <config.h>

#include <clutter/x11/clutter-x11.h>

#include <meta/screen.h>
#include <meta/errors.h>
#include <meta/window.h>
#include "compositor-private.h"
#include <meta/compositor-muffin.h>
#include "xprops.h"
#include <meta/prefs.h>
#include <meta/main.h>
#include <meta/meta-shadow-factory.h>
#include "meta-window-actor-private.h"
#include "meta-window-group.h"
#include "meta-background-actor-private.h"
#include "window-private.h" /* to check window->hidden */
#include "display-private.h" /* for meta_display_lookup_x_window() */
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include "meta-sync-ring.h"

static MetaCompositor *compositor_global = NULL;

static void
frame_callback (ClutterStage     *stage,
                CoglFrameEvent    event,
                ClutterFrameInfo *frame_info,
                MetaCompositor   *compositor);

static inline gboolean
composite_at_least_version (MetaDisplay *display, int maj, int min)
{
  static int major = -1;
  static int minor = -1;

  if (major == -1)
    meta_display_get_compositor_version (display, &major, &minor);

  return (major > maj || (major == maj && minor >= min));
}

static void sync_actor_stacking (MetaCompositor *compositor);

static void
meta_finish_workspace_switch (MetaCompositor *compositor)
{
  GList *l;

  /* Finish hiding and showing actors for the new workspace */
  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_sync_visibility (l->data);

  /*
   * Fix up stacking order in case the plugin messed it up.
   */
  sync_actor_stacking (compositor);
}

LOCAL_SYMBOL void
meta_switch_workspace_completed (MetaScreen *screen)
{
  MetaCompositor *compositor = screen->display->compositor;

  /* FIXME -- must redo stacking order */
  compositor->switch_workspace_in_progress--;
  if (compositor->switch_workspace_in_progress < 0)
    {
      g_warning ("Error in workspace_switch accounting!");
      compositor->switch_workspace_in_progress = 0;
    }

  if (!compositor->switch_workspace_in_progress)
    meta_finish_workspace_switch (compositor);

  meta_compositor_set_all_obscured (compositor_global, TRUE);
}

void
meta_compositor_destroy (MetaCompositor *compositor)
{
  clutter_threads_remove_repaint_func (compositor->pre_paint_func_id);
  clutter_threads_remove_repaint_func (compositor->post_paint_func_id);

  if (compositor->have_x11_sync_object)
    meta_sync_ring_destroy ();
}

static void
process_property_notify (MetaCompositor	*compositor,
                         XPropertyEvent *event,
                         MetaWindow     *window)
{
  MetaWindowActor *window_actor;

  if (event->atom == compositor->atom_x_root_pixmap)
    {
      GSList *l;

      for (l = meta_display_get_screens (compositor->display); l; l = l->next)
        {
          MetaScreen  *screen = l->data;
          if (event->window == meta_screen_get_xroot (screen))
            {
              meta_background_actor_update (screen);
              return;
            }
        }
    }

  if (window == NULL)
    return;

  window_actor = window->compositor_private;
  if (window_actor == NULL)
    return;

  /* Check for the opacity changing */
  if (event->atom == compositor->atom_net_wm_window_opacity)
    {
      meta_window_actor_set_opacity (window_actor, 256);
      return;
    }
}

static Window
get_output_window (MetaScreen *screen)
{
  MetaDisplay *display = meta_screen_get_display (screen);
  Display     *xdisplay = meta_display_get_xdisplay (display);
  Window       output, xroot;
  XWindowAttributes attr;
  long         event_mask;

  xroot = meta_screen_get_xroot (screen);

  event_mask = FocusChangeMask |
               ExposureMask |
               EnterWindowMask | LeaveWindowMask |
	       PointerMotionMask |
               PropertyChangeMask |
               ButtonPressMask | ButtonReleaseMask |
               KeyPressMask | KeyReleaseMask;

  output = screen->composite_overlay_window;

  if (XGetWindowAttributes (xdisplay, output, &attr))
      {
        event_mask |= attr.your_event_mask;
      }

  XSelectInput (xdisplay, output, event_mask);

  return output;
}

/**
 * meta_get_stage_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The #ClutterStage for the screen
 */
ClutterActor *
meta_get_stage_for_screen (MetaScreen *screen)
{
  return screen->display->compositor->stage;
}

/**
 * meta_get_overlay_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The overlay group corresponding to @screen
 */
ClutterActor *
meta_get_overlay_group_for_screen (MetaScreen *screen)
{
  return screen->display->compositor->overlay_group;
}

/**
 * meta_get_window_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The window group corresponding to @screen
 */
ClutterActor *
meta_get_window_group_for_screen (MetaScreen *screen)
{
  return screen->display->compositor->window_group;
}

/**
 * meta_get_bottom_window_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The bottom window group corresponding to @screen
 */
ClutterActor *
meta_get_bottom_window_group_for_screen (MetaScreen *screen)
{
  return screen->display->compositor->bottom_window_group;
}

/**
 * meta_get_top_window_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The top window group corresponding to @screen
 */
ClutterActor *
meta_get_top_window_group_for_screen (MetaScreen *screen)
{
  return screen->display->compositor->top_window_group;
}

/**
 * meta_get_background_actor_for_screen:
 * @screen: a #MetaScreen
 *
 * Gets the actor that draws the root window background under the windows.
 * The root window background automatically tracks the image or color set
 * by the environment.
 *
 * Returns: (transfer none): The background actor corresponding to @screen
 */
ClutterActor *
meta_get_background_actor_for_screen (MetaScreen *screen)
{
  return screen->display->compositor->background_actor;
}

/**
 * meta_get_window_actors:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none) (element-type Clutter.Actor): The set of #MetaWindowActor on @screen
 */
GList *
meta_get_window_actors (MetaScreen *screen)
{
  return screen->display->compositor->windows;
}

static void
do_set_stage_input_region (MetaScreen   *screen,
                           XserverRegion region)
{
  MetaDisplay *display = screen->display;
  MetaCompositor *compositor = display->compositor;
  Display *xdpy = display->xdisplay;
  Window xstage = clutter_x11_get_stage_window (CLUTTER_STAGE (compositor->stage));

  XFixesSetWindowShapeRegion (xdpy, xstage, ShapeInput, 0, 0, region);

  /* It's generally a good heuristic that when a crossing event is generated because
   * we reshape the overlay, we don't want it to affect focus-follows-mouse focus -
   * it's not the user doing something, it's the environment changing under the user.
   */
  meta_display_add_ignored_crossing_serial (display, XNextRequest (xdpy));
  XFixesSetWindowShapeRegion (xdpy, compositor->output, ShapeInput, 0, 0, region);
}

void
meta_set_stage_input_region (MetaScreen   *screen,
                             XserverRegion region)
{
  MetaDisplay  *display = screen->display;
  MetaCompositor *compositor = display->compositor;
  Display *xdpy = display->xdisplay;

  if (compositor->stage && compositor->output)
    {
      do_set_stage_input_region (screen, region);
    }
  else
    {
      /* Reset compositor->pending_input_region if one existed before and set the new
       * one to use it later. */
      if (compositor->pending_input_region)
        {
          XFixesDestroyRegion (xdpy, compositor->pending_input_region);
          compositor->pending_input_region = None;
        }
      if (region != None)
        {
          compositor->pending_input_region = XFixesCreateRegion (xdpy, NULL, 0);
          XFixesCopyRegion (xdpy, compositor->pending_input_region, region);
        }
    }
}

void
meta_empty_stage_input_region (MetaScreen *screen)
{
  /* Using a static region here is a bit hacky, but Metacity never opens more than
   * one XDisplay, so it works fine. */
  static XserverRegion region = None;

  if (region == None)
    {
      MetaDisplay  *display = meta_screen_get_display (screen);
      Display      *xdpy    = meta_display_get_xdisplay (display);
      region = XFixesCreateRegion (xdpy, NULL, 0);
    }

  meta_set_stage_input_region (screen, region);
}

LOCAL_SYMBOL gboolean
meta_begin_modal_for_plugin (MetaScreen       *screen,
                             MetaPlugin       *plugin,
                             Window            grab_window,
                             Cursor            cursor,
                             MetaModalOptions  options,
                             guint32           timestamp)
{
  /* To some extent this duplicates code in meta_display_begin_grab_op(), but there
   * are significant differences in how we handle grabs that make it difficult to
   * merge the two.
   */

  MetaDisplay    *display    = meta_screen_get_display (screen);
  Display        *xdpy       = meta_display_get_xdisplay (display);
  MetaCompositor *compositor = display->compositor;
  gboolean pointer_grabbed = FALSE;
  int result;

  if (compositor->modal_plugin != NULL || display->grab_op != META_GRAB_OP_NONE)
    return FALSE;

  if ((options & META_MODAL_POINTER_ALREADY_GRABBED) == 0)
    {
      result = XGrabPointer (xdpy, grab_window,
                             False, /* owner_events */
                             (ButtonPressMask | ButtonReleaseMask |
                              EnterWindowMask | LeaveWindowMask | PointerMotionMask),
                             GrabModeAsync, GrabModeAsync,
                             None, /* confine to */
                             cursor,
                             timestamp);
      if (result != Success)
        goto fail;

      pointer_grabbed = TRUE;
    }

  if ((options & META_MODAL_KEYBOARD_ALREADY_GRABBED) == 0)
    {
      result = XGrabKeyboard (xdpy, grab_window,
                              False, /* owner_events */
                              GrabModeAsync, GrabModeAsync,
                              timestamp);

      if (result != Success)
        goto fail;
    }

  display->grab_op = META_GRAB_OP_COMPOSITOR;
  display->grab_window = NULL;
  display->grab_screen = screen;
  display->grab_have_pointer = TRUE;
  display->grab_have_keyboard = TRUE;

  compositor->modal_plugin = plugin;

  return TRUE;

 fail:
  if (pointer_grabbed)
    XUngrabPointer (xdpy, timestamp);

  return FALSE;
}

LOCAL_SYMBOL void
meta_end_modal_for_plugin (MetaScreen     *screen,
                           MetaPlugin     *plugin,
                           guint32         timestamp)
{
  MetaDisplay    *display    = meta_screen_get_display (screen);
  Display        *xdpy = meta_display_get_xdisplay (display);
  MetaCompositor *compositor = display->compositor;

  g_return_if_fail (compositor->modal_plugin == plugin);

  XUngrabPointer (xdpy, timestamp);
  XUngrabKeyboard (xdpy, timestamp);

  display->grab_op = META_GRAB_OP_NONE;
  display->grab_window = NULL;
  display->grab_screen = NULL;
  display->grab_have_pointer = FALSE;
  display->grab_have_keyboard = FALSE;

  compositor->modal_plugin = NULL;
}

/* This is used when reloading plugins to make sure we don't have
 * a left-over modal grab for this screen.
 */
LOCAL_SYMBOL void
meta_check_end_modal (MetaScreen *screen)
{
  MetaDisplay    *display    = meta_screen_get_display (screen);
  MetaCompositor *compositor = display->compositor;

  if (compositor->modal_plugin &&
      meta_plugin_get_screen (compositor->modal_plugin) == screen)
    {
      meta_end_modal_for_plugin (screen,
                                 compositor->modal_plugin,
                                 CurrentTime);
    }
}

static void
after_stage_paint (ClutterStage *stage,
                   gpointer      data)
{
  MetaCompositor *compositor = (MetaCompositor*) data;
  GList *l;

  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_post_paint (l->data);
}

static void
redirect_windows (MetaCompositor *compositor,
                  MetaScreen     *screen)
{
  MetaDisplay *display       = meta_screen_get_display (screen);
  Display     *xdisplay      = meta_display_get_xdisplay (display);
  Window       xroot         = meta_screen_get_xroot (screen);
  int          screen_number = meta_screen_get_screen_number (screen);
  guint        n_retries;
  guint        max_retries;

  if (meta_get_replace_current_wm ())
    max_retries = 5;
  else
    max_retries = 1;

  n_retries = 0;

  /* Some compositors (like old versions of Muffin) might not properly unredirect
   * subwindows before destroying the WM selection window; so we wait a while
   * for such a compositor to exit before giving up.
   */
  while (TRUE)
    {
      meta_error_trap_push_with_return (display);
      XCompositeRedirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
      XSync (xdisplay, FALSE);

      if (!meta_error_trap_pop_with_return (display))
        break;

      if (n_retries == max_retries)
        {
          /* This probably means that a non-WM compositor like xcompmgr is running;
           * we have no way to get it to exit */
          meta_fatal (_("Another compositing manager is already running on screen %i on display \"%s\"."),
                      screen_number, display->name);
        }

      n_retries++;
      g_usleep (G_USEC_PER_SEC);
    }
}

LOCAL_SYMBOL void
meta_compositor_toggle_send_frame_timings (MetaScreen *screen)
{
  MetaCompositor *compositor = screen->display->compositor;
  if (meta_prefs_get_send_frame_timings())
    {
      g_signal_connect_after (CLUTTER_STAGE (compositor->stage), "presented",
                              G_CALLBACK (frame_callback), compositor);
    }
  else
    {
      g_signal_handlers_disconnect_by_func (CLUTTER_STAGE (compositor->stage),
                                            frame_callback, NULL);
    }
}

void
meta_compositor_manage_screen (MetaCompositor *compositor,
                               MetaScreen     *screen)
{
  MetaDisplay    *display       = meta_screen_get_display (screen);
  Display        *xdisplay      = meta_display_get_xdisplay (display);
  Window          xwin;
  gint            width, height;
  XWindowAttributes attr;
  long            event_mask;

  redirect_windows (compositor, screen);

  /*
   * We use an empty input region for Clutter as a default because that allows
   * the user to interact with all the windows displayed on the screen.
   * We have to initialize compositor->pending_input_region to an empty region explicitly,
   * because None value is used to mean that the whole screen is an input region.
   */
  compositor->pending_input_region = XFixesCreateRegion (xdisplay, NULL, 0);

  compositor->screen = screen;
  compositor->output = None;
  compositor->windows = NULL;

  meta_screen_set_cm_selection (screen);

  compositor->stage = clutter_stage_new ();

  meta_compositor_toggle_send_frame_timings(screen);

  g_signal_connect_after (CLUTTER_STAGE (compositor->stage), "after-paint",
                          G_CALLBACK (after_stage_paint), compositor);

  clutter_stage_set_sync_delay (CLUTTER_STAGE (compositor->stage), META_SYNC_DELAY);

  meta_screen_get_size (screen, &width, &height);
  clutter_actor_realize (compositor->stage);

  xwin = clutter_x11_get_stage_window (CLUTTER_STAGE (compositor->stage));

  XResizeWindow (xdisplay, xwin, width, height);

  event_mask = FocusChangeMask |
               ExposureMask |
               EnterWindowMask | LeaveWindowMask |
               PointerMotionMask |
               PropertyChangeMask |
               ButtonPressMask | ButtonReleaseMask |
               KeyPressMask | KeyReleaseMask |
               StructureNotifyMask;

  if (XGetWindowAttributes (xdisplay, xwin, &attr))
      {
        event_mask |= attr.your_event_mask;
      }

  XSelectInput (xdisplay, xwin, event_mask);

  compositor->window_group = meta_window_group_new (screen);
  compositor->background_actor = meta_background_actor_new_for_screen (screen);
  compositor->bottom_window_group = clutter_actor_new();
  compositor->overlay_group = clutter_actor_new ();
  compositor->top_window_group = meta_window_group_new (screen);
  compositor->hidden_group = clutter_actor_new ();

  clutter_actor_add_child (compositor->window_group, compositor->background_actor);
  clutter_actor_add_child (compositor->stage, compositor->window_group);
  clutter_actor_add_child (compositor->stage, compositor->top_window_group);
  clutter_actor_add_child (compositor->stage, compositor->overlay_group);
  clutter_actor_add_child (compositor->stage, compositor->hidden_group);

  clutter_actor_set_flags (compositor->background_actor, CLUTTER_ACTOR_NO_LAYOUT);
  clutter_actor_set_flags (compositor->top_window_group, CLUTTER_ACTOR_NO_LAYOUT);
  clutter_actor_set_flags (compositor->overlay_group, CLUTTER_ACTOR_NO_LAYOUT);
  clutter_actor_set_flags (compositor->hidden_group, CLUTTER_ACTOR_NO_LAYOUT);

  clutter_actor_hide (compositor->hidden_group);

  compositor->plugin_mgr = meta_plugin_manager_new (screen);

  /*
   * Delay the creation of the overlay window as long as we can, to avoid
   * blanking out the screen. This means that during the plugin loading, the
   * overlay window is not accessible; if the plugin needs to access it
   * directly, it should hook into the "show" signal on stage, and do
   * its stuff there.
   */
  compositor->output = get_output_window (screen);
  XReparentWindow (xdisplay, xwin, compositor->output, 0, 0);

 /* Make sure there isn't any left-over output shape on the
  * overlay window by setting the whole screen to be an
  * output region.
  *
  * Note: there doesn't seem to be any real chance of that
  *  because the X server will destroy the overlay window
  *  when the last client using it exits.
  */
  XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, None);

  do_set_stage_input_region (screen, compositor->pending_input_region);
  if (compositor->pending_input_region != None)
    {
      XFixesDestroyRegion (xdisplay, compositor->pending_input_region);
      compositor->pending_input_region = None;
    }

  clutter_actor_show (compositor->overlay_group);
  clutter_actor_show (compositor->stage);

  /* Map overlay window before redirecting windows offscreen so we catch their
   * contents until we show the stage.
   */
  XMapWindow (xdisplay, compositor->output);

  compositor->have_x11_sync_object = meta_sync_ring_init (xdisplay);
}

void
meta_compositor_unmanage_screen (MetaCompositor *compositor,
                                 MetaScreen     *screen)
{
  MetaDisplay    *display       = meta_screen_get_display (screen);
  Display        *xdisplay      = meta_display_get_xdisplay (display);
  Window          xroot         = meta_screen_get_xroot (screen);

  /* This is the most important part of cleanup - we have to do this
   * before giving up the window manager selection or the next
   * window manager won't be able to redirect subwindows */
  XCompositeUnredirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
}

/*
 * Shapes the cow so that the given window is exposed,
 * when metaWindow is NULL it clears the shape again
 */
static void
meta_shape_cow_for_window (MetaScreen *screen,
                           MetaWindow *metaWindow)
{
  MetaDisplay *display = screen->display;
  MetaCompositor *compositor = display->compositor;
  Display *xdisplay = display->xdisplay;

  if (metaWindow == NULL)
      XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, None);
  else
    {
      XserverRegion output_region;
      XRectangle screen_rect, window_bounds;
      int width, height;

      window_bounds.x = metaWindow->outer_rect.x;
      window_bounds.y = metaWindow->outer_rect.y;
      window_bounds.width = metaWindow->outer_rect.width;
      window_bounds.height = metaWindow->outer_rect.height;

      meta_screen_get_size (screen, &width, &height);
      screen_rect.x = 0;
      screen_rect.y = 0;
      screen_rect.width = width;
      screen_rect.height = height;

      output_region = XFixesCreateRegion (xdisplay, &window_bounds, 1);

      XFixesInvertRegion (xdisplay, output_region, &screen_rect, output_region);
      XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, output_region);
      XFixesDestroyRegion (xdisplay, output_region);
    }
}

void
meta_compositor_add_window (MetaCompositor    *compositor,
                            MetaWindow        *window)
{
  MetaScreen *screen = meta_window_get_screen (window);
  MetaDisplay *display = meta_screen_get_display (screen);

  meta_error_trap_push (display);

  meta_window_actor_new (window);
  sync_actor_stacking (window->screen->display->compositor);

  meta_error_trap_pop (display);
}

void
meta_compositor_remove_window (MetaCompositor *compositor,
                               MetaWindow     *window)
{
  MetaWindowActor *window_actor = window->compositor_private;
  MetaScreen *screen;

  if (!window_actor)
    return;

  screen = window->screen;

  if (window_actor == compositor->unredirected_window)
    {
      meta_window_actor_set_redirected (window_actor, TRUE);
      meta_shape_cow_for_window (screen,  NULL);
      compositor->unredirected_window = NULL;
    }

  meta_window_actor_destroy (window_actor);
}

static gboolean
is_grabbed_event (XEvent *event)
{
  switch (event->xany.type)
    {
    case ButtonPress:
    case ButtonRelease:
    case EnterNotify:
    case LeaveNotify:
    case MotionNotify:
    case KeyPress:
    case KeyRelease:
      return TRUE;
    }

  return FALSE;
}

/**
 * meta_compositor_process_event: (skip)
 *
 */
gboolean
meta_compositor_process_event (MetaCompositor *compositor,
                               XEvent         *event,
                               MetaWindow     *window)
{
  if (compositor->modal_plugin && is_grabbed_event (event))
    {
      MetaPluginClass *klass = META_PLUGIN_GET_CLASS (compositor->modal_plugin);

      if (klass->xevent_filter)
        klass->xevent_filter (compositor->modal_plugin, event);

      /* We always consume events even if the plugin says it didn't handle them;
       * exclusive is exclusive */
      return TRUE;
    }

  if (meta_plugin_manager_xevent_filter (compositor->plugin_mgr, event))
    return TRUE;

  switch (event->type)
    {
    case PropertyNotify:
      process_property_notify (compositor, (XPropertyEvent *) event, window);
      break;

    default:
      if (event->type == compositor->display->damage_event_base + XDamageNotify)
        {
          /* Core code doesn't handle damage events, so we need to extract the MetaWindow
           * ourselves
           */
          if (window == NULL)
            {
              Window xwin = ((XDamageNotifyEvent *) event)->drawable;
              window = meta_display_lookup_x_window (compositor->display, xwin);
            }

          if (window && window->compositor_private)
            {
              meta_window_actor_process_damage (window->compositor_private, (XDamageNotifyEvent *) event);
              compositor->frame_has_updated_xsurfaces = TRUE;
            }
        }
      break;
    }

  if (compositor->have_x11_sync_object)
    meta_sync_ring_handle_event (event);

  /* Clutter needs to know about MapNotify events otherwise it will
     think the stage is invisible */
  if (event->type == MapNotify)
    clutter_x11_handle_event (event);

  /* The above handling is basically just "observing" the events, so we return
   * FALSE to indicate that the event should not be filtered out; if we have
   * GTK+ windows in the same process, GTK+ needs the ConfigureNotify event, for example.
   */
  return FALSE;
}

void
meta_compositor_switch_workspace (MetaCompositor     *compositor,
                                  MetaScreen         *screen,
                                  MetaWorkspace      *from,
                                  MetaWorkspace      *to,
                                  MetaMotionDirection direction)
{
  gint to_indx, from_indx;
  to_indx   = meta_workspace_index (to);
  from_indx = meta_workspace_index (from);

  meta_compositor_set_all_obscured (compositor, FALSE);

  compositor->switch_workspace_in_progress++;

  if (!compositor->plugin_mgr ||
      !meta_plugin_manager_switch_workspace (compositor->plugin_mgr,
                                             from_indx,
                                             to_indx,
                                             direction))
    {
      compositor->switch_workspace_in_progress--;

      /* We have to explicitely call this to fix up stacking order of the
       * actors; this is because the abs stacking position of actors does not
       * necessarily change during the window hiding/unhiding, only their
       * relative position toward the destkop window.
       */
      meta_finish_workspace_switch (compositor);
      meta_compositor_set_all_obscured (compositor, TRUE);
    }
}

static void
sync_actor_stacking (MetaCompositor *compositor)
{
  GList *children;
  GList *tmp;
  GList *old;
  gboolean reordered;

  /* NB: The first entries in the lists are stacked the lowest */

  /* Restacking will trigger full screen redraws, so it's worth a
   * little effort to make sure we actually need to restack before
   * we go ahead and do it */

  children = clutter_actor_get_children (compositor->window_group);
  reordered = FALSE;

  old = children;

  /* We allow for actors in the window group other than the actors we
   * know about, but it's up to a plugin to try and keep them stacked correctly
   * (we really need extra API to make that reliable.)
   */

  /* Of the actors we know, the bottom actor should be the background actor */

  while (old && old->data != compositor->background_actor && !META_IS_WINDOW_ACTOR (old->data))
    old = old->next;
  if (old == NULL || old->data != compositor->background_actor)
    {
      reordered = TRUE;
      goto done_with_check;
    }

  /* Then the window actors should follow in sequence */

  old = old->next;
  for (tmp = compositor->windows; tmp != NULL; tmp = tmp->next)
    {
      while (old && !META_IS_WINDOW_ACTOR (old->data))
        old = old->next;

      /* old == NULL: someone reparented a window out of the window group,
       * order undefined, always restack */
      if (old == NULL || old->data != tmp->data)
        {
          reordered = TRUE;
          goto done_with_check;
        }

      old = old->next;
    }

 done_with_check:

  g_list_free (children);

  if (!reordered)
    return;

  ClutterActor *parent;

  for (tmp = g_list_last (compositor->windows); tmp != NULL; tmp = tmp->prev)
    {
      ClutterActor *actor = tmp->data;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }

  parent = clutter_actor_get_parent (compositor->background_actor);
  clutter_actor_set_child_below_sibling (parent, compositor->background_actor, NULL);
}

void
meta_compositor_sync_stack (MetaCompositor  *compositor,
                            MetaScreen	    *screen,
                            GList	          *stack)
{
  GList *old_stack;

  /* This is painful because hidden windows that we are in the process
   * of animating out of existence. They'll be at the bottom of the
   * stack of X windows, but we want to leave them in their old position
   * until the animation effect finishes.
   */

  /* Sources: first window is the highest */
  stack = g_list_copy (stack); /* The new stack of MetaWindow */
  old_stack = g_list_reverse (compositor->windows); /* The old stack of MetaWindowActor */
  compositor->windows = NULL;

  while (TRUE)
    {
      MetaWindowActor *old_actor = NULL, *stack_actor = NULL, *actor;
      MetaWindow *old_window = NULL, *stack_window = NULL, *window;

      /* Find the remaining top actor in our existing stack (ignoring
       * windows that have been hidden and are no longer animating) */
      while (old_stack)
        {
          old_actor = old_stack->data;
          old_window = old_actor->priv->window;

          if ((old_window->hidden || old_window->unmanaging) &&
              !old_actor->priv->effect_in_progress)
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
          stack_actor = META_WINDOW_ACTOR (stack_window->compositor_private);
          if (!stack_actor)
            {
              meta_verbose ("Failed to find corresponding MetaWindowActor "
                            "for window %s\n", stack_window->desc);
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

      meta_window_actor_check_obscured (actor);

      compositor->windows = g_list_prepend (compositor->windows, actor);

      stack = g_list_remove (stack, window);
      old_stack = g_list_remove (old_stack, actor);
    }

  sync_actor_stacking (compositor);
}

void
meta_compositor_sync_screen_size (MetaCompositor *compositor,
                                  MetaScreen	   *screen,
                                  guint		        width,
                                  guint		        height)
{
  MetaDisplay *display = compositor->display;
  Display *xdisplay;
  Window xwin;

  xdisplay = meta_display_get_xdisplay (display);
  xwin = clutter_x11_get_stage_window (CLUTTER_STAGE (compositor->stage));

  XResizeWindow (xdisplay, xwin, width, height);

  meta_background_actor_screen_size_changed (screen);

  meta_verbose ("Changed size for stage on screen %d to %dx%d\n",
		screen->number,
		width, height);
}

static void
frame_callback (ClutterStage     *stage,
                CoglFrameEvent    event,
                ClutterFrameInfo *frame_info,
                MetaCompositor   *compositor)
{
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
          gint64 current_cogl_time = cogl_get_clock_time (compositor->context);
          gint64 current_monotonic_time = g_get_monotonic_time ();

          presentation_time =
            current_monotonic_time + (presentation_time_cogl - current_cogl_time) / 1000;
        }
      else
        {
          presentation_time = 0;
        }

      for (l = compositor->windows; l; l = l->next)
        meta_window_actor_frame_complete (l->data, frame_info, presentation_time);
    }
}

static gboolean
meta_pre_paint_func (gpointer data)
{
  GList *l;
  MetaCompositor *compositor = data;
  GSList *screens = compositor->display->screens;
  MetaWindowActor *top_window;
  MetaWindowActor *expected_unredirected_window = NULL;

  if (compositor->windows == NULL)
    return TRUE;

  top_window = g_list_last (compositor->windows)->data;

  if (meta_window_actor_should_unredirect (top_window) &&
      compositor->disable_unredirect_count == 0)
    expected_unredirected_window = top_window;

  if (compositor->unredirected_window != expected_unredirected_window)
    {
      if (compositor->unredirected_window != NULL)
        {
          meta_window_actor_set_redirected (compositor->unredirected_window, TRUE);
          meta_shape_cow_for_window (compositor->display->active_screen, NULL);
        }

      if (expected_unredirected_window != NULL)
        {
          meta_shape_cow_for_window (compositor->display->active_screen,
                                     top_window->priv->window);
          meta_window_actor_set_redirected (top_window, FALSE);
        }

      compositor->unredirected_window = expected_unredirected_window;
    }

  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_pre_paint (l->data);

  if (compositor->frame_has_updated_xsurfaces)
    {
      /* We need to make sure that any X drawing that happens before
       * the XDamageSubtract() for each window above is visible to
       * subsequent GL rendering; the standardized way to do this is
       * GL_EXT_X11_sync_object. Since this isn't implemented yet in
       * mesa, we also have a path that relies on the implementation
       * of the open source drivers.
       *
       * Anything else, we just hope for the best.
       *
       * Xorg and open source driver specifics:
       *
       * The X server makes sure to flush drawing to the kernel before
       * sending out damage events, but since we use
       * DamageReportBoundingBox there may be drawing between the last
       * damage event and the XDamageSubtract() that needs to be
       * flushed as well.
       *
       * Xorg always makes sure that drawing is flushed to the kernel
       * before writing events or responses to the client, so any
       * round trip request at this point is sufficient to flush the
       * GLX buffers.
       */
      if (compositor->have_x11_sync_object)
        compositor->have_x11_sync_object = meta_sync_ring_insert_wait ();
      else
        XSync (compositor->display->xdisplay, False);
    }

  return TRUE;
}

static gboolean
meta_post_paint_func (gpointer data)
{
  MetaCompositor *compositor = data;
  CoglGraphicsResetStatus status;

  if (compositor->frame_has_updated_xsurfaces)
    {
      if (compositor->have_x11_sync_object)
        compositor->have_x11_sync_object = meta_sync_ring_after_frame ();

      compositor->frame_has_updated_xsurfaces = FALSE;
    }

  status = cogl_get_graphics_reset_status (compositor->context);
  switch (status)
    {
    case COGL_GRAPHICS_RESET_STATUS_NO_ERROR:
      break;

    case COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET:
      g_signal_emit_by_name (compositor->display, "gl-video-memory-purged");
      clutter_actor_queue_redraw (CLUTTER_ACTOR (compositor->stage));
      break;

    default:
      /* The ARB_robustness spec says that, on error, the application
         should destroy the old context and create a new one. Since we
         don't have the necessary plumbing to do this we'll simply
         restart the process. Obviously we can't do this when we are
         a wayland compositor but in that case we shouldn't get here
         since we don't enable robustness in that case. */
      meta_restart ();
      break;
    }

  return TRUE;
}

void
meta_compositor_on_shadow_factory_changed (void)
{
  GList *l;

  for (l = compositor_global->windows; l; l = l->next)
    meta_window_actor_invalidate_shadow (l->data);
}

/**
 * meta_compositor_new: (skip)
 *
 */
MetaCompositor *
meta_compositor_new (MetaDisplay *display)
{
  char *atom_names[] = {
    "_XROOTPMAP_ID",
    "_XSETROOT_ID",
    "_NET_WM_WINDOW_OPACITY",
  };
  Atom                   atoms[G_N_ELEMENTS(atom_names)];
  MetaCompositor        *compositor;
  Display               *xdisplay = meta_display_get_xdisplay (display);

  if (!composite_at_least_version (display, 0, 3))
    return NULL;

  compositor = g_new0 (MetaCompositor, 1);

  compositor->display = display;
  compositor->context = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  if (g_getenv("META_DISABLE_MIPMAPS"))
    compositor->no_mipmaps = TRUE;

  meta_verbose ("Creating %d atoms\n", (int) G_N_ELEMENTS (atom_names));
  XInternAtoms (xdisplay, atom_names, G_N_ELEMENTS (atom_names),
                False, atoms);

  compositor->atom_x_root_pixmap = atoms[0];
  compositor->atom_x_set_root = atoms[1];
  compositor->atom_net_wm_window_opacity = atoms[2];

  compositor->pre_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT,
                                           meta_pre_paint_func,
                                           compositor,
                                           NULL);
  compositor->post_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                           meta_post_paint_func,
                                           compositor,
                                           NULL);

  compositor_global = compositor;

  return compositor;
}

/**
 * meta_get_overlay_window: (skip)
 *
 */
Window
meta_get_overlay_window (MetaScreen *screen)
{
  return screen->display->compositor->output;
}

/**
 * meta_disable_unredirect_for_screen:
 * @screen: a #MetaScreen
 *
 * Disables unredirection, can be usefull in situations where having
 * unredirected windows is undesireable like when recording a video.
 *
 */
void
meta_disable_unredirect_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = screen->display->compositor;

  compositor->disable_unredirect_count = compositor->disable_unredirect_count + 1;
}

/**
 * meta_enable_unredirect_for_screen:
 * @screen: a #MetaScreen
 *
 * Enables unredirection which reduces the overhead for apps like games.
 *
 */
void
meta_enable_unredirect_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = screen->display->compositor;
  if (compositor->disable_unredirect_count == 0)
    g_warning ("Called enable_unredirect_for_screen while unredirection is enabled.");
  if (compositor->disable_unredirect_count > 0)
   compositor->disable_unredirect_count = compositor->disable_unredirect_count - 1;
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
meta_compositor_flash_screen (MetaCompositor *compositor,
                              MetaScreen     *screen)
{
  ClutterActor *stage;
  ClutterActor *flash;
  ClutterTransition *transition;
  gfloat width, height;

  stage = meta_get_stage_for_screen (screen);
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

void
meta_compositor_show_tile_preview (MetaCompositor *compositor,
                                   MetaScreen     *screen,
                                   MetaWindow     *window,
                                   MetaRectangle  *tile_rect,
                                   int            tile_monitor_number,
                                   guint          snap_queued)
{
  meta_window_update_rects (window);
  meta_plugin_manager_show_tile_preview (compositor->plugin_mgr,
                                          window, tile_rect, tile_monitor_number,
                                          snap_queued);
}

void
meta_compositor_hide_tile_preview (MetaCompositor *compositor,
                                   MetaScreen     *screen)
{
  meta_plugin_manager_hide_tile_preview (compositor->plugin_mgr);
}

void
meta_compositor_show_hud_preview (MetaCompositor *compositor,
                                  MetaScreen     *screen,
                                  guint          current_proximity_zone,
                                  MetaRectangle  *work_area,
                                  guint          snap_queued)
{
  meta_plugin_manager_show_hud_preview (compositor->plugin_mgr,
                                        current_proximity_zone,
                                        work_area,
                                        snap_queued);
}

void
meta_compositor_hide_hud_preview (MetaCompositor *compositor,
                                  MetaScreen     *screen)
{
  meta_plugin_manager_hide_hud_preview (compositor->plugin_mgr);
}

MetaWindow *
meta_compositor_get_window_for_xwindow (Window xwindow)
{
  GList *l;

  for (l = compositor_global->windows; l; l = l->next)
    {
      MetaWindow *window = META_WINDOW_ACTOR (l->data)->priv->window;
      if (window->xwindow == xwindow)
        return window;
    }
  return NULL;
}

/**
 * meta_compositor_monotonic_time_to_server_time:
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
gint64
meta_compositor_monotonic_time_to_server_time (MetaDisplay *display,
                                               gint64       monotonic_time)
{
  MetaCompositor *compositor = display->compositor;

  if (compositor->server_time_query_time == 0 ||
      (!compositor->server_time_is_monotonic_time &&
       monotonic_time > compositor->server_time_query_time + 10*1000*1000)) /* 10 seconds */
    {
      guint32 server_time = meta_display_get_current_time_roundtrip (display);
      gint64 server_time_usec = (gint64)server_time * 1000;
      gint64 current_monotonic_time = g_get_monotonic_time ();
      compositor->server_time_query_time = current_monotonic_time;

      /* If the server time is within a second of the monotonic time,
       * we assume that they are identical. This seems like a big margin,
       * but we want to be as robust as possible even if the system
       * is under load and our processing of the server response is
       * delayed.
       */
      if (server_time_usec > current_monotonic_time - 1000*1000 &&
          server_time_usec < current_monotonic_time + 1000*1000)
        compositor->server_time_is_monotonic_time = TRUE;

      compositor->server_time_offset = server_time_usec - current_monotonic_time;
    }

  if (compositor->server_time_is_monotonic_time)
    return monotonic_time;
  else
    return monotonic_time + compositor->server_time_offset;
}

void
meta_compositor_set_all_obscured (MetaCompositor *compositor,
                                  gboolean        obscured)
{
  GList *l;

  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_override_obscured_internal (l->data, obscured);
}

void
meta_compositor_grab_op_begin (MetaCompositor *compositor)
{
  // CLUTTER_ACTOR_NO_LAYOUT set on the window group improves responsiveness of windows,
  // but causes windows to flicker in and out of view sporadically on some configurations
  // while dragging windows. Make sure it is disabled during the grab.
  clutter_actor_unset_flags (compositor->window_group, CLUTTER_ACTOR_NO_LAYOUT);
  meta_compositor_set_all_obscured (compositor, FALSE);
}

void
meta_compositor_grab_op_end (MetaCompositor *compositor)
{
  MetaWindow *window = compositor->display->grab_window;

  clutter_actor_set_flags (compositor->window_group, CLUTTER_ACTOR_NO_LAYOUT);
  meta_compositor_set_all_obscured (compositor, TRUE);

  meta_window_update_rects (window);
  meta_window_update_monitor (window);
}

CoglContext *
meta_compositor_get_cogl_context (void)
{
  return compositor_global->context;
}

void
meta_compositor_update_sync_state (MetaCompositor *compositor,
                                   gboolean state)
{
  clutter_stage_x11_update_sync_state (compositor->stage, state);
}

