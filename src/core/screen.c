/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin X screen handler */

/* 
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>
#include "screen-private.h"
#include <meta/main.h>
#include <meta/util.h>
#include <meta/errors.h>
#include "window-private.h"
#include "frame.h"
#include <meta/prefs.h>
#include "workspace-private.h"
#include "keybindings-private.h"
#include "stack.h"
#include "xprops.h"
#include <meta/compositor.h>
#include "muffin-enum-types.h"

#ifdef HAVE_SOLARIS_XINERAMA
#include <X11/extensions/xinerama.h>
#endif
#ifdef HAVE_XFREE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include <X11/Xatom.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>

static char* get_screen_name (MetaDisplay *display,
                              int          number);

static void update_num_workspaces  (MetaScreen *screen,
                                    guint32     timestamp);
static void update_focus_mode      (MetaScreen *screen);
static void set_workspace_names    (MetaScreen *screen);
static void prefs_changed_callback (MetaPreference pref,
                                    gpointer       data);

static void set_desktop_geometry_hint (MetaScreen *screen);
static void set_desktop_viewport_hint (MetaScreen *screen);

#ifdef HAVE_STARTUP_NOTIFICATION
static void meta_screen_sn_event   (SnMonitorEvent *event,
                                    void           *user_data);
#endif

#define SNAP_OSD_TIMEOUT 2

enum
{
  PROP_N_WORKSPACES = 1,
  PROP_KEYBOARD_GRABBED,
};

enum
{
  RESTACKED,
  TOGGLE_RECORDING,
  WORKSPACE_ADDED,
  WORKSPACE_REMOVED,
  WORKSPACE_SWITCHED,
  WINDOW_ENTERED_MONITOR,
  WINDOW_LEFT_MONITOR,
  STARTUP_SEQUENCE_CHANGED,
  WORKAREAS_CHANGED,
  MONITORS_CHANGED,
  SNAP_OSD_SHOW,
  SNAP_OSD_HIDE,
  WORKSPACE_OSD_SHOW,

  LAST_SIGNAL
};

static guint screen_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (MetaScreen, meta_screen, G_TYPE_OBJECT);

static void
meta_screen_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
#if 0
  MetaScreen *screen = META_SCREEN (object);
#endif

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_screen_get_property (GObject      *object,
                          guint         prop_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  MetaScreen *screen = META_SCREEN (object);

  switch (prop_id)
    {
    case PROP_N_WORKSPACES:
      g_value_set_int (value, meta_screen_get_n_workspaces (screen));
      break;
    case PROP_KEYBOARD_GRABBED:
      g_value_set_boolean (value, screen->all_keys_grabbed ? TRUE : FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_screen_finalize (GObject *object)
{
  /* Actual freeing done in meta_screen_free() for now */
  
  G_OBJECT_CLASS (meta_screen_parent_class)->finalize (object);
}

