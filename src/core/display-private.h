/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X display handler */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
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

#ifndef META_DISPLAY_PRIVATE_H
#define META_DISPLAY_PRIVATE_H

#include "meta/display.h"

#include <glib.h>
#include <X11/extensions/sync.h>
#include <X11/Xlib.h>

#ifdef HAVE_STARTUP_NOTIFICATION
#include <libsn/sn.h>
#endif

#include "clutter/clutter.h"
#include "core/keybindings-private.h"
#include "core/meta-gesture-tracker-private.h"
#include "core/stack-tracker.h"
#include "core/startup-notification-private.h"
#include "meta/barrier.h"
#include "meta/boxes.h"
#include "meta/common.h"
#include "meta/meta-selection.h"
#include "meta/prefs.h"

typedef struct _MetaBell       MetaBell;
typedef struct _MetaStack      MetaStack;
typedef struct _MetaUISlave    MetaUISlave;

typedef struct MetaEdgeResistanceData MetaEdgeResistanceData;

typedef enum
{
  META_LIST_DEFAULT                   = 0,      /* normal windows */
  META_LIST_INCLUDE_OVERRIDE_REDIRECT = 1 << 0, /* normal and O-R */
  META_LIST_SORTED                    = 1 << 1, /* sort list by mru */
} MetaListWindowsFlags;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

/* This is basically a bogus number, just has to be large enough
 * to handle the expected case of the alt+tab operation, where
 * we want to ignore serials from UnmapNotify on the tab popup,
 * and the LeaveNotify/EnterNotify from the pointer ungrab. It
 * also has to be big enough to hold ignored serials from the point
 * where we reshape the stage to the point where we get events back.
 */
#define N_IGNORED_CROSSING_SERIALS  10

typedef enum
{
  META_TILE_NONE,
  META_TILE_LEFT,
  META_TILE_RIGHT,
  META_TILE_MAXIMIZED
} MetaTileMode;

typedef enum
{
  /* Normal interaction where you're interacting with windows.
   * Events go to windows normally. */
  META_EVENT_ROUTE_NORMAL,

  /* In a window operation like moving or resizing. All events
   * goes to MetaWindow, but not to the actual client window. */
  META_EVENT_ROUTE_WINDOW_OP,

  /* In a compositor grab operation. All events go to the
   * compositor plugin. */
  META_EVENT_ROUTE_COMPOSITOR_GRAB,

  /* A Wayland application has a popup open. All events go to
   * the Wayland application. */
  META_EVENT_ROUTE_WAYLAND_POPUP,

  /* The user is clicking on a window button. */
  META_EVENT_ROUTE_FRAME_BUTTON,
} MetaEventRoute;

typedef void (* MetaDisplayWindowFunc) (MetaWindow *window,
                                        gpointer    user_data);

struct _MetaDisplay
{
  GObject parent_instance;

  MetaX11Display *x11_display;

  int clutter_event_filter;

  /* Our best guess as to the "currently" focused window (that is, the
   * window that we expect will be focused at the point when the X
   * server processes our next request), and the serial of the request
   * or event that caused this.
   */
  MetaWindow *focus_window;

  /* last timestamp passed to XSetInputFocus */
  guint32 last_focus_time;

  /* last user interaction time in any app */
  guint32 last_user_time;

  /* whether we're using mousenav (only relevant for sloppy&mouse focus modes;
   * !mouse_mode means "keynav mode")
   */
  guint mouse_mode : 1;

  /* Helper var used when focus_new_windows setting is 'strict'; only
   * relevant in 'strict' mode and if the focus window is a terminal.
   * In that case, we don't allow new windows to take focus away from
   * a terminal, but if the user explicitly did something that should
   * allow a different window to gain focus (e.g. global keybinding or
   * clicking on a dock), then we will allow the transfer.
   */
  guint allow_terminal_deactivation : 1;

  /*< private-ish >*/
  GHashTable *stamps;
  GHashTable *wayland_windows;

  /* serials of leave/unmap events that may
   * correspond to an enter event we should
   * ignore
   */
  unsigned long ignored_crossing_serials[N_IGNORED_CROSSING_SERIALS];

  guint32 current_time;

  /* We maintain a sequence counter, incremented for each #MetaWindow
   * created.  This is exposed by meta_window_get_stable_sequence()
   * but is otherwise not used inside mutter.
   *
   * It can be useful to plugins which want to sort windows in a
   * stable fashion.
   */
  guint32 window_sequence_counter;

  /* Pings which we're waiting for a reply from */
  GSList     *pending_pings;

  /* Pending focus change */
  guint       focus_timeout_id;

  /* Pending autoraise */
  guint       autoraise_timeout_id;
  MetaWindow* autoraise_window;

  /* Event routing */
  MetaEventRoute event_route;