static void
meta_screen_class_init (MetaScreenClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec   *pspec;

  object_class->get_property = meta_screen_get_property;
  object_class->set_property = meta_screen_set_property;
  object_class->finalize = meta_screen_finalize;

  screen_signals[RESTACKED] =
    g_signal_new ("restacked",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaScreenClass, restacked),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  pspec = g_param_spec_int ("n-workspaces",
                            "N Workspaces",
                            "Number of workspaces",
                            1, G_MAXINT, 1,
                            G_PARAM_READABLE);

  screen_signals[WORKSPACE_ADDED] =
    g_signal_new ("workspace-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_INT);

  screen_signals[WORKSPACE_REMOVED] =
    g_signal_new ("workspace-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_INT);

  screen_signals[WORKSPACE_SWITCHED] =
    g_signal_new ("workspace-switched",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  3,
                  G_TYPE_INT,
                  G_TYPE_INT,
                  META_TYPE_MOTION_DIRECTION);

  screen_signals[WINDOW_ENTERED_MONITOR] =
    g_signal_new ("window-entered-monitor",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  META_TYPE_WINDOW);

  screen_signals[WINDOW_LEFT_MONITOR] =
    g_signal_new ("window-left-monitor",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  META_TYPE_WINDOW);

  screen_signals[STARTUP_SEQUENCE_CHANGED] =
    g_signal_new ("startup-sequence-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  screen_signals[TOGGLE_RECORDING] =
    g_signal_new ("toggle-recording",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  screen_signals[WORKAREAS_CHANGED] =
    g_signal_new ("workareas-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaScreenClass, workareas_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  screen_signals[MONITORS_CHANGED] =
    g_signal_new ("monitors-changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (MetaScreenClass, monitors_changed),
          NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  screen_signals[SNAP_OSD_SHOW] =
    g_signal_new ("show-snap-osd",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  screen_signals[SNAP_OSD_HIDE] =
    g_signal_new ("hide-snap-osd",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  screen_signals[WORKSPACE_OSD_SHOW] =
    g_signal_new ("show-workspace-osd",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  g_object_class_install_property (object_class,
                                   PROP_N_WORKSPACES,
                                   pspec);

  pspec = g_param_spec_boolean ("keyboard-grabbed",
                                "Keyboard grabbed",
                                "Whether the keyboard is grabbed",
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_KEYBOARD_GRABBED,
                                   pspec);
}

static void
meta_screen_init (MetaScreen *screen)
{
}

static int
set_wm_check_hint (MetaScreen *screen)
{
  unsigned long data[1];

  g_return_val_if_fail (screen->display->leader_window != None, 0);
  
  data[0] = screen->display->leader_window;

  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SUPPORTING_WM_CHECK,
                   XA_WINDOW,
                   32, PropModeReplace, (guchar*) data, 1);

  return Success;
}

static void
unset_wm_check_hint (MetaScreen *screen)
{
  XDeleteProperty (screen->display->xdisplay, screen->xroot, 
                   screen->display->atom__NET_SUPPORTING_WM_CHECK);
}

static int
set_supported_hint (MetaScreen *screen)
{
  Atom atoms[] = {
#define EWMH_ATOMS_ONLY
#define item(x)  screen->display->atom_##x,
#include <meta/atomnames.h>
#undef item
#undef EWMH_ATOMS_ONLY
  };

  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SUPPORTED,
                   XA_ATOM,
                   32, PropModeReplace,
                   (guchar*) atoms, G_N_ELEMENTS(atoms));
  
  return Success;
}

static int
set_wm_icon_size_hint (MetaScreen *screen)
{
#define N_VALS 6
  gulong vals[N_VALS];

  /* min width, min height, max w, max h, width inc, height inc */
  vals[0] = META_ICON_WIDTH;
  vals[1] = META_ICON_HEIGHT;
  vals[2] = META_ICON_WIDTH;
  vals[3] = META_ICON_HEIGHT;
  vals[4] = 0;
  vals[5] = 0;
  
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom_WM_ICON_SIZE,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) vals, N_VALS);
  
  return Success;
#undef N_VALS
}

/* The list of monitors reported by the windowing system might include
 * mirrored monitors with identical bounds. Since mirrored monitors
 * shouldn't be treated as separate monitors for most purposes, we
 * filter them out here. (We ignore the possibility of partially
 * overlapping monitors because they are rare and it's hard to come
 * up with any sensible interpretation.)
 */
static void
filter_mirrored_monitors (MetaScreen *screen)
{
  int i, j;

  /* Currently always true and simplifies things */
  g_assert (screen->primary_monitor_index == 0);

  for (i = 1; i < screen->n_monitor_infos; i++)
    {
      /* In case we've filtered previous monitors */
      screen->monitor_infos[i].number = i;

      for (j = 0; j < i; j++)
        {
          if (meta_rectangle_equal (&screen->monitor_infos[i].rect,
                                    &screen->monitor_infos[j].rect))
            {
              memmove (&screen->monitor_infos[i],
                       &screen->monitor_infos[i + 1],
                       (screen->n_monitor_infos - i - 1) * sizeof (MetaMonitorInfo));
              screen->n_monitor_infos--;
              i--;

              continue;
            }
        }
    }
}

#ifdef HAVE_RANDR
static MetaMonitorInfo *
find_monitor_with_rect (MetaScreen *screen, int x, int y, int w, int h)
{
  MetaMonitorInfo *info;
  int i;

  for (i = 0; i < screen->n_monitor_infos; i++)
    {
      info = &screen->monitor_infos[i];
      if (x == info->rect.x &&
          y == info->rect.y &&
          w == info->rect.width &&
          h == info->rect.height)
        return info;
    }
  return NULL;
}

/* In the case of multiple outputs of a single crtc (mirroring), we consider one of the
 * outputs the "main". This is the one we consider "owning" the windows, so if
 * the mirroring is changed to a dual monitor setup then the windows are moved to the
 * crtc that now has that main output. If one of the outputs is the primary that is
 * always the main, otherwise we just use the first.
 */
static XID
find_main_output_for_crtc (MetaScreen *screen, XRRScreenResources *resources, XRRCrtcInfo *crtc)
{
  XRROutputInfo *output;
  RROutput primary_output;
  int i;
  XID res;

  primary_output = XRRGetOutputPrimary (screen->display->xdisplay, screen->xroot);

  res = None;
  for (i = 0; i < crtc->noutput; i++)
    {
      output = XRRGetOutputInfo (screen->display->xdisplay, resources, crtc->outputs[i]);
      if (output->connection != RR_Disconnected &&
          (res == None || crtc->outputs[i] == primary_output))
        res = crtc->outputs[i];
      XRRFreeOutputInfo (output);
    }

  return res;
}

#endif

static void
reload_monitor_infos (MetaScreen *screen)
{
  MetaDisplay *display;

  {
    GList *tmp;

    tmp = screen->workspaces;
    while (tmp != NULL)
      {
        MetaWorkspace *space = tmp->data;

        meta_workspace_invalidate_work_area (space);
        
        tmp = tmp->next;
      }
  }

  display = screen->display;

  /* Any previous screen->monitor_infos is freed by the caller */

  screen->monitor_infos = NULL;
  screen->n_monitor_infos = 0;
  screen->last_monitor_index = 0;

  /* Xinerama doesn't have a concept of primary monitor, however XRandR
   * does. However, the XRandR xinerama compat code always sorts the
   * primary output first, so we rely on that here. We could use the
   * native XRandR calls instead of xinerama, but that would be
   * slightly problematic for _NET_WM_FULLSCREEN_MONITORS support, as
   * that is defined in terms of xinerama monitor indexes.
   * So, since we don't need anything in xrandr except the primary
   * we can keep using xinerama and use the first monitor as the
   * primary.
   */
  screen->primary_monitor_index = 0;

  screen->display->monitor_cache_invalidated = TRUE;

  if (g_getenv ("MUFFIN_DEBUG_XINERAMA"))
    {
      meta_topic (META_DEBUG_XINERAMA,
                  "Pretending a single monitor has two Xinerama screens\n");

      screen->monitor_infos = g_new0 (MetaMonitorInfo, 2);
      screen->n_monitor_infos = 2;

      screen->monitor_infos[0].number = 0;
      screen->monitor_infos[0].rect = screen->rect;
      screen->monitor_infos[0].rect.width = screen->rect.width / 2;

      screen->monitor_infos[1].number = 1;
      screen->monitor_infos[1].rect = screen->rect;
      screen->monitor_infos[1].rect.x = screen->rect.width / 2;
      screen->monitor_infos[1].rect.width = screen->rect.width / 2;
    }

#ifdef HAVE_XFREE_XINERAMA
  if (screen->n_monitor_infos == 0 &&
      XineramaIsActive (display->xdisplay))
    {
      XineramaScreenInfo *infos;
      int n_infos;
      int i;
      
      n_infos = 0;
      infos = XineramaQueryScreens (display->xdisplay, &n_infos);

      meta_topic (META_DEBUG_XINERAMA,
                  "Found %d Xinerama screens on display %s\n",
                  n_infos, display->name);

      if (n_infos > 0)
        {
          screen->monitor_infos = g_new0 (MetaMonitorInfo, n_infos);
          screen->n_monitor_infos = n_infos;
          
          i = 0;
          while (i < n_infos)
            {
              screen->monitor_infos[i].number = infos[i].screen_number;
              screen->monitor_infos[i].rect.x = infos[i].x_org;
              screen->monitor_infos[i].rect.y = infos[i].y_org;
              screen->monitor_infos[i].rect.width = infos[i].width;
              screen->monitor_infos[i].rect.height = infos[i].height;

              meta_topic (META_DEBUG_XINERAMA,
                          "Monitor %d is %d,%d %d x %d\n",
                          screen->monitor_infos[i].number,
                          screen->monitor_infos[i].rect.x,
                          screen->monitor_infos[i].rect.y,
                          screen->monitor_infos[i].rect.width,
                          screen->monitor_infos[i].rect.height);
              
              ++i;
            }
        }
      
      meta_XFree (infos);

#ifdef HAVE_RANDR
      {
        XRRScreenResources *resources;

        resources = XRRGetScreenResourcesCurrent (display->xdisplay, screen->xroot);
        if (resources)
          {
            for (i = 0; i < resources->ncrtc; i++)
              {
                XRRCrtcInfo *crtc;
                MetaMonitorInfo *info;

                crtc = XRRGetCrtcInfo (display->xdisplay, resources, resources->crtcs[i]);
                info = find_monitor_with_rect (screen, crtc->x, crtc->y, (int)crtc->width, (int)crtc->height);
                if (info)
                  info->output = find_main_output_for_crtc (screen, resources, crtc);

                XRRFreeCrtcInfo (crtc);
              }
            XRRFreeScreenResources (resources);
          }
      }
#endif
    }
  else if (screen->n_monitor_infos > 0)
    {
      meta_topic (META_DEBUG_XINERAMA,
                  "No XFree86 Xinerama extension or XFree86 Xinerama inactive on display %s\n",
                  display->name);
    }
#else
  meta_topic (META_DEBUG_XINERAMA,
              "Muffin compiled without XFree86 Xinerama support\n");
#endif /* HAVE_XFREE_XINERAMA */

#ifdef HAVE_SOLARIS_XINERAMA
  /* This code from GDK, Copyright (C) 2002 Sun Microsystems */
  if (screen->n_monitor_infos == 0 &&
      XineramaGetState (screen->display->xdisplay,
                        screen->number))
    {
      XRectangle monitors[MAXFRAMEBUFFERS];
      unsigned char hints[16];
      int result;
      int n_monitors;
      int i;

      n_monitors = 0;
      result = XineramaGetInfo (screen->display->xdisplay,
                                screen->number,
				monitors, hints,
                                &n_monitors);
      /* Yes I know it should be Success but the current implementation 
       * returns the num of monitor
       */
      if (result > 0)
	{
          g_assert (n_monitors > 0);
          
          screen->monitor_infos = g_new0 (MetaMonitorInfo, n_monitors);
          screen->n_monitor_infos = n_monitors;
          
          i = 0;
          while (i < n_monitors)
            {
              screen->monitor_infos[i].number = i;
              screen->monitor_infos[i].rect.x = monitors[i].x;
              screen->monitor_infos[i].rect.y = monitors[i].y;
              screen->monitor_infos[i].rect.width = monitors[i].width;
              screen->monitor_infos[i].rect.height = monitors[i].height;

              meta_topic (META_DEBUG_XINERAMA,
                          "Monitor %d is %d,%d %d x %d\n",
                          screen->monitor_infos[i].number,
                          screen->monitor_infos[i].rect.x,
                          screen->monitor_infos[i].rect.y,
                          screen->monitor_infos[i].rect.width,
                          screen->monitor_infos[i].rect.height);              
              
              ++i;
            }
	}
    }
  else if (screen->n_monitor_infos == 0)
    {
      meta_topic (META_DEBUG_XINERAMA,
                  "No Solaris Xinerama extension or Solaris Xinerama inactive on display %s\n",
                  display->name);
    }
#else
  meta_topic (META_DEBUG_XINERAMA,
              "Muffin compiled without Solaris Xinerama support\n");
#endif /* HAVE_SOLARIS_XINERAMA */

  
  /* If no Xinerama, fill in the single screen info so
   * we can use the field unconditionally
   */
  if (screen->n_monitor_infos == 0)
    {
      meta_topic (META_DEBUG_XINERAMA,
                  "No Xinerama screens, using default screen info\n");
          
      screen->monitor_infos = g_new0 (MetaMonitorInfo, 1);
      screen->n_monitor_infos = 1;
          
      screen->monitor_infos[0].number = 0;
      screen->monitor_infos[0].rect = screen->rect;
    }

  filter_mirrored_monitors (screen);

  screen->monitor_infos[screen->primary_monitor_index].is_primary = TRUE;

  g_assert (screen->n_monitor_infos > 0);
  g_assert (screen->monitor_infos != NULL);
}

/* The guard window allows us to leave minimized windows mapped so
 * that compositor code may provide live previews of them.
 * Instead of being unmapped/withdrawn, they get pushed underneath
 * the guard window. */
static Window
create_guard_window (Display *xdisplay, MetaScreen *screen)
{
  XSetWindowAttributes attributes;
  Window guard_window;
  gulong create_serial;
  
  attributes.event_mask = NoEventMask;
  attributes.override_redirect = True;
  attributes.background_pixel = BlackPixel (xdisplay, screen->number);

  /* We have to call record_add() after we have the new window ID,
   * so save the serial for the CreateWindow request until then */
  create_serial = XNextRequest(xdisplay);
  guard_window =
    XCreateWindow (xdisplay,
		   screen->xroot,
		   0, /* x */
		   0, /* y */
		   screen->rect.width,
		   screen->rect.height,
		   0, /* border width */
		   CopyFromParent, /* depth */
		   CopyFromParent, /* class */
		   CopyFromParent, /* visual */
		   CWEventMask|CWOverrideRedirect|CWBackPixel,
		   &attributes);
  meta_stack_tracker_record_add (screen->stack_tracker,
                                 guard_window,
                                 create_serial);

  meta_stack_tracker_record_lower (screen->stack_tracker,
                                   guard_window,
                                   XNextRequest (xdisplay));
  XLowerWindow (xdisplay, guard_window);
  XMapWindow (xdisplay, guard_window);
  return guard_window;
}

LOCAL_SYMBOL MetaScreen*
meta_screen_new (MetaDisplay *display,
                 int          number,
                 guint32      timestamp)
{
  MetaScreen *screen;
  Window xroot;
  Display *xdisplay;
  XWindowAttributes attr;
  Window new_wm_sn_owner;
  Window current_wm_sn_owner;
  gboolean replace_current_wm;
  Atom wm_sn_atom;
  char buf[128];
  guint32 manager_timestamp;
  gulong current_workspace;
  
  replace_current_wm = meta_get_replace_current_wm ();
  
  /* Only display->name, display->xdisplay, and display->error_traps
   * can really be used in this function, since normally screens are
   * created from the MetaDisplay constructor
   */
  
  xdisplay = display->xdisplay;
  
  meta_verbose ("Trying screen %d on display '%s'\n",
                number, display->name);

  xroot = RootWindow (xdisplay, number);

  /* FVWM checks for None here, I don't know if this
   * ever actually happens
   */
  if (xroot == None)
    {
      meta_warning (_("Screen %d on display '%s' is invalid\n"),
                    number, display->name);
      return NULL;
    }

  sprintf (buf, "WM_S%d", number);
  wm_sn_atom = XInternAtom (xdisplay, buf, False);  
  
  current_wm_sn_owner = XGetSelectionOwner (xdisplay, wm_sn_atom);

  if (current_wm_sn_owner != None)
    {
      XSetWindowAttributes attrs;
      
      if (!replace_current_wm)
        {
          meta_warning (_("Screen %d on display \"%s\" already has a window manager; try using the --replace option to replace the current window manager.\n"),
                        number, display->name);

          return NULL;
        }

      /* We want to find out when the current selection owner dies */
      meta_error_trap_push_with_return (display);
      attrs.event_mask = StructureNotifyMask;
      XChangeWindowAttributes (xdisplay,
                               current_wm_sn_owner, CWEventMask, &attrs);
      if (meta_error_trap_pop_with_return (display) != Success)
        current_wm_sn_owner = None; /* don't wait for it to die later on */
    }

  /* We need SelectionClear and SelectionRequest events on the new_wm_sn_owner,
   * but those cannot be masked, so we only need NoEventMask.
   */
  new_wm_sn_owner = meta_create_offscreen_window (xdisplay, xroot, NoEventMask);

  manager_timestamp = timestamp;
  
  XSetSelectionOwner (xdisplay, wm_sn_atom, new_wm_sn_owner,
                      manager_timestamp);

  if (XGetSelectionOwner (xdisplay, wm_sn_atom) != new_wm_sn_owner)
    {
      meta_warning (_("Could not acquire window manager selection on screen %d display \"%s\"\n"),
                    number, display->name);

      XDestroyWindow (xdisplay, new_wm_sn_owner);
      
      return NULL;
    }
  
  {
    /* Send client message indicating that we are now the WM */
    XClientMessageEvent ev;
    
    ev.type = ClientMessage;
    ev.window = xroot;
    ev.message_type = display->atom_MANAGER;
    ev.format = 32;
    ev.data.l[0] = manager_timestamp;
    ev.data.l[1] = wm_sn_atom;

    XSendEvent (xdisplay, xroot, False, StructureNotifyMask, (XEvent*)&ev);
  }

  /* Wait for old window manager to go away */
  if (current_wm_sn_owner != None)
    {
      XEvent event;

      /* We sort of block infinitely here which is probably lame. */
      
      meta_verbose ("Waiting for old window manager to exit\n");
      do
        {
          XWindowEvent (xdisplay, current_wm_sn_owner,
                        StructureNotifyMask, &event);
        }
      while (event.type != DestroyNotify);
    }
  
  /* select our root window events */
  meta_error_trap_push_with_return (display);

  /* We need to or with the existing event mask since
   * gtk+ may be interested in other events.
   */
  XGetWindowAttributes (xdisplay, xroot, &attr);
  XSelectInput (xdisplay,
                xroot,
                SubstructureRedirectMask | SubstructureNotifyMask |
                ColormapChangeMask | PropertyChangeMask |
                LeaveWindowMask | EnterWindowMask |
                KeyPressMask | KeyReleaseMask |
                FocusChangeMask | StructureNotifyMask |
                ExposureMask | attr.your_event_mask);
  if (meta_error_trap_pop_with_return (display) != Success)
    {
      meta_warning (_("Screen %d on display \"%s\" already has a window manager\n"),
                    number, display->name);

      XDestroyWindow (xdisplay, new_wm_sn_owner);
      
      return NULL;
    }
  
  screen = g_object_new (META_TYPE_SCREEN, NULL);
  screen->closing = 0;
  
  screen->display = display;
  screen->number = number;
  screen->screen_name = get_screen_name (display, number);
  screen->xscreen = ScreenOfDisplay (xdisplay, number);
  screen->xroot = xroot;
  screen->rect.x = screen->rect.y = 0;
  screen->rect.width = WidthOfScreen (screen->xscreen);
  screen->rect.height = HeightOfScreen (screen->xscreen);
  screen->current_cursor = -1; /* invalid/unset */
  screen->default_xvisual = DefaultVisualOfScreen (screen->xscreen);
  screen->default_depth = DefaultDepthOfScreen (screen->xscreen);
  screen->flash_window = None;

  screen->wm_sn_selection_window = new_wm_sn_owner;
  screen->wm_sn_atom = wm_sn_atom;
  screen->wm_sn_timestamp = manager_timestamp;

  screen->wm_cm_selection_window = meta_create_offscreen_window (xdisplay, 
                                                                 xroot, 
                                                                 NoEventMask);
  screen->work_area_later = 0;

  screen->active_workspace = NULL;
  screen->workspaces = NULL;
  screen->rows_of_workspaces = 1;
  screen->columns_of_workspaces = -1;
  screen->vertical_workspaces = FALSE;
  screen->starting_corner = META_SCREEN_TOPLEFT;
  screen->compositor_data = NULL;
  screen->guard_window = None;

  screen->monitor_infos = NULL;
  screen->n_monitor_infos = 0;
  screen->last_monitor_index = 0;  
  
  reload_monitor_infos (screen);
  
  meta_screen_set_cursor (screen, META_CURSOR_DEFAULT);

  /* Handle creating a no_focus_window for this screen */  
  screen->no_focus_window =
    meta_create_offscreen_window (display->xdisplay,
                                  screen->xroot,
                                  FocusChangeMask|KeyPressMask|KeyReleaseMask);
  XMapWindow (display->xdisplay, screen->no_focus_window);
  /* Done with no_focus_window stuff */
  
  set_wm_icon_size_hint (screen);
  
  set_supported_hint (screen);
  
  set_wm_check_hint (screen);

  set_desktop_viewport_hint (screen);

  set_desktop_geometry_hint (screen);

  meta_screen_update_workspace_layout (screen);

  /* Get current workspace */
  current_workspace = 0;
  if (meta_prop_get_cardinal (screen->display,
                              screen->xroot,
                              screen->display->atom__NET_CURRENT_DESKTOP,
                              &current_workspace))
    meta_verbose ("Read existing _NET_CURRENT_DESKTOP = %d\n",
                  (int) current_workspace);
  else
    meta_verbose ("No _NET_CURRENT_DESKTOP present\n");
  
  /* Screens must have at least one workspace at all times,
   * so create that required workspace.
   */
  meta_workspace_activate (meta_workspace_new (screen), timestamp);
  update_num_workspaces (screen, timestamp);
  
  set_workspace_names (screen);

  screen->all_keys_grabbed = FALSE;
  screen->keys_grabbed = FALSE;
  meta_screen_grab_keys (screen);

  screen->ui = meta_ui_new (screen->display->xdisplay,
                            screen->xscreen);

  screen->tab_popup = NULL;
  screen->ws_popup = NULL;
  screen->tile_preview = NULL;
  screen->tile_hud = NULL;

  screen->tile_preview_timeout_id = 0;
  screen->tile_hud_timeout_id = 0;
  screen->tile_hud_fade_timeout_id = 0;

  screen->snap_osd_timeout_id = 0;

  screen->hud_opacity = 0.0;
  screen->hud_hiding = FALSE;

  screen->stack = meta_stack_new (screen);
  screen->stack_tracker = meta_stack_tracker_new (screen);

  meta_prefs_add_listener (prefs_changed_callback, screen);

#ifdef HAVE_STARTUP_NOTIFICATION
  screen->sn_context =
    sn_monitor_context_new (screen->display->sn_display,
                            screen->number,
                            meta_screen_sn_event,
                            screen,
                            NULL);
  screen->startup_sequences = NULL;
  screen->startup_sequence_timeout = 0;
#endif

  /* Switch to the _NET_CURRENT_DESKTOP workspace */
  {
    MetaWorkspace *space;
    
    space = meta_screen_get_workspace_by_index (screen,
                                                current_workspace);
    
    if (space != NULL)
      meta_workspace_activate (space, timestamp);
  }

  meta_verbose ("Added screen %d ('%s') root 0x%lx\n",
                screen->number, screen->screen_name, screen->xroot);
  
  return screen;
}

LOCAL_SYMBOL void
meta_screen_free (MetaScreen *screen,
                  guint32     timestamp)
{
  MetaDisplay *display;

  display = screen->display;

  screen->closing += 1;
  
  meta_display_grab (display);

  if (screen->display->compositor)
    {
      meta_compositor_unmanage_screen (screen->display->compositor,
				       screen);
    }
  
  meta_display_unmanage_windows_for_screen (display, screen, timestamp);
  
  meta_prefs_remove_listener (prefs_changed_callback, screen);
  
  meta_screen_ungrab_keys (screen);

#ifdef HAVE_STARTUP_NOTIFICATION
  g_slist_foreach (screen->startup_sequences,
                   (GFunc) sn_startup_sequence_unref, NULL);
  g_slist_free (screen->startup_sequences);
  screen->startup_sequences = NULL;

  if (screen->startup_sequence_timeout != 0)
    {
      g_source_remove (screen->startup_sequence_timeout);
      screen->startup_sequence_timeout = 0;
    }
  if (screen->sn_context)
    {
      sn_monitor_context_unref (screen->sn_context);
      screen->sn_context = NULL;
    }
#endif
  
  meta_ui_free (screen->ui);

  meta_stack_free (screen->stack);
  meta_stack_tracker_free (screen->stack_tracker);

  meta_error_trap_push_with_return (screen->display);
  XSelectInput (screen->display->xdisplay, screen->xroot, 0);
  if (meta_error_trap_pop_with_return (screen->display) != Success)
    meta_warning (_("Could not release screen %d on display \"%s\"\n"),
                  screen->number, screen->display->name);

  unset_wm_check_hint (screen);

  XDestroyWindow (screen->display->xdisplay,
                  screen->wm_sn_selection_window);
  
  if (screen->work_area_later != 0)
    g_source_remove (screen->work_area_later);

  if (screen->monitor_infos)
    g_free (screen->monitor_infos);

  if (screen->tile_preview_timeout_id)
    g_source_remove (screen->tile_preview_timeout_id);

  if (screen->tile_preview)
    meta_tile_preview_free (screen->tile_preview);

  if (screen->tile_hud_timeout_id)
    g_source_remove (screen->tile_hud_timeout_id);

  if (screen->tile_hud_fade_timeout_id)
    g_source_remove (screen->tile_hud_fade_timeout_id);

  if (screen->snap_osd_timeout_id)
    g_source_remove (screen->snap_osd_timeout_id);

  if (screen->tile_hud)
    meta_tile_hud_free (screen->tile_hud);

  g_free (screen->screen_name);

  g_object_unref (screen);

  XFlush (display->xdisplay);
  meta_display_ungrab (display);
}

typedef struct
{
  Window		xwindow;
  XWindowAttributes	attrs;
} WindowInfo;

static GList *
list_windows (MetaScreen *screen)
{
  Window ignored1, ignored2;
  Window *children;
  guint n_children, i;
  GList *result;

  XQueryTree (screen->display->xdisplay,
              screen->xroot,
              &ignored1, &ignored2, &children, &n_children);

  result = NULL;
  for (i = 0; i < n_children; ++i)
    {
      WindowInfo *info = g_new0 (WindowInfo, 1);

      meta_error_trap_push_with_return (screen->display);
      
      XGetWindowAttributes (screen->display->xdisplay,
                            children[i], &info->attrs);

      if (meta_error_trap_pop_with_return (screen->display))
	{
          meta_verbose ("Failed to get attributes for window 0x%lx\n",
                        children[i]);
	  g_free (info);
        }
      else
        {
	  info->xwindow = children[i];
	}

      result = g_list_prepend (result, info);
    }

  if (children)
    XFree (children);

  return g_list_reverse (result);
}

LOCAL_SYMBOL void
meta_screen_manage_all_windows (MetaScreen *screen)
{
  GList *windows;
  GList *list;

  meta_display_grab (screen->display);

  if (screen->guard_window == None)
    screen->guard_window = create_guard_window (screen->display->xdisplay,
                                                screen);

  windows = list_windows (screen);

  meta_stack_freeze (screen->stack);
  for (list = windows; list != NULL; list = list->next)
    {
      WindowInfo *info = list->data;

      meta_window_new_with_attrs (screen->display, info->xwindow, TRUE,
                                  META_COMP_EFFECT_NONE,
                                  &info->attrs);
    }
  meta_stack_thaw (screen->stack);

  g_list_foreach (windows, (GFunc)g_free, NULL);
  g_list_free (windows);

  meta_display_ungrab (screen->display);
}

LOCAL_SYMBOL void
meta_screen_composite_all_windows (MetaScreen *screen)
{
  MetaDisplay *display;
  GSList *windows, *tmp;

  display = screen->display;
  if (!display->compositor)
    return;

  windows = meta_display_list_windows (display,
                                       META_LIST_INCLUDE_OVERRIDE_REDIRECT);
  for (tmp = windows; tmp != NULL; tmp = tmp->next)
    {
      MetaWindow *window = tmp->data;

      meta_compositor_add_window (display->compositor, window);
      if (window->visible_to_compositor)
        meta_compositor_show_window (display->compositor, window,
                                     META_COMP_EFFECT_NONE);
    }

  g_slist_free (windows);
  
  /* initialize the compositor's view of the stacking order */
  meta_stack_tracker_sync_stack (screen->stack_tracker);
}

/**
 * meta_screen_for_x_screen:
 * @xscreen: an X screen structure.
 *
 * Gets the #MetaScreen corresponding to an X screen structure.
 *
 * Return value: (transfer none): the #MetaScreen for the X screen
 *   %NULL if Metacity is not managing the screen.
 */
MetaScreen*
meta_screen_for_x_screen (Screen *xscreen)
{
  MetaDisplay *display;
  
  display = meta_display_for_x_display (DisplayOfScreen (xscreen));

  if (display == NULL)
    return NULL;
  
  return meta_display_screen_for_x_screen (display, xscreen);
}

static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  MetaScreen *screen = data;
  
  if ((pref == META_PREF_NUM_WORKSPACES ||
       pref == META_PREF_DYNAMIC_WORKSPACES) &&
      !meta_prefs_get_dynamic_workspaces ())
    {
      /* GSettings doesn't provide timestamps, but luckily update_num_workspaces
       * often doesn't need it...
       */
      guint32 timestamp = 
        meta_display_get_current_time_roundtrip (screen->display);
      update_num_workspaces (screen, timestamp);
    }
  else if (pref == META_PREF_FOCUS_MODE)
    {
      update_focus_mode (screen);
    }
  else if (pref == META_PREF_WORKSPACE_NAMES)
    {
      set_workspace_names (screen);
    }
}


static char*
get_screen_name (MetaDisplay *display,
                 int          number)
{
  char *p;
  char *dname;
  char *scr;
  
  /* DisplayString gives us a sort of canonical display,
   * vs. the user-entered name from XDisplayName()
   */
  dname = g_strdup (DisplayString (display->xdisplay));

  /* Change display name to specify this screen.
   */
  p = strrchr (dname, ':');
  if (p)
    {
      p = strchr (p, '.');
      if (p)
        *p = '\0';
    }
  
  scr = g_strdup_printf ("%s.%d", dname, number);

  g_free (dname);

  return scr;
}

static gint
ptrcmp (gconstpointer a, gconstpointer b)
{
  if (a < b)
    return -1;
  else if (a > b)
    return 1;
  else
    return 0;
}

static void
listify_func (gpointer key, gpointer value, gpointer data)
{
  GSList **listp;
  
  listp = data;

  *listp = g_slist_prepend (*listp, value);
}

/**
 * meta_screen_foreach_window:
 * @screen: a #MetaScreen
 * @func: function to call for each window
 * @data: user data to pass to @func
 *
 * Calls the specified function for each window on the screen,
 * ignoring override-redirect windows.
 */
LOCAL_SYMBOL void
meta_screen_foreach_window (MetaScreen *screen,
                            MetaScreenWindowFunc func,
                            gpointer data)
{
  GSList *winlist;
  GSList *tmp;

  /* If we end up doing this often, just keeping a list
   * of windows might be sensible.
   */
  
  winlist = NULL;
  g_hash_table_foreach (screen->display->window_ids,
                        listify_func,
                        &winlist);
  
  winlist = g_slist_sort (winlist, ptrcmp);
  
  tmp = winlist;
  while (tmp != NULL)
    {
      /* If the next node doesn't contain this window
       * a second time, delete the window.
       */
      if (tmp->next == NULL ||
          (tmp->next && tmp->next->data != tmp->data))
        {
          MetaWindow *window = tmp->data;

          if (window->screen == screen && !window->override_redirect)
            (* func) (screen, window, data);
        }
      
      tmp = tmp->next;
    }
  g_slist_free (winlist);
}

static void
queue_draw (MetaScreen *screen, MetaWindow *window, gpointer data)
{
  if (window->frame)
    meta_frame_queue_draw (window->frame);
}

LOCAL_SYMBOL void
meta_screen_queue_frame_redraws (MetaScreen *screen)
{
  meta_screen_foreach_window (screen, queue_draw, NULL);
}

static void
queue_resize (MetaScreen *screen, MetaWindow *window, gpointer data)
{
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}

LOCAL_SYMBOL void
meta_screen_queue_window_resizes (MetaScreen *screen)
{
  meta_screen_foreach_window (screen, queue_resize, NULL);
}

int
meta_screen_get_n_workspaces (MetaScreen *screen)
{
  return g_list_length (screen->workspaces);
}

/**
 * meta_screen_get_workspace_by_index:
 * @screen: a #MetaScreen
 * @index: index of one of the screen's workspaces
 *
 * Gets the workspace object for one of a screen's workspaces given the workspace
 * index. It's valid to call this function with an out-of-range index and it
 * will robustly return %NULL.
 *
 * Return value: (transfer none): the workspace object with specified index, or %NULL
 *   if the index is out of range.
 */
MetaWorkspace*
meta_screen_get_workspace_by_index (MetaScreen  *screen,
                                    int          idx)
{
  GList *tmp;
  int i;

  /* should be robust, idx is maybe from an app */
  if (idx < 0)
    return NULL;
  
  i = 0;
  tmp = screen->workspaces;
  while (tmp != NULL)
    {
      MetaWorkspace *w = tmp->data;

      if (i == idx)
        return w;

      ++i;
      tmp = tmp->next;
    }

  return NULL;
}

static void
set_number_of_spaces_hint (MetaScreen *screen,
			   int         n_spaces)
{
  unsigned long data[1];

  if (screen->closing > 0)
    return;

  data[0] = n_spaces;

  meta_verbose ("Setting _NET_NUMBER_OF_DESKTOPS to %lu\n", data[0]);

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_NUMBER_OF_DESKTOPS,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_error_trap_pop (screen->display);
}

static void
set_desktop_geometry_hint (MetaScreen *screen)
{
  unsigned long data[2];

  if (screen->closing > 0)
    return;

  data[0] = screen->rect.width;
  data[1] = screen->rect.height;

  meta_verbose ("Setting _NET_DESKTOP_GEOMETRY to %lu, %lu\n", data[0], data[1]);

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_DESKTOP_GEOMETRY,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 2);
  meta_error_trap_pop (screen->display);
}

static void
set_desktop_viewport_hint (MetaScreen *screen)
{
  unsigned long data[2];

  if (screen->closing > 0)
    return;

  /*
   * Muffin does not implement viewports, so this is a fixed 0,0
   */
  data[0] = 0;
  data[1] = 0;

  meta_verbose ("Setting _NET_DESKTOP_VIEWPORT to 0, 0\n");

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_DESKTOP_VIEWPORT,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 2);
  meta_error_trap_pop (screen->display);
}

void
meta_screen_remove_workspace (MetaScreen *screen, MetaWorkspace *workspace,
                              guint32 timestamp)
{
  GList         *l;
  MetaWorkspace *neighbour = NULL;
  GList         *next = NULL;
  int            index;
  gboolean       active_index_changed;
  int            new_num;

  l = screen->workspaces;
  while (l)
    {
      MetaWorkspace *w = l->data;

      if (w == workspace)
        {
          if (l->next)
            next = l->next;

          if (l->prev)
            neighbour = l->prev->data;
          else if (l->next)
            neighbour = l->next->data;
          else
            {
              /* Cannot remove the only workspace! */
              return;
            }

          break;
        }

      l = l->next;
    }

  if (!neighbour)
    return;

  meta_workspace_relocate_windows (workspace, neighbour);

  if (workspace == screen->active_workspace)
    meta_workspace_activate (neighbour, timestamp);

  /* To emit the signal after removing the workspace */
  index = meta_workspace_index (workspace);
  active_index_changed = index < meta_screen_get_active_workspace_index (screen);

  /* This also removes the workspace from the screens list */
  meta_workspace_remove (workspace);

  new_num = g_list_length (screen->workspaces);

  set_number_of_spaces_hint (screen, new_num);

  if (!meta_prefs_get_dynamic_workspaces ())
    meta_prefs_set_num_workspaces (new_num);

  /* If deleting a workspace before the current workspace, the active
   * workspace index changes, so we need to update that hint */
  if (active_index_changed)
      meta_screen_set_active_workspace_hint (screen);

  l = next;
  while (l)
    {
      MetaWorkspace *w = l->data;

      meta_workspace_update_window_hints (w);

      l = l->next;
    }

  meta_screen_queue_workarea_recalc (screen);

  g_signal_emit (screen, screen_signals[WORKSPACE_REMOVED], 0, index);
  g_object_notify (G_OBJECT (screen), "n-workspaces");
}

/**
 * meta_screen_append_new_workspace:
 * @screen: a #MetaScreen
 * @activate: %TRUE if the workspace should be switched to after creation
 * @timestamp: if switching to a new workspace, timestamp to be used when
 *   focusing a window on the new workspace. (Doesn't hurt to pass a valid
 *   timestamp when available even if not switching workspaces.)
 *
 * Append a new workspace to the screen and (optionally) switch to that
 * screen.
 *
 * Return value: (transfer none): the newly appended workspace.
 */
MetaWorkspace *
meta_screen_append_new_workspace (MetaScreen *screen, gboolean activate,
                                  guint32 timestamp)
{
  MetaWorkspace *w;
  int new_num;

  /* This also adds the workspace to the screen list */
  w = meta_workspace_new (screen);

  if (!w)
    return NULL;

  if (activate)
    meta_workspace_activate (w, timestamp);

  new_num = g_list_length (screen->workspaces);

  set_number_of_spaces_hint (screen, new_num);

  if (!meta_prefs_get_dynamic_workspaces ())
    meta_prefs_set_num_workspaces (new_num);

  meta_screen_queue_workarea_recalc (screen);

  g_signal_emit (screen, screen_signals[WORKSPACE_ADDED],
                 0, meta_workspace_index (w));
  g_object_notify (G_OBJECT (screen), "n-workspaces");

  return w;
}


static void
update_num_workspaces (MetaScreen *screen,
                       guint32     timestamp)
{
  int new_num, old_num;
  GList *tmp;
  int i;
  GList *extras;
  MetaWorkspace *last_remaining;
  gboolean need_change_space;
  
  new_num = meta_prefs_get_num_workspaces ();

  g_assert (new_num > 0);

  if (g_list_length (screen->workspaces) == (guint) new_num)
    return;

  last_remaining = NULL;
  extras = NULL;
  i = 0;
  tmp = screen->workspaces;
  while (tmp != NULL)
    {
      MetaWorkspace *w = tmp->data;

      if (i >= new_num)
        extras = g_list_prepend (extras, w);
      else
        last_remaining = w;
          
      ++i;
      tmp = tmp->next;
    }
  old_num = i;

  g_assert (last_remaining);
  
  /* Get rid of the extra workspaces by moving all their windows
   * to last_remaining, then activating last_remaining if
   * one of the removed workspaces was active. This will be a bit
   * wacky if the config tool for changing number of workspaces
   * is on a removed workspace ;-)
   */
  need_change_space = FALSE;
  tmp = extras;
  while (tmp != NULL)
    {
      MetaWorkspace *w = tmp->data;

      meta_workspace_relocate_windows (w, last_remaining);      

      if (w == screen->active_workspace)
        need_change_space = TRUE;
      
      tmp = tmp->next;
    }

  if (need_change_space)
    meta_workspace_activate (last_remaining, timestamp);

  /* Should now be safe to free the workspaces */
  tmp = extras;
  while (tmp != NULL)
    {
      MetaWorkspace *w = tmp->data;

      g_assert (w->windows == NULL);
      meta_workspace_remove (w);

      tmp = tmp->next;
    }

  g_list_free (extras);

  for (i = old_num; i < new_num; i++)
    meta_workspace_new (screen);

  set_number_of_spaces_hint (screen, new_num);

  meta_screen_queue_workarea_recalc (screen);

  for (i = old_num; i < new_num; i++)
    g_signal_emit (screen, screen_signals[WORKSPACE_ADDED], 0, i);

  g_object_notify (G_OBJECT (screen), "n-workspaces");
}

static void
update_focus_mode (MetaScreen *screen)
{
  /* nothing to do anymore */ ;
}

LOCAL_SYMBOL void
meta_screen_set_cursor (MetaScreen *screen,
                        MetaCursor  cursor)
{
  Cursor xcursor;

  if (cursor == screen->current_cursor)
    return;

  screen->current_cursor = cursor;
  
  xcursor = meta_display_create_x_cursor (screen->display, cursor);
  XDefineCursor (screen->display->xdisplay, screen->xroot, xcursor);
  XFlush (screen->display->xdisplay);
  XFreeCursor (screen->display->xdisplay, xcursor);
}

LOCAL_SYMBOL void
meta_screen_update_cursor (MetaScreen *screen)
{
  Cursor xcursor;

  xcursor = meta_display_create_x_cursor (screen->display, 
					  screen->current_cursor);
  XDefineCursor (screen->display->xdisplay, screen->xroot, xcursor);
  XFlush (screen->display->xdisplay);
  XFreeCursor (screen->display->xdisplay, xcursor);
}

LOCAL_SYMBOL void
meta_screen_tab_popup_create (MetaScreen      *screen,
                              MetaTabList      list_type,
                              MetaTabShowType  show_type,
                              MetaWindow      *initial_selection)
{
  MetaTabEntry *entries;
  GList *tab_list;
  GList *tmp;
  int len;
  int i;

  if (screen->tab_popup)
    return;

  tab_list = meta_display_get_tab_list (screen->display,
                                        list_type,
                                        screen,
                                        screen->active_workspace);

  len = g_list_length (tab_list);

  entries = g_new (MetaTabEntry, len + 1);
  entries[len].key = NULL;
  entries[len].title = NULL;
  entries[len].icon = NULL;

  i = 0;
  tmp = tab_list;
  while (i < len)
    {
      MetaWindow *window;
      MetaRectangle r;

      window = tmp->data;

      entries[i].key = (MetaTabEntryKey) window;
      entries[i].title = window->title;
      entries[i].icon = g_object_ref (window->icon);
      entries[i].blank = FALSE;
      entries[i].hidden = !meta_window_showing_on_its_workspace (window);
      entries[i].demands_attention = window->wm_state_demands_attention;

      if (show_type == META_TAB_SHOW_INSTANTLY ||
          !entries[i].hidden                   ||
          !meta_window_get_icon_geometry (window, &r))
        meta_window_get_outer_rect (window, &r);

      entries[i].rect = r;

      /* Find inside of highlight rectangle to be used when window is
       * outlined for tabbing.  This should be the size of the
       * east/west frame, and the size of the south frame, on those
       * sides.  On the top it should be the size of the south frame
       * edge.
       */
#define OUTLINE_WIDTH 5
      /* Top side */
      if (!entries[i].hidden &&
          window->frame && window->frame->bottom_height > 0 &&
          window->frame->child_y >= window->frame->bottom_height)
        entries[i].inner_rect.y = window->frame->bottom_height;
      else
        entries[i].inner_rect.y = OUTLINE_WIDTH;

      /* Bottom side */
      if (!entries[i].hidden &&
          window->frame && window->frame->bottom_height != 0)
        entries[i].inner_rect.height = r.height
          - entries[i].inner_rect.y - window->frame->bottom_height;
      else
        entries[i].inner_rect.height = r.height
          - entries[i].inner_rect.y - OUTLINE_WIDTH;

      /* Left side */
      if (!entries[i].hidden && window->frame && window->frame->child_x != 0)
        entries[i].inner_rect.x = window->frame->child_x;
      else
        entries[i].inner_rect.x = OUTLINE_WIDTH;

      /* Right side */
      if (!entries[i].hidden &&
          window->frame && window->frame->right_width != 0)
        entries[i].inner_rect.width = r.width
          - entries[i].inner_rect.x - window->frame->right_width;
      else
        entries[i].inner_rect.width = r.width
          - entries[i].inner_rect.x - OUTLINE_WIDTH;

      ++i;
      tmp = tmp->next;
    }

  if (!meta_prefs_get_no_tab_popup ())
    screen->tab_popup = meta_ui_tab_popup_new (entries,
                                               screen->number,
                                               len,
                                               5, /* FIXME */
                                               TRUE);

  for (i = 0; i < len; i++)
    g_object_unref (entries[i].icon);

  g_free (entries);

  g_list_free (tab_list);

  meta_ui_tab_popup_select (screen->tab_popup,
                            (MetaTabEntryKey) initial_selection);

  if (show_type != META_TAB_SHOW_INSTANTLY)
    meta_ui_tab_popup_set_showing (screen->tab_popup, TRUE);
}

LOCAL_SYMBOL void
meta_screen_tab_popup_forward (MetaScreen *screen)
{
  g_return_if_fail (screen->tab_popup != NULL);

  meta_ui_tab_popup_forward (screen->tab_popup);
}

LOCAL_SYMBOL void
meta_screen_tab_popup_backward (MetaScreen *screen)
{
  g_return_if_fail (screen->tab_popup != NULL);

  meta_ui_tab_popup_backward (screen->tab_popup);
}

LOCAL_SYMBOL MetaWindow *
meta_screen_tab_popup_get_selected (MetaScreen *screen)
{
  g_return_val_if_fail (screen->tab_popup != NULL, NULL);

  return (MetaWindow *) meta_ui_tab_popup_get_selected (screen->tab_popup);
}

LOCAL_SYMBOL void
meta_screen_tab_popup_destroy (MetaScreen *screen)
{
  if (screen->tab_popup)
    {
      meta_ui_tab_popup_free (screen->tab_popup);
      screen->tab_popup = NULL;
    }
}

LOCAL_SYMBOL void
meta_screen_workspace_popup_create (MetaScreen    *screen,
                                    MetaWorkspace *initial_selection)
{
  MetaTabEntry *entries;
  int len;
  int i;
  MetaWorkspaceLayout layout;
  int n_workspaces;
  int current_workspace;

  if (screen->ws_popup || meta_prefs_get_no_tab_popup ())
    return;

  current_workspace = meta_workspace_index (screen->active_workspace);
  n_workspaces = meta_screen_get_n_workspaces (screen);

  meta_screen_calc_workspace_layout (screen, n_workspaces,
                                     current_workspace, &layout);

  len = layout.grid_area;
  
  entries = g_new (MetaTabEntry, len + 1);
  entries[len].key = NULL;
  entries[len].title = NULL;
  entries[len].icon = NULL;

  i = 0;
  while (i < len)
    {
      if (layout.grid[i] >= 0)
        {
          MetaWorkspace *workspace;
          
          workspace = meta_screen_get_workspace_by_index (screen,
                                                          layout.grid[i]);
          
          entries[i].key = (MetaTabEntryKey) workspace;
          entries[i].title = meta_workspace_get_name (workspace);
          entries[i].icon = NULL;
          entries[i].blank = FALSE;
          
          g_assert (entries[i].title != NULL);
        }
      else
        {
          entries[i].key = NULL;
          entries[i].title = NULL;
          entries[i].icon = NULL;
          entries[i].blank = TRUE;
        }
      entries[i].hidden = FALSE;
      entries[i].demands_attention = FALSE;

      ++i;
    }

  screen->ws_popup = meta_ui_tab_popup_new (entries,
                                            screen->number,
                                            len,
                                            layout.cols,
                                            FALSE);

  g_free (entries);
  meta_screen_free_workspace_layout (&layout);

  meta_ui_tab_popup_select (screen->ws_popup,
                            (MetaTabEntryKey) initial_selection);
  meta_ui_tab_popup_set_showing (screen->ws_popup, TRUE);
}

LOCAL_SYMBOL void
meta_screen_workspace_popup_select (MetaScreen    *screen,
                                    MetaWorkspace *workspace)
{
  g_return_if_fail (screen->ws_popup != NULL);

  meta_ui_tab_popup_select (screen->ws_popup,
                            (MetaTabEntryKey) workspace);
}

LOCAL_SYMBOL MetaWorkspace *
meta_screen_workspace_popup_get_selected (MetaScreen *screen)
{
  g_return_val_if_fail (screen->ws_popup != NULL, NULL);

  return (MetaWorkspace *) meta_ui_tab_popup_get_selected (screen->ws_popup);
}

LOCAL_SYMBOL void
meta_screen_workspace_popup_destroy (MetaScreen *screen)
{
  if (screen->ws_popup)
    {
      meta_ui_tab_popup_free (screen->ws_popup);
      screen->ws_popup = NULL;
    }
}

static gboolean
snap_osd_timeout (void *data)
{
  MetaScreen *screen = data;
  if (meta_screen_tile_preview_get_visible (screen) ||
      meta_screen_tile_hud_get_visible (screen))
      g_signal_emit (screen, screen_signals[SNAP_OSD_SHOW], 0);
  screen->snap_osd_timeout_id = 0;
  return FALSE;
}

static gboolean
maybe_hide_snap_osd (void *data)
{
    MetaScreen *screen = data;
    if (!meta_screen_tile_preview_get_visible (screen) &&
        !meta_screen_tile_hud_get_visible (screen)) {
        if (screen->snap_osd_timeout_id > 0) {
            g_source_remove (screen->snap_osd_timeout_id);
            screen->snap_osd_timeout_id = 0;
        }
        g_signal_emit (screen, screen_signals[SNAP_OSD_HIDE], 0);
    }
    return FALSE;
}

static gboolean
meta_screen_tile_preview_update_timeout (gpointer data)
{
  MetaScreen *screen = data;
  MetaWindow *window = screen->display->grab_window;
  gboolean needs_preview = FALSE;

  screen->tile_preview_timeout_id = 0;

  if (!screen->tile_preview)
    {
      Window xwindow;
      gulong create_serial;
      screen->tile_preview = meta_tile_preview_new (screen->number);
      xwindow = meta_tile_preview_get_xwindow (screen->tile_preview,
                                               &create_serial);
      meta_stack_tracker_record_add (screen->stack_tracker,
                                     xwindow,
                                     create_serial);
    }

  if (window && window->mouse_on_edge)
    {
      switch (window->tile_mode)
        {
          case META_TILE_LEFT:
          case META_TILE_RIGHT:
              if (!META_WINDOW_TILED_SIDE_BY_SIDE (window))
                needs_preview = TRUE;
              break;
          case META_TILE_ULC:
          case META_TILE_LLC:
          case META_TILE_URC:
          case META_TILE_LRC:
              if (!META_WINDOW_TILED_CORNER (window))
                needs_preview = TRUE;
              break;
          case META_TILE_TOP:
          case META_TILE_BOTTOM:
              if (!META_WINDOW_TILED_TOP_BOTTOM (window))
                needs_preview = TRUE;
              break;
          case META_TILE_MAXIMIZE:
              if (!META_WINDOW_MAXIMIZED (window))
                needs_preview = TRUE;
              break;
          default:
              needs_preview = FALSE;
              break;
        }
    }

  if (needs_preview)
    {
      MetaRectangle tile_rect;

      meta_window_get_current_tile_area (window, &tile_rect);
      meta_tile_preview_show (screen->tile_preview, &tile_rect, window->snap_queued);
      if (screen->snap_osd_timeout_id == 0)
        screen->snap_osd_timeout_id = g_timeout_add_seconds (SNAP_OSD_TIMEOUT,
                                                             snap_osd_timeout,
                                                             screen);
    }
  else
    meta_tile_preview_hide (screen->tile_preview);

  return FALSE;
}

#define TILE_PREVIEW_TIMEOUT_MS 200

LOCAL_SYMBOL void
meta_screen_tile_preview_update (MetaScreen *screen,
                                 gboolean    delay)
{
  if (delay && !meta_screen_tile_preview_get_visible (screen))
    {
      if (screen->tile_preview_timeout_id > 0)
        return;

      screen->tile_preview_timeout_id =
        g_timeout_add (TILE_PREVIEW_TIMEOUT_MS,
                       meta_screen_tile_preview_update_timeout,
                       screen);
    }
  else
    {
      if (screen->tile_preview_timeout_id > 0)
        g_source_remove (screen->tile_preview_timeout_id);

      meta_screen_tile_preview_update_timeout ((gpointer)screen);
    }
}

LOCAL_SYMBOL void
meta_screen_tile_preview_hide (MetaScreen *screen)
{
  if (screen->tile_preview_timeout_id > 0)
    g_source_remove (screen->tile_preview_timeout_id);

  if (screen->tile_preview)
    meta_tile_preview_hide (screen->tile_preview);

  g_timeout_add (250, maybe_hide_snap_osd, screen);
}

LOCAL_SYMBOL gboolean
meta_screen_tile_preview_get_visible (MetaScreen *screen)
{
    if (screen->tile_preview == NULL)
        return FALSE;

    return meta_tile_preview_get_visible (screen->tile_preview);
}

static gboolean
meta_screen_tile_hud_update_timeout (gpointer data)
{
  MetaScreen *screen = data;
  MetaWindow *window = screen->display->grab_window;
  if (!screen->tile_hud)
    {
      Window xwindow;
      gulong create_serial;

      screen->tile_hud = meta_tile_hud_new (screen->number);
      xwindow = meta_tile_hud_get_xwindow (screen->tile_hud,
                                           &create_serial);
      meta_stack_tracker_record_add (screen->stack_tracker,
                                     xwindow,
                                     create_serial);
    }

  if (!screen->hud_hiding && window != NULL && window->current_proximity_zone != ZONE_NONE)
    {
      HUDTileRestrictions rest = meta_window_get_tile_restrictions (window);
      MetaRectangle work_area;
      /* This bit is liable to get more complicated when there are multiple
         monitors involved - we'll have partial hud, with a bare area at the
         monitor 'joint' or something... */
      const MetaMonitorInfo *monitor;
      monitor = meta_screen_get_current_monitor (screen);
      meta_window_get_work_area_for_monitor (window, monitor->number, &work_area);
      meta_tile_hud_show (screen->tile_hud,
                          &work_area, screen->hud_opacity,
                          window->snap_queued,
                          rest,
                          window->current_proximity_zone);
    if (screen->snap_osd_timeout_id == 0)
        screen->snap_osd_timeout_id = g_timeout_add_seconds (SNAP_OSD_TIMEOUT,
                                                             snap_osd_timeout,
                                                             screen);
    }
  else {
      meta_tile_hud_fade_out (screen->tile_hud, screen->hud_opacity, FALSE);
      g_timeout_add (250, maybe_hide_snap_osd, screen);
  }

  if (!screen->hud_hiding && screen->hud_opacity < 1.0) {
    screen->hud_opacity += 0.05;
    return TRUE;
  } else if (screen->hud_hiding && screen->hud_opacity > 0.0) {
    screen->hud_opacity -= 0.05;
    return TRUE;
  } else {
    screen->tile_hud_fade_timeout_id = 0;
    return FALSE;
  }
}

#define TILE_HUD_TIMEOUT_MS 100
#define TILE_HUD_FADE_IN_MS 25
#define TILE_HUD_FADE_OUT_MS 15

static gboolean
do_hud_fade_in_out (gpointer data)
{
    MetaScreen *screen = META_SCREEN (data);
    screen->tile_hud_timeout_id = 0;

    if (screen->hud_hiding) 
        screen->tile_hud_fade_timeout_id = 
            g_timeout_add (TILE_HUD_FADE_OUT_MS,
                           meta_screen_tile_hud_update_timeout,
                           screen);
    else
        screen->tile_hud_fade_timeout_id = 
            g_timeout_add (TILE_HUD_FADE_IN_MS,
                           meta_screen_tile_hud_update_timeout,
                           screen);

    return FALSE;
}

LOCAL_SYMBOL void
meta_screen_tile_hud_update (MetaScreen *screen,
                              gboolean    delay,
                              gboolean    hiding)
{
  if (screen->hud_hiding != hiding) {
    if (screen->tile_hud_fade_timeout_id > 0) {
        g_source_remove (screen->tile_hud_fade_timeout_id);
        screen->tile_hud_fade_timeout_id = 0;
    }
    screen->hud_hiding = hiding;
  }

  if (delay && !meta_screen_tile_hud_get_visible (screen))
    {
      if (screen->tile_hud_timeout_id > 0)
        return;

      screen->tile_hud_timeout_id =
        g_timeout_add (TILE_HUD_TIMEOUT_MS,
                       do_hud_fade_in_out,
                       screen);
    }
  else
    {
      if (screen->tile_hud_timeout_id > 0) {
        g_source_remove (screen->tile_hud_timeout_id);
        screen->tile_hud_timeout_id = 0;
      }

      do_hud_fade_in_out ((gpointer)screen);
    }
}

LOCAL_SYMBOL void
meta_screen_tile_hud_hide (MetaScreen *screen)
{
  if (screen->tile_hud_timeout_id > 0)
    g_source_remove (screen->tile_hud_timeout_id);
  if (screen->tile_hud_fade_timeout_id > 0)
    g_source_remove (screen->tile_hud_fade_timeout_id);

  if (screen->tile_hud)
    meta_tile_hud_hide (screen->tile_hud);
  screen->hud_opacity = 0.0;
  screen->hud_hiding = FALSE;
  screen->tile_hud_timeout_id = 0;

  g_timeout_add (250, maybe_hide_snap_osd, screen);
}

LOCAL_SYMBOL gboolean
meta_screen_tile_hud_get_visible (MetaScreen *screen)
{
    if (screen->tile_hud == NULL)
        return FALSE;

    return meta_tile_hud_get_visible (screen->tile_hud);
}

LOCAL_SYMBOL void
meta_screen_hide_hud_and_preview (MetaScreen *screen)
{
    meta_screen_tile_hud_hide (screen);
    meta_screen_tile_preview_hide (screen);
}

/**
 * meta_screen_get_mouse_window:
 * @screen: an X screen structure.
 * @not_this_one: (allow-none): window to be excluded
 *
 * Gets the #MetaWindow pointed by the mouse
 *
 * Return value: (transfer none): the #MetaWindow pointed by the mouse
 *  %NULL when window not found
 */
MetaWindow*
meta_screen_get_mouse_window (MetaScreen  *screen,
                              MetaWindow  *not_this_one)
{
  MetaWindow *window;
  Window root_return, child_return;
  int root_x_return, root_y_return;
  int win_x_return, win_y_return;
  unsigned int mask_return;
  
  if (not_this_one)
    meta_topic (META_DEBUG_FOCUS,
                "Focusing mouse window excluding %s\n", not_this_one->desc);

  meta_error_trap_push (screen->display);
  XQueryPointer (screen->display->xdisplay,
                 screen->xroot,
                 &root_return,
                 &child_return,
                 &root_x_return,
                 &root_y_return,
                 &win_x_return,
                 &win_y_return,
                 &mask_return);
  meta_error_trap_pop (screen->display);

  window = meta_stack_get_default_focus_window_at_point (screen->stack,
                                                         screen->active_workspace,
                                                         not_this_one,
                                                         root_x_return,
                                                         root_y_return);

  return window;
}

LOCAL_SYMBOL const MetaMonitorInfo*
meta_screen_get_monitor_for_rect (MetaScreen    *screen,
                                  MetaRectangle *rect)
{
  int i;
  int best_monitor, monitor_score;

  if (screen->n_monitor_infos == 1)
    return &screen->monitor_infos[0];

  best_monitor = 0;
  monitor_score = 0;

  for (i = 0; i < screen->n_monitor_infos; i++)
    {
      MetaRectangle dest;
      if (meta_rectangle_intersect (&screen->monitor_infos[i].rect,
                                    rect,
                                    &dest))
        {
          int cur = meta_rectangle_area (&dest);
          if (cur > monitor_score)
            {
              monitor_score = cur;
              best_monitor = i;
            }
        }
    }

  return &screen->monitor_infos[best_monitor];
}

LOCAL_SYMBOL const MetaMonitorInfo*
meta_screen_get_monitor_for_window (MetaScreen *screen,
                                    MetaWindow *window)
{
  MetaRectangle window_rect;
  
  meta_window_get_outer_rect (window, &window_rect);

  return meta_screen_get_monitor_for_rect (screen, &window_rect);
}

LOCAL_SYMBOL const MetaMonitorInfo* 
meta_screen_get_monitor_neighbor (MetaScreen         *screen,
                                  int                 which_monitor,
                                  MetaScreenDirection direction)
{
  MetaMonitorInfo* input = screen->monitor_infos + which_monitor;
  MetaMonitorInfo* current;
  int i;

  for (i = 0; i < screen->n_monitor_infos; i++)
    {
      current = screen->monitor_infos + i;

      if ((direction == META_SCREEN_RIGHT && 
           current->rect.x == input->rect.x + input->rect.width &&
           meta_rectangle_vert_overlap(&current->rect, &input->rect)) ||
          (direction == META_SCREEN_LEFT && 
           input->rect.x == current->rect.x + current->rect.width &&
           meta_rectangle_vert_overlap(&current->rect, &input->rect)) ||
          (direction == META_SCREEN_UP && 
           input->rect.y == current->rect.y + current->rect.height &&
           meta_rectangle_horiz_overlap(&current->rect, &input->rect)) ||
          (direction == META_SCREEN_DOWN && 
           current->rect.y == input->rect.y + input->rect.height &&
           meta_rectangle_horiz_overlap(&current->rect, &input->rect)))
        {
          return current;
        }
    }
  
  return NULL;
}

LOCAL_SYMBOL void
meta_screen_get_natural_monitor_list (MetaScreen *screen,
                                      int**       monitors_list,
                                      int*        n_monitors)
{
  const MetaMonitorInfo* current;
  const MetaMonitorInfo* tmp;
  GQueue* monitor_queue;
  int* visited;
  int cur = 0;
  int i;

  *n_monitors = screen->n_monitor_infos;
  *monitors_list = g_new (int, screen->n_monitor_infos);

  /* we calculate a natural ordering by which to choose monitors for
   * window placement.  We start at the current monitor, and perform
   * a breadth-first search of the monitors starting from that
   * monitor.  We choose preferentially left, then right, then down,
   * then up.  The visitation order produced by this traversal is the
   * natural monitor ordering.
   */

  visited = g_new (int, screen->n_monitor_infos);
  for (i = 0; i < screen->n_monitor_infos; i++)
    {
      visited[i] = FALSE;
    }

  current = meta_screen_get_current_monitor (screen);
  monitor_queue = g_queue_new ();
  g_queue_push_tail (monitor_queue, (gpointer) current);
  visited[current->number] = TRUE;

  while (!g_queue_is_empty (monitor_queue))
    {
      current = (const MetaMonitorInfo*) 
        g_queue_pop_head (monitor_queue);

      (*monitors_list)[cur++] = current->number;

      /* enqueue each of the directions */
      tmp = meta_screen_get_monitor_neighbor (screen,
                                              current->number,
                                              META_SCREEN_LEFT);
      if (tmp && !visited[tmp->number])
        {
          g_queue_push_tail (monitor_queue,
                             (MetaMonitorInfo*) tmp);
          visited[tmp->number] = TRUE;
        }
      tmp = meta_screen_get_monitor_neighbor (screen,
                                              current->number,
                                              META_SCREEN_RIGHT);
      if (tmp && !visited[tmp->number])
        {
          g_queue_push_tail (monitor_queue,
                             (MetaMonitorInfo*) tmp);
          visited[tmp->number] = TRUE;
        }
      tmp = meta_screen_get_monitor_neighbor (screen,
                                              current->number,
                                              META_SCREEN_UP);
      if (tmp && !visited[tmp->number])
        {
          g_queue_push_tail (monitor_queue,
                             (MetaMonitorInfo*) tmp);
          visited[tmp->number] = TRUE;
        }
      tmp = meta_screen_get_monitor_neighbor (screen,
                                              current->number,
                                              META_SCREEN_DOWN);
      if (tmp && !visited[tmp->number])
        {
          g_queue_push_tail (monitor_queue,
                             (MetaMonitorInfo*) tmp);
          visited[tmp->number] = TRUE;
        }
    }

  /* in case we somehow missed some set of monitors, go through the
   * visited list and add in any monitors that were missed
   */
  for (i = 0; i < screen->n_monitor_infos; i++)
    {
      if (visited[i] == FALSE)
        {
          (*monitors_list)[cur++] = i;
        }
    }

  g_free (visited);
  g_queue_free (monitor_queue);
}

LOCAL_SYMBOL const MetaMonitorInfo*
meta_screen_get_current_monitor (MetaScreen *screen)
{
  if (screen->n_monitor_infos == 1)
    return &screen->monitor_infos[0];
  
  /* Sadly, we have to do it this way. Yuck.
   */
  
  if (screen->display->monitor_cache_invalidated)
    {
      Window root_return, child_return;
      int win_x_return, win_y_return;
      unsigned int mask_return;
      int i;
      MetaRectangle pointer_position;
      
      screen->display->monitor_cache_invalidated = FALSE;
      
      pointer_position.width = pointer_position.height = 1;
      XQueryPointer (screen->display->xdisplay,
                     screen->xroot,
                     &root_return,
                     &child_return,
                     &pointer_position.x,
                     &pointer_position.y,
                     &win_x_return,
                     &win_y_return,
                     &mask_return);

      screen->last_monitor_index = 0;
      for (i = 0; i < screen->n_monitor_infos; i++)
        {
          if (meta_rectangle_contains_rect (&screen->monitor_infos[i].rect,
                                            &pointer_position))
            {
              screen->last_monitor_index = i;
              break;
            }
        }
      
      meta_topic (META_DEBUG_XINERAMA,
                  "Rechecked current monitor, now %d\n",
                  screen->last_monitor_index);
    }

  return &screen->monitor_infos[screen->last_monitor_index];
}

/**
 * meta_screen_get_n_monitors:
 * @screen: a #MetaScreen
 *
 * Gets the number of monitors that are joined together to form @screen.
 *
 * Return value: the number of monitors
 */
int
meta_screen_get_n_monitors (MetaScreen *screen)
{
  g_return_val_if_fail (META_IS_SCREEN (screen), 0);

  return screen->n_monitor_infos;
}

/**
 * meta_screen_get_primary_monitor:
 * @screen: a #MetaScreen
 *
 * Gets the index of the primary monitor on this @screen.
 *
 * Return value: a monitor index
 */
int
meta_screen_get_primary_monitor (MetaScreen *screen)
{
  g_return_val_if_fail (META_IS_SCREEN (screen), 0);

  return screen->primary_monitor_index;
}

/**
 * meta_screen_get_monitor_geometry:
 * @screen: a #MetaScreen
 * @monitor: the monitor number
 * @geometry: (out): location to store the monitor geometry
 *
 * Stores the location and size of the indicated monitor in @geometry.
 */
void
meta_screen_get_monitor_geometry (MetaScreen    *screen,
                                  int            monitor,
                                  MetaRectangle *geometry)
{
  g_return_if_fail (META_IS_SCREEN (screen));
  g_return_if_fail (monitor >= 0 && monitor < screen->n_monitor_infos);
  g_return_if_fail (geometry != NULL);

  *geometry = screen->monitor_infos[monitor].rect;
}

#define _NET_WM_ORIENTATION_HORZ 0
#define _NET_WM_ORIENTATION_VERT 1

#define _NET_WM_TOPLEFT     0
#define _NET_WM_TOPRIGHT    1
#define _NET_WM_BOTTOMRIGHT 2
#define _NET_WM_BOTTOMLEFT  3

LOCAL_SYMBOL void
meta_screen_update_workspace_layout (MetaScreen *screen)
{
  gulong *list;
  int n_items;

  if (screen->workspace_layout_overridden)
    return;
  
  list = NULL;
  n_items = 0;

  if (meta_prop_get_cardinal_list (screen->display,
                                   screen->xroot,
                                   screen->display->atom__NET_DESKTOP_LAYOUT,
                                   &list, &n_items))
    {
      if (n_items == 3 || n_items == 4)
        {
          int cols, rows;
          
          switch (list[0])
            {
            case _NET_WM_ORIENTATION_HORZ:
              screen->vertical_workspaces = FALSE;
              break;
            case _NET_WM_ORIENTATION_VERT:
              screen->vertical_workspaces = TRUE;
              break;
            default:
              meta_warning ("Someone set a weird orientation in _NET_DESKTOP_LAYOUT\n");
              break;
            }

          cols = list[1];
          rows = list[2];

          if (rows <= 0 && cols <= 0)
            {
              meta_warning ("Columns = %d rows = %d in _NET_DESKTOP_LAYOUT makes no sense\n", rows, cols);
            }
          else
            {
              if (rows > 0)
                screen->rows_of_workspaces = rows;
              else
                screen->rows_of_workspaces = -1;
              
              if (cols > 0)
                screen->columns_of_workspaces = cols;
              else
                screen->columns_of_workspaces = -1;
            }

          if (n_items == 4)
            {
              switch (list[3])
                {
                  case _NET_WM_TOPLEFT:
                    screen->starting_corner = META_SCREEN_TOPLEFT;
                    break;
                  case _NET_WM_TOPRIGHT:
                    screen->starting_corner = META_SCREEN_TOPRIGHT;
                    break;
                  case _NET_WM_BOTTOMRIGHT:
                    screen->starting_corner = META_SCREEN_BOTTOMRIGHT;
                    break;
                  case _NET_WM_BOTTOMLEFT:
                    screen->starting_corner = META_SCREEN_BOTTOMLEFT;
                    break;
                  default:
                    meta_warning ("Someone set a weird starting corner in _NET_DESKTOP_LAYOUT\n");
                    break;
                }
            }
          else
            screen->starting_corner = META_SCREEN_TOPLEFT;
        }
      else
        {
          meta_warning ("Someone set _NET_DESKTOP_LAYOUT to %d integers instead of 4 "
                        "(3 is accepted for backwards compat)\n", n_items);
        }

      meta_XFree (list);
    }

  meta_verbose ("Workspace layout rows = %d cols = %d orientation = %d starting corner = %u\n",
                screen->rows_of_workspaces,
                screen->columns_of_workspaces,
                screen->vertical_workspaces,
                screen->starting_corner);
}

/**
 * meta_screen_override_workspace_layout:
 * @screen: a #MetaScreen
 * @starting_corner: the corner at which the first workspace is found
 * @vertical_layout: if %TRUE the workspaces are laid out in columns rather than rows
 * @n_rows: number of rows of workspaces, or -1 to determine the number of rows from
 *   @n_columns and the total number of workspaces
 * @n_columns: number of columns of workspaces, or -1 to determine the number of columns from
 *   @n_rows and the total number of workspaces
 *
 * Explicitly set the layout of workspaces. Once this has been called, the contents of the
 * _NET_DESKTOP_LAYOUT property on the root window are completely ignored.
 */
void
meta_screen_override_workspace_layout (MetaScreen      *screen,
                                       MetaScreenCorner starting_corner,
                                       gboolean         vertical_layout,
                                       int              n_rows,
                                       int              n_columns)
{
  g_return_if_fail (META_IS_SCREEN (screen));
  g_return_if_fail (n_rows > 0 || n_columns > 0);
  g_return_if_fail (n_rows != 0 && n_columns != 0);

  screen->workspace_layout_overridden = TRUE;
  screen->vertical_workspaces = vertical_layout != FALSE;
  screen->starting_corner = starting_corner;
  screen->rows_of_workspaces = n_rows;
  screen->columns_of_workspaces = n_columns;

  /* In theory we should remove _NET_DESKTOP_LAYOUT from _NET_SUPPORTED at this
   * point, but it's unlikely that anybody checks that, and it's unlikely that
   * anybody who checks that handles changes, so we'd probably just create
   * a race condition. And it's hard to implement with the code in set_supported_hint()
   */
}

static void
set_workspace_names (MetaScreen *screen)
{
  /* This updates names on root window when the pref changes,
   * note we only get prefs change notify if things have
   * really changed.
   */
  GString *flattened;
  int i;
  int n_spaces;

  /* flatten to nul-separated list */
  n_spaces = meta_screen_get_n_workspaces (screen);
  flattened = g_string_new ("");
  i = 0;
  while (i < n_spaces)
    {
      const char *name;

      name = meta_prefs_get_workspace_name (i);

      if (name)
        g_string_append_len (flattened, name,
                             strlen (name) + 1);
      else
        g_string_append_len (flattened, "", 1);
      
      ++i;
    }
  
  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay,
                   screen->xroot,
                   screen->display->atom__NET_DESKTOP_NAMES,
		   screen->display->atom_UTF8_STRING,
                   8, PropModeReplace,
		   (unsigned char *)flattened->str, flattened->len);
  meta_error_trap_pop (screen->display);
  
  g_string_free (flattened, TRUE);
}

LOCAL_SYMBOL void
meta_screen_update_workspace_names (MetaScreen *screen)
{
  char **names;
  int n_names;
  int i;

  /* this updates names in prefs when the root window property changes,
   * iff the new property contents don't match what's already in prefs
   */
  
  names = NULL;
  n_names = 0;
  if (!meta_prop_get_utf8_list (screen->display,
                                screen->xroot,
                                screen->display->atom__NET_DESKTOP_NAMES,
                                &names, &n_names))
    {
      meta_verbose ("Failed to get workspace names from root window %d\n",
                    screen->number);
      return;
    }

  i = 0;
  while (i < n_names)
    {
      meta_topic (META_DEBUG_PREFS,
                  "Setting workspace %d name to \"%s\" due to _NET_DESKTOP_NAMES change\n",
                  i, names[i] ? names[i] : "null");
      meta_prefs_change_workspace_name (i, names[i]);
      
      ++i;
    }
  
  g_strfreev (names);
}

LOCAL_SYMBOL Window
meta_create_offscreen_window (Display *xdisplay,
                              Window   parent,
                              long     valuemask)
{
  XSetWindowAttributes attrs;

  /* we want to be override redirect because sometimes we
   * create a window on a screen we aren't managing.
   * (but on a display we are managing at least one screen for)
   */
  attrs.override_redirect = True;
  attrs.event_mask = valuemask;
  
  return XCreateWindow (xdisplay,
                        parent,
                        -100, -100, 1, 1,
                        0,
                        CopyFromParent,
                        CopyFromParent,
                        (Visual *)CopyFromParent,
                        CWOverrideRedirect | CWEventMask,
                        &attrs);
}

static void
set_work_area_hint (MetaScreen *screen)
{
  int num_workspaces;
  GList *tmp_list;
  unsigned long *data, *tmp;
  MetaRectangle area;
  
  num_workspaces = meta_screen_get_n_workspaces (screen);
  data = g_new (unsigned long, num_workspaces * 4);
  tmp_list = screen->workspaces;
  tmp = data;
  
  while (tmp_list != NULL)
    {
      MetaWorkspace *workspace = tmp_list->data;

      if (workspace->screen == screen)
        {
          meta_workspace_get_work_area_all_monitors (workspace, &area);
          tmp[0] = area.x;
          tmp[1] = area.y;
          tmp[2] = area.width;
          tmp[3] = area.height;

	  tmp += 4;
        }
      
      tmp_list = tmp_list->next;
    }
  
  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
		   screen->display->atom__NET_WORKAREA,
		   XA_CARDINAL, 32, PropModeReplace,
		   (guchar*) data, num_workspaces*4);
  g_free (data);
  meta_error_trap_pop (screen->display);

  g_signal_emit (screen, screen_signals[WORKAREAS_CHANGED], 0);
}

static gboolean
set_work_area_later_func (MetaScreen *screen)
{
  meta_topic (META_DEBUG_WORKAREA,
              "Running work area hint computation function\n");
  
  screen->work_area_later = 0;
  
  set_work_area_hint (screen);
  
  return FALSE;
}

LOCAL_SYMBOL void
meta_screen_queue_workarea_recalc (MetaScreen *screen)
{
  /* Recompute work area later before redrawing */
  if (screen->work_area_later == 0)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Adding work area hint computation function\n");
      screen->work_area_later =
        meta_later_add (META_LATER_BEFORE_REDRAW,
                        (GSourceFunc) set_work_area_later_func,
                        screen,
                        NULL);
    }
}


#ifdef WITH_VERBOSE_MODE
static char *
meta_screen_corner_to_string (MetaScreenCorner corner)
{
  switch (corner)
    {
    case META_SCREEN_TOPLEFT:
      return "TopLeft";
    case META_SCREEN_TOPRIGHT:
      return "TopRight";
    case META_SCREEN_BOTTOMLEFT:
      return "BottomLeft";
    case META_SCREEN_BOTTOMRIGHT:
      return "BottomRight";
    }

  return "Unknown";
}
#endif /* WITH_VERBOSE_MODE */

LOCAL_SYMBOL void
meta_screen_calc_workspace_layout (MetaScreen          *screen,
                                   int                  num_workspaces,
                                   int                  current_space,
                                   MetaWorkspaceLayout *layout)
{
  int rows, cols;
  int grid_area;
  int *grid;
  int i, r, c;
  int current_row, current_col;
  
  rows = screen->rows_of_workspaces;
  cols = screen->columns_of_workspaces;
  if (rows <= 0 && cols <= 0)
    cols = num_workspaces;

  if (rows <= 0)
    rows = num_workspaces / cols + ((num_workspaces % cols) > 0 ? 1 : 0);
  if (cols <= 0)
    cols = num_workspaces / rows + ((num_workspaces % rows) > 0 ? 1 : 0);

  /* paranoia */
  if (rows < 1)
    rows = 1;
  if (cols < 1)
    cols = 1;

  g_assert (rows != 0 && cols != 0);
  
  grid_area = rows * cols;
  
  meta_verbose ("Getting layout rows = %d cols = %d current = %d "
                "num_spaces = %d vertical = %s corner = %s\n",
                rows, cols, current_space, num_workspaces,
                screen->vertical_workspaces ? "(true)" : "(false)",
                meta_screen_corner_to_string (screen->starting_corner));
  
  /* ok, we want to setup the distances in the workspace array to go     
   * in each direction. Remember, there are many ways that a workspace   
   * array can be setup.                                                 
   * see http://www.freedesktop.org/standards/wm-spec/1.2/html/x109.html 
   * and look at the _NET_DESKTOP_LAYOUT section for details.            
   * For instance:
   */
  /* starting_corner = META_SCREEN_TOPLEFT                         
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       1234                                    1357            
   *       5678                                    2468            
   *                                                               
   * starting_corner = META_SCREEN_TOPRIGHT                        
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       4321                                    7531            
   *       8765                                    8642            
   *                                                               
   * starting_corner = META_SCREEN_BOTTOMLEFT                      
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       5678                                    2468            
   *       1234                                    1357            
   *                                                               
   * starting_corner = META_SCREEN_BOTTOMRIGHT                     
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       8765                                    8642            
   *       4321                                    7531            
   *
   */
  /* keep in mind that we could have a ragged layout, e.g. the "8"
   * in the above grids could be missing
   */

  
  grid = g_new (int, grid_area);

  current_row = -1;
  current_col = -1;
  i = 0;
  
  switch (screen->starting_corner) 
    {
    case META_SCREEN_TOPLEFT:
      if (screen->vertical_workspaces) 
        {
          c = 0;
          while (c < cols)
            {
              r = 0;
              while (r < rows)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++r;
                }
              ++c;
            }
        }
      else
        {
          r = 0;
          while (r < rows)
            {
              c = 0;
              while (c < cols)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++c;
                }
              ++r;
            }
        }
      break;
    case META_SCREEN_TOPRIGHT:
      if (screen->vertical_workspaces) 
        {
          c = cols - 1;
          while (c >= 0)
            {
              r = 0;
              while (r < rows)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++r;
                }
              --c;
            }
        }
      else
        {
          r = 0;
          while (r < rows)
            {
              c = cols - 1;
              while (c >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --c;
                }
              ++r;
            }
        }
      break;
    case META_SCREEN_BOTTOMLEFT:
      if (screen->vertical_workspaces) 
        {
          c = 0;
          while (c < cols)
            {
              r = rows - 1;
              while (r >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --r;
                }
              ++c;
            }
        }
      else
        {
          r = rows - 1;
          while (r >= 0)
            {
              c = 0;
              while (c < cols)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++c;
                }
              --r;
            }
        }
      break;
    case META_SCREEN_BOTTOMRIGHT:
      if (screen->vertical_workspaces) 
        {
          c = cols - 1;
          while (c >= 0)
            {
              r = rows - 1;
              while (r >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --r;
                }
              --c;
            }
        }
      else
        {
          r = rows - 1;
          while (r >= 0)
            {
              c = cols - 1;
              while (c >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --c;
                }
              --r;
            }
        }
      break;
    }  

  if (i != grid_area)
    meta_bug ("did not fill in the whole workspace grid in %s (%d filled)\n",
              G_STRFUNC, i);
  
  current_row = 0;
  current_col = 0;
  r = 0;
  while (r < rows)
    {
      c = 0;
      while (c < cols)
        {
          if (grid[r*cols+c] == current_space)
            {
              current_row = r;
              current_col = c;
            }
          else if (grid[r*cols+c] >= num_workspaces)
            {
              /* flag nonexistent spaces with -1 */
              grid[r*cols+c] = -1;
            }
          ++c;
        }
      ++r;
    }

  layout->rows = rows;
  layout->cols = cols;
  layout->grid = grid;
  layout->grid_area = grid_area;
  layout->current_row = current_row;
  layout->current_col = current_col;