  /* current window operation */
  MetaGrabOp  grab_op;
  MetaWindow *grab_window;
  int         grab_button;
  int         grab_anchor_root_x;
  int         grab_anchor_root_y;
  MetaRectangle grab_anchor_window_pos;
  MetaTileMode  grab_tile_mode;
  int           grab_tile_monitor_number;
  int         grab_latest_motion_x;
  int         grab_latest_motion_y;
  guint       grab_have_pointer : 1;
  guint       grab_have_keyboard : 1;
  guint       grab_frame_action : 1;
  MetaRectangle grab_initial_window_pos;
  int         grab_initial_x, grab_initial_y;  /* These are only relevant for */
  gboolean    grab_threshold_movement_reached; /* raise_on_click == FALSE.    */
  int64_t     grab_last_moveresize_time;
  MetaEdgeResistanceData *grab_edge_resistance_data;
  unsigned int grab_last_user_action_was_snap;

  int	      grab_resize_timeout_id;

  MetaKeyBindingManager key_binding_manager;

  /* Monitor cache */
  unsigned int monitor_cache_invalidated : 1;

  /* Opening the display */
  unsigned int display_opening : 1;

  /* Closing down the display */
  int closing;

  /* Managed by compositor.c */
  MetaCompositor *compositor;

  MetaGestureTracker *gesture_tracker;
  ClutterEventSequence *pointer_emulating_sequence;

  ClutterActor *current_pad_osd;

  MetaStartupNotification *startup_notification;

  MetaCursor current_cursor;

  MetaStack *stack;
  MetaStackTracker *stack_tracker;

  guint tile_preview_timeout_id;
  guint preview_tile_mode : 2;

  GSList *startup_sequences;

  guint work_area_later;
  guint check_fullscreen_later;

  MetaBell *bell;
  MetaWorkspaceManager *workspace_manager;

  MetaSoundPlayer *sound_player;

  MetaSelectionSource *selection_source;
  GBytes *saved_clipboard;
  gchar *saved_clipboard_mimetype;
  MetaSelection *selection;
};

struct _MetaDisplayClass
{
  GObjectClass parent_class;
};

#define XSERVER_TIME_IS_BEFORE_ASSUMING_REAL_TIMESTAMPS(time1, time2) \
  ( (( (time1) < (time2) ) && ( (time2) - (time1) < ((guint32)-1)/2 )) ||     \
    (( (time1) > (time2) ) && ( (time1) - (time2) > ((guint32)-1)/2 ))        \
  )
/**
 * XSERVER_TIME_IS_BEFORE:
 *
 * See the docs for meta_display_xserver_time_is_before().
 */
#define XSERVER_TIME_IS_BEFORE(time1, time2)                          \
  ( (time1) == 0 ||                                                     \
    (XSERVER_TIME_IS_BEFORE_ASSUMING_REAL_TIMESTAMPS(time1, time2) && \
     (time2) != 0)                                                      \
  )

gboolean      meta_display_open                (void);

void meta_display_manage_all_xwindows (MetaDisplay *display);
void meta_display_unmanage_windows   (MetaDisplay *display,
                                      guint32      timestamp);

/* Utility function to compare the stacking of two windows */
int           meta_display_stack_cmp           (const void *a,
                                                const void *b);

/* Each MetaWindow is uniquely identified by a 64-bit "stamp"; unlike a
 * a MetaWindow *, a stamp will never be recycled
 */
MetaWindow* meta_display_lookup_stamp     (MetaDisplay *display,
                                           guint64      stamp);
void        meta_display_register_stamp   (MetaDisplay *display,
                                           guint64     *stampp,
                                           MetaWindow  *window);
void        meta_display_unregister_stamp (MetaDisplay *display,
                                           guint64      stamp);

/* A "stack id" is a XID or a stamp */
#define META_STACK_ID_IS_X11(id) ((id) < G_GUINT64_CONSTANT(0x100000000))

META_EXPORT_TEST
MetaWindow* meta_display_lookup_stack_id   (MetaDisplay *display,
                                            guint64      stack_id);

/* for debug logging only; returns a human-description of the stack
 * ID - a small number of buffers are recycled, so the result must
 * be used immediately or copied */
const char *meta_display_describe_stack_id (MetaDisplay *display,
                                            guint64      stack_id);

void        meta_display_register_wayland_window   (MetaDisplay *display,
                                                    MetaWindow  *window);
void        meta_display_unregister_wayland_window (MetaDisplay *display,
                                                    MetaWindow  *window);

void        meta_display_notify_window_created (MetaDisplay  *display,
                                                MetaWindow   *window);

META_EXPORT_TEST
GSList*     meta_display_list_windows        (MetaDisplay          *display,
                                              MetaListWindowsFlags  flags);

MetaDisplay* meta_display_for_x_display  (Display     *xdisplay);