#ifdef WITH_VERBOSE_MODE
  if (meta_is_verbose ())
    {
      r = 0;
      while (r < layout->rows)
        {
          meta_verbose (" ");
          meta_push_no_msg_prefix ();
          c = 0;
          while (c < layout->cols)
            {
              if (r == layout->current_row &&
                  c == layout->current_col)
                meta_verbose ("*%2d ", layout->grid[r*layout->cols+c]);
              else
                meta_verbose ("%3d ", layout->grid[r*layout->cols+c]);
              ++c;
            }
          meta_verbose ("\n");
          meta_pop_no_msg_prefix ();
          ++r;
        }
    }
#endif /* WITH_VERBOSE_MODE */
}

LOCAL_SYMBOL void
meta_screen_free_workspace_layout (MetaWorkspaceLayout *layout)
{
  g_free (layout->grid);
}

static void
meta_screen_resize_func (MetaScreen *screen,
                         MetaWindow *window,
                         void       *user_data)
{
  if (window->struts)
    {
      meta_window_update_struts (window);
    }
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);

  meta_window_recalc_features (window);
}

LOCAL_SYMBOL void
meta_screen_resize (MetaScreen *screen,
                    int         width,
                    int         height)
{
  GSList *windows, *tmp;
  MetaMonitorInfo *old_monitor_infos;

  screen->rect.width = width;
  screen->rect.height = height;

  /* Save the old monitor infos, so they stay valid during the update */
  old_monitor_infos = screen->monitor_infos;

  reload_monitor_infos (screen);
  set_desktop_geometry_hint (screen);

  if (screen->display->compositor)
    meta_compositor_sync_screen_size (screen->display->compositor,
				      screen, width, height);

  /* Queue a resize on all the windows */
  meta_screen_foreach_window (screen, meta_screen_resize_func, 0);

  /* Fix up monitor for all windows on this screen */
  windows = meta_display_list_windows (screen->display,
                                       META_LIST_INCLUDE_OVERRIDE_REDIRECT);
  for (tmp = windows; tmp != NULL; tmp = tmp->next)
    {
      MetaWindow *window = tmp->data;

      if (window->screen == screen)
        meta_window_update_for_monitors_changed (window);
    }

  g_free (old_monitor_infos);
  g_slist_free (windows);

  g_signal_emit (screen, screen_signals[MONITORS_CHANGED], 0);
}