META_EXPORT_TEST
MetaDisplay* meta_get_display            (void);

void meta_display_reload_cursor (MetaDisplay *display);
void meta_display_update_cursor (MetaDisplay *display);

void    meta_display_check_threshold_reached (MetaDisplay *display,
                                              int          x,
                                              int          y);
void     meta_display_grab_window_buttons    (MetaDisplay *display,
                                              Window       xwindow);
void     meta_display_ungrab_window_buttons  (MetaDisplay *display,
                                              Window       xwindow);

void meta_display_grab_focus_window_button   (MetaDisplay *display,
                                              MetaWindow  *window);
void meta_display_ungrab_focus_window_button (MetaDisplay *display,
                                              MetaWindow  *window);

/* Next function is defined in edge-resistance.c */
void meta_display_cleanup_edges              (MetaDisplay *display);

/* utility goo */
const char* meta_event_mode_to_string   (int m);
const char* meta_event_detail_to_string (int d);

void meta_display_queue_retheme_all_windows (MetaDisplay *display);

void meta_display_ping_window      (MetaWindow  *window,
                                    guint32      serial);
void meta_display_pong_for_serial  (MetaDisplay *display,
                                    guint32      serial);

MetaGravity meta_resize_gravity_from_grab_op (MetaGrabOp op);

gboolean meta_grab_op_is_moving   (MetaGrabOp op);
gboolean meta_grab_op_is_resizing (MetaGrabOp op);
gboolean meta_grab_op_is_mouse    (MetaGrabOp op);
gboolean meta_grab_op_is_keyboard (MetaGrabOp op);

void meta_display_queue_autoraise_callback  (MetaDisplay *display,
                                             MetaWindow  *window);
void meta_display_remove_autoraise_callback (MetaDisplay *display);

void meta_display_overlay_key_activate (MetaDisplay *display);
void meta_display_accelerator_activate (MetaDisplay     *display,
                                        guint            action,
                                        ClutterKeyEvent *event);
gboolean meta_display_modifiers_accelerator_activate (MetaDisplay *display);

void meta_display_sync_wayland_input_focus (MetaDisplay *display);
void meta_display_update_focus_window (MetaDisplay *display,
                                       MetaWindow  *window);

void meta_display_sanity_check_timestamps (MetaDisplay *display,
                                           guint32      timestamp);
gboolean meta_display_timestamp_too_old (MetaDisplay *display,
                                         guint32     *timestamp);

void meta_display_remove_pending_pings_for_window (MetaDisplay *display,
                                                   MetaWindow  *window);

MetaGestureTracker * meta_display_get_gesture_tracker (MetaDisplay *display);

gboolean meta_display_show_restart_message (MetaDisplay *display,
                                            const char  *message);
gboolean meta_display_request_restart      (MetaDisplay *display);

gboolean meta_display_show_resize_popup (MetaDisplay *display,
                                         gboolean show,
                                         MetaRectangle *rect,
                                         int display_w,
                                         int display_h);

void meta_set_is_restart (gboolean whether);

void meta_display_cancel_touch (MetaDisplay *display);

gboolean meta_display_windows_are_interactable (MetaDisplay *display);

void meta_display_show_tablet_mapping_notification (MetaDisplay        *display,
                                                    ClutterInputDevice *pad,
                                                    const gchar        *pretty_name);

void meta_display_notify_pad_group_switch (MetaDisplay        *display,
                                           ClutterInputDevice *pad,
                                           const gchar        *pretty_name,
                                           guint               n_group,
                                           guint               n_mode,
                                           guint               n_modes);

void meta_display_foreach_window (MetaDisplay           *display,
                                  MetaListWindowsFlags   flags,
                                  MetaDisplayWindowFunc  func,
                                  gpointer               data);

void meta_display_restacked (MetaDisplay *display);


void meta_display_update_tile_preview (MetaDisplay *display,
                                       gboolean     delay);
void meta_display_hide_tile_preview   (MetaDisplay *display);

gboolean meta_display_apply_startup_properties (MetaDisplay *display,
                                                MetaWindow  *window);

void meta_display_queue_workarea_recalc  (MetaDisplay *display);
void meta_display_queue_check_fullscreen (MetaDisplay *display);

MetaWindow *meta_display_get_pointer_window (MetaDisplay *display,
                                             MetaWindow  *not_this_one);

MetaWindow *meta_display_get_window_from_id (MetaDisplay *display,
                                             uint64_t     window_id);
uint64_t    meta_display_generate_window_id (MetaDisplay *display);

void meta_display_init_x11 (MetaDisplay         *display,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data);
gboolean meta_display_init_x11_finish (MetaDisplay   *display,
                                       GAsyncResult  *result,
                                       GError       **error);

void     meta_display_shutdown_x11 (MetaDisplay  *display);

#endif