LOCAL_SYMBOL void
meta_screen_update_showing_desktop_hint (MetaScreen *screen)
{
  unsigned long data[1];

  data[0] = screen->active_workspace->showing_desktop ? 1 : 0;
      
  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SHOWING_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_error_trap_pop (screen->display);
}

static void
queue_windows_showing (MetaScreen *screen)
{
  GSList *windows;
  GSList *tmp;

  /* Must operate on all windows on display instead of just on the
   * active_workspace's window list, because the active_workspace's
   * window list may not contain the on_all_workspace windows.
   */
  windows = meta_display_list_windows (screen->display, META_LIST_DEFAULT);

  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      if (w->screen == screen)
        meta_window_queue (w, META_QUEUE_CALC_SHOWING);
      
      tmp = tmp->next;
    }

  g_slist_free (windows);
}

LOCAL_SYMBOL void
meta_screen_minimize_all_on_active_workspace_except (MetaScreen *screen,
                                                     MetaWindow *keep)
{
  GList *windows;
  GList *tmp;

  windows = screen->active_workspace->windows;
  
  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;
      
      if (w->screen == screen  &&
          w->has_minimize_func &&
	  w != keep)
	meta_window_minimize (w);
      
      tmp = tmp->next;
    }
}

void
meta_screen_toggle_desktop (MetaScreen *screen,
							guint32    timestamp)
{
  if (screen->active_workspace->showing_desktop)
    {
      /*
       * dirty hack because the actual window to be focused after unshowing the desktop
       * lost its focus when showing desktop
       * and therefore is second in the MRU list
       */
      MetaWindow *not_this_one;
      not_this_one = meta_stack_get_default_focus_window(screen->stack, 
                                                         screen->active_workspace,
                                                         NULL);
      meta_screen_unshow_desktop (screen);
      meta_workspace_focus_default_window (screen->active_workspace,
                                           not_this_one,
                                           timestamp);
      // If there's only one window, make sure it gets the focus
      meta_workspace_focus_default_window (screen->active_workspace,
                                           NULL,
                                           timestamp);
    }
  else
    meta_screen_show_desktop (screen, timestamp);
}

void
meta_screen_show_desktop (MetaScreen *screen, 
                          guint32     timestamp)
{
  GList *windows;

  if (screen->active_workspace->showing_desktop)
    return;
  
  screen->active_workspace->showing_desktop = TRUE;
  
  queue_windows_showing (screen);

  /* Focus the most recently used META_WINDOW_DESKTOP window, if there is one;
   * see bug 159257.
   */
  windows = screen->active_workspace->mru_list;
  while (windows != NULL)
    {
      MetaWindow *w = windows->data;
      
      if (w->screen == screen  && 
          w->type == META_WINDOW_DESKTOP)
        {
          meta_window_focus (w, timestamp);
          break;
        }
      
      windows = windows->next;
    }

  
  meta_screen_update_showing_desktop_hint (screen);
}

void
meta_screen_unshow_desktop (MetaScreen *screen)
{
  if (!screen->active_workspace->showing_desktop)
    return;

  screen->active_workspace->showing_desktop = FALSE;

  queue_windows_showing (screen);

  meta_screen_update_showing_desktop_hint (screen);
}


#ifdef HAVE_STARTUP_NOTIFICATION
static gboolean startup_sequence_timeout (void *data);

static void
update_startup_feedback (MetaScreen *screen)
{
  if (screen->startup_sequences != NULL)
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Setting busy cursor\n");
      meta_screen_set_cursor (screen, META_CURSOR_BUSY);
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Setting default cursor\n");
      meta_screen_set_cursor (screen, META_CURSOR_DEFAULT);
    }
}

static void
add_sequence (MetaScreen        *screen,
              SnStartupSequence *sequence)
{
  meta_topic (META_DEBUG_STARTUP,
              "Adding sequence %s\n",
              sn_startup_sequence_get_id (sequence));
  sn_startup_sequence_ref (sequence);
  screen->startup_sequences = g_slist_prepend (screen->startup_sequences,
                                               sequence);

  /* our timeout just polls every second, instead of bothering
   * to compute exactly when we may next time out
   */
  if (screen->startup_sequence_timeout == 0)
    screen->startup_sequence_timeout = g_timeout_add_seconds (1,
                                                              startup_sequence_timeout,
                                                              screen);

  update_startup_feedback (screen);
}

static void
remove_sequence (MetaScreen        *screen,
                 SnStartupSequence *sequence)
{
  meta_topic (META_DEBUG_STARTUP,
              "Removing sequence %s\n",
              sn_startup_sequence_get_id (sequence));
  
  screen->startup_sequences = g_slist_remove (screen->startup_sequences,
                                              sequence);

  if (screen->startup_sequences == NULL &&
      screen->startup_sequence_timeout != 0)
    {
      g_source_remove (screen->startup_sequence_timeout);
      screen->startup_sequence_timeout = 0;
    }

  update_startup_feedback (screen);

  sn_startup_sequence_unref (sequence);
}

typedef struct
{
  GSList *list;
  GTimeVal now;
} CollectTimedOutData;

/* This should be fairly long, as it should never be required unless
 * apps or .desktop files are buggy, and it's confusing if
 * OpenOffice or whatever seems to stop launching - people
 * might decide they need to launch it again.
 */
#define STARTUP_TIMEOUT 15000

static void
collect_timed_out_foreach (void *element,
                           void *data)
{
  CollectTimedOutData *ctod = data;
  SnStartupSequence *sequence = element;
  long tv_sec, tv_usec;
  double elapsed;
  
  sn_startup_sequence_get_last_active_time (sequence, &tv_sec, &tv_usec);

  elapsed =
    ((((double)ctod->now.tv_sec - tv_sec) * G_USEC_PER_SEC +
      (ctod->now.tv_usec - tv_usec))) / 1000.0;

  meta_topic (META_DEBUG_STARTUP,
              "Sequence used %g seconds vs. %g max: %s\n",
              elapsed, (double) STARTUP_TIMEOUT,
              sn_startup_sequence_get_id (sequence));
  
  if (elapsed > STARTUP_TIMEOUT)
    ctod->list = g_slist_prepend (ctod->list, sequence);
}

static gboolean
startup_sequence_timeout (void *data)
{
  MetaScreen *screen = data;
  CollectTimedOutData ctod;
  GSList *tmp;
  
  ctod.list = NULL;
  g_get_current_time (&ctod.now);
  g_slist_foreach (screen->startup_sequences,
                   collect_timed_out_foreach,
                   &ctod);

  tmp = ctod.list;
  while (tmp != NULL)
    {
      SnStartupSequence *sequence = tmp->data;

      meta_topic (META_DEBUG_STARTUP,
                  "Timed out sequence %s\n",
                  sn_startup_sequence_get_id (sequence));
      
      sn_startup_sequence_complete (sequence);
      
      tmp = tmp->next;
    }

  g_slist_free (ctod.list);
  
  if (screen->startup_sequences != NULL)
    {
      return TRUE;
    }
  else
    {
      /* remove */
      screen->startup_sequence_timeout = 0;
      return FALSE;
    }
}

static void
meta_screen_sn_event (SnMonitorEvent *event,
                      void           *user_data)
{
  MetaScreen *screen;
  SnStartupSequence *sequence;
  
  screen = user_data;

  sequence = sn_monitor_event_get_startup_sequence (event);

  sn_startup_sequence_ref (sequence);

  switch (sn_monitor_event_get_type (event))
    {
    case SN_MONITOR_EVENT_INITIATED:
      {
        const char *wmclass;

        wmclass = sn_startup_sequence_get_wmclass (sequence);
        
        meta_topic (META_DEBUG_STARTUP,
                    "Received startup initiated for %s wmclass %s\n",
                    sn_startup_sequence_get_id (sequence),
                    wmclass ? wmclass : "(unset)");
        add_sequence (screen, sequence);
      }
      break;

    case SN_MONITOR_EVENT_COMPLETED:
      {
        meta_topic (META_DEBUG_STARTUP,
                    "Received startup completed for %s\n",
                    sn_startup_sequence_get_id (sequence));
        remove_sequence (screen,
                         sn_monitor_event_get_startup_sequence (event));
      }
      break;

    case SN_MONITOR_EVENT_CHANGED:
      meta_topic (META_DEBUG_STARTUP,
                  "Received startup changed for %s\n",
                  sn_startup_sequence_get_id (sequence));
      break;

    case SN_MONITOR_EVENT_CANCELED:
      meta_topic (META_DEBUG_STARTUP,
                  "Received startup canceled for %s\n",
                  sn_startup_sequence_get_id (sequence));
      break;
    }

  g_signal_emit (G_OBJECT (screen), screen_signals[STARTUP_SEQUENCE_CHANGED], 0, sequence);

  sn_startup_sequence_unref (sequence);
}

/**
 * meta_screen_get_startup_sequences: (skip)
 * @screen:
 *
 * Return value: (transfer none): Currently active #SnStartupSequence items
 */
GSList *
meta_screen_get_startup_sequences (MetaScreen *screen)
{
  return screen->startup_sequences;
}
#endif

/* Sets the initial_timestamp and initial_workspace properties
 * of a window according to information given us by the
 * startup-notification library.
 *
 * Returns TRUE if startup properties have been applied, and
 * FALSE if they have not (for example, if they had already
 * been applied.)
 */
LOCAL_SYMBOL gboolean
meta_screen_apply_startup_properties (MetaScreen *screen,
                                      MetaWindow *window)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  const char *startup_id;
  GSList *tmp;
  SnStartupSequence *sequence;
  
  /* Does the window have a startup ID stored? */
  startup_id = meta_window_get_startup_id (window);

  meta_topic (META_DEBUG_STARTUP,
              "Applying startup props to %s id \"%s\"\n",
              window->desc,
              startup_id ? startup_id : "(none)");
  
  sequence = NULL;
  if (startup_id == NULL)
    {
      /* No startup ID stored for the window. Let's ask the
       * startup-notification library whether there's anything
       * stored for the resource name or resource class hints.
       */
      tmp = screen->startup_sequences;
      while (tmp != NULL)
        {
          const char *wmclass;

          wmclass = sn_startup_sequence_get_wmclass (tmp->data);

          if (wmclass != NULL &&
              ((window->res_class &&
                strcmp (wmclass, window->res_class) == 0) ||
               (window->res_name &&
                strcmp (wmclass, window->res_name) == 0)))
            {
              sequence = tmp->data;

              g_assert (window->startup_id == NULL);
              window->startup_id = g_strdup (sn_startup_sequence_get_id (sequence));
              startup_id = window->startup_id;

              meta_topic (META_DEBUG_STARTUP,
                          "Ending legacy sequence %s due to window %s\n",
                          sn_startup_sequence_get_id (sequence),
                          window->desc);
              
              sn_startup_sequence_complete (sequence);
              break;
            }
          
          tmp = tmp->next;
        }
    }

  /* Still no startup ID? Bail. */
  if (startup_id == NULL)
    return FALSE;
  
  /* We might get this far and not know the sequence ID (if the window
   * already had a startup ID stored), so let's look for one if we don't
   * already know it.
   */
  if (sequence == NULL)
    {
      tmp = screen->startup_sequences;
      while (tmp != NULL)
        {
          const char *id;
          
          id = sn_startup_sequence_get_id (tmp->data);
          
          if (strcmp (id, startup_id) == 0)
            {
              sequence = tmp->data;
              break;
            }
          
          tmp = tmp->next;
        }
    }

  if (sequence != NULL)
    {
      gboolean changed_something = FALSE;
          
      meta_topic (META_DEBUG_STARTUP,
                  "Found startup sequence for window %s ID \"%s\"\n",
                  window->desc, startup_id);

      if (!window->initial_workspace_set)
        {
          int space = sn_startup_sequence_get_workspace (sequence);
          if (space >= 0)
            {
              meta_topic (META_DEBUG_STARTUP,
                          "Setting initial window workspace to %d based on startup info\n",
                          space);
              
              window->initial_workspace_set = TRUE;
              window->initial_workspace = space;
              changed_something = TRUE;
            }
        }

      if (!window->initial_timestamp_set)
        {
          guint32 timestamp = sn_startup_sequence_get_timestamp (sequence);
          meta_topic (META_DEBUG_STARTUP,
                      "Setting initial window timestamp to %u based on startup info\n",
                      timestamp);
              
          window->initial_timestamp_set = TRUE;
          window->initial_timestamp = timestamp;
          changed_something = TRUE;
        }

      return changed_something;
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Did not find startup sequence for window %s ID \"%s\"\n",
                  window->desc, startup_id);
    }
  
#endif /* HAVE_STARTUP_NOTIFICATION */

  return FALSE;
}

int
meta_screen_get_screen_number (MetaScreen *screen)
{
  return screen->number;
}

/**
 * meta_screen_get_display:
 * Retrieve the display associated with screen.
 * @screen: A #MetaScreen
 * 
 * Returns: (transfer none): Display 
 */
MetaDisplay *
meta_screen_get_display (MetaScreen *screen)
{
  return screen->display;
}

/**
 * meta_screen_get_xroot: (skip)
 *
 */
Window
meta_screen_get_xroot (MetaScreen *screen)
{
  return screen->xroot;
}

/**
 * meta_screen_get_size:
 * @screen: A #MetaScreen
 * @width: (out): The width of the screen
 * @height: (out): The height of the screen
 *
 * Retrieve the size of the screen.
 */
void 
meta_screen_get_size (MetaScreen *screen,
                      int        *width,
                      int        *height)
{
  *width = screen->rect.width;
  *height = screen->rect.height;
}

/**
 * meta_screen_get_compositor_data: (skip)
 *
 */
gpointer
meta_screen_get_compositor_data (MetaScreen *screen)
{
  return screen->compositor_data;
}

void
meta_screen_set_compositor_data (MetaScreen *screen,
                                 gpointer    compositor)
{
  screen->compositor_data = compositor;
}

void
meta_screen_set_cm_selection (MetaScreen *screen)
{
  char selection[32];
  Atom a;

  screen->wm_cm_timestamp = meta_display_get_current_time_roundtrip (
                                                               screen->display);

  g_snprintf (selection, sizeof(selection), "_NET_WM_CM_S%d", screen->number);
  meta_verbose ("Setting selection: %s\n", selection);
  a = XInternAtom (screen->display->xdisplay, selection, FALSE);
  XSetSelectionOwner (screen->display->xdisplay, a, 
                      screen->wm_cm_selection_window, screen->wm_cm_timestamp);
}

void
meta_screen_unset_cm_selection (MetaScreen *screen)
{
  char selection[32];
  Atom a;

  g_snprintf (selection, sizeof(selection), "_NET_WM_CM_S%d", screen->number);
  a = XInternAtom (screen->display->xdisplay, selection, FALSE);
  XSetSelectionOwner (screen->display->xdisplay, a,
                      None, screen->wm_cm_timestamp);
}

/**
 * meta_screen_get_workspaces: (skip)
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none) (element-type Meta.Workspace): The workspaces for @screen
 */
GList *
meta_screen_get_workspaces (MetaScreen *screen)
{
  return screen->workspaces;
}

int
meta_screen_get_active_workspace_index (MetaScreen *screen)
{
  MetaWorkspace *active = screen->active_workspace;

  if (!active)
    return -1;

  return meta_workspace_index (active);
}

/**
 * meta_screen_get_active_workspace:
 *
 * Returns: (transfer none): The current workspace
 */
MetaWorkspace *
meta_screen_get_active_workspace (MetaScreen *screen)
{
  return screen->active_workspace;
}

LOCAL_SYMBOL void
meta_screen_restacked (MetaScreen *screen)
{
  g_signal_emit (screen, screen_signals[RESTACKED], 0);
}

LOCAL_SYMBOL void
meta_screen_workspace_switched (MetaScreen         *screen,
                                int                 from,
                                int                 to,
                                MetaMotionDirection direction)
{
  meta_screen_update_snapped_windows (screen);
  g_signal_emit (screen, screen_signals[WORKSPACE_SWITCHED], 0,
                 from, to, direction);
}

LOCAL_SYMBOL void
meta_screen_set_active_workspace_hint (MetaScreen *screen)
{
  unsigned long data[1];

  /* this is because we destroy the spaces in order,
   * so we always end up setting a current desktop of
   * 0 when closing a screen, so lose the current desktop
   * on restart. By doing this we keep the current
   * desktop on restart.
   */
  if (screen->closing > 0)
    return;
  
  data[0] = meta_workspace_index (screen->active_workspace);

  meta_verbose ("Setting _NET_CURRENT_DESKTOP to %lu\n", data[0]);
  
  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_CURRENT_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_error_trap_pop (screen->display);
}

LOCAL_SYMBOL void
meta_screen_update_snapped_windows (MetaScreen *screen)
{
    GList* tmp = screen->workspaces;
    while (tmp)
        {
          MetaWorkspace* work = (MetaWorkspace*) tmp->data;
          meta_workspace_update_snapped_windows (work);
          tmp = tmp->next;
        }
}
