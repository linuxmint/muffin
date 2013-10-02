/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file window-private.h  Windows which Muffin manages
 *
 * Managing X windows.
 * This file contains methods on this class which are available to
 * routines in core but not outside it.  (See window.h for the routines
 * which the rest of the world is allowed to use.)
 */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
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

#ifndef META_WINDOW_PRIVATE_H
#define META_WINDOW_PRIVATE_H

#include <config.h>
#include <meta/compositor.h>
#include <meta/window.h>
#include "screen-private.h"
#include <meta/util.h>
#include "stack.h"
#include "iconcache.h"
#include <X11/Xutil.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "tile-hud.h"

typedef struct _MetaWindowQueue MetaWindowQueue;

typedef enum {
  META_CLIENT_TYPE_UNKNOWN = 0,
  META_CLIENT_TYPE_APPLICATION = 1,
  META_CLIENT_TYPE_PAGER = 2,
  META_CLIENT_TYPE_MAX_RECOGNIZED = 2
} MetaClientType;

typedef enum {
  META_QUEUE_CALC_SHOWING = 1 << 0,
  META_QUEUE_MOVE_RESIZE  = 1 << 1,
  META_QUEUE_UPDATE_ICON  = 1 << 2,
} MetaQueueType;

/* edge zones for tiling/snapping identification

  ___________________________
  | 4          0          5 |
  |                         |
  |                         |
  |                         |
  |                         |
  |  2                   3  |
  |                         |
  |                         |
  |                         |
  |                         |
  | 7          1          6 |
  |_________________________|

*/

enum {
    ZONE_0 = 0,
    ZONE_1,
    ZONE_2,
    ZONE_3,
    ZONE_4,
    ZONE_5,
    ZONE_6,
    ZONE_7,
    ZONE_NONE
};

#define NUMBER_OF_QUEUES 3

#define HUD_WIDTH 24

struct _MetaWindow
{
  GObject parent_instance;
  
  MetaDisplay *display;
  MetaScreen *screen;
  const MetaMonitorInfo *monitor;
  MetaWorkspace *workspace;
  Window xwindow;
  /* may be NULL! not all windows get decorated */
  MetaFrame *frame;
  int depth;
  Visual *xvisual;
  Colormap colormap;
  char *desc; /* used in debug spew */
  char *title;

  char *icon_name;
  GdkPixbuf *icon;
  GdkPixbuf *mini_icon;
  MetaIconCache icon_cache;
  Pixmap wm_hints_pixmap;
  Pixmap wm_hints_mask;
  
  MetaWindowType type;
  Atom type_atom;
  
  /* NOTE these five are not in UTF-8, we just treat them as random
   * binary data
   */
  char *res_class;
  char *res_name;
  char *role;
  char *sm_client_id;
  char *wm_client_machine;

  char *startup_id;
  char *muffin_hints;
  char *gtk_theme_variant;
  char *gtk_application_id;
  char *gtk_unique_bus_name;
  char *gtk_application_object_path;
  char *gtk_window_object_path;
  char *gtk_app_menu_object_path;
  char *gtk_menubar_object_path;
  
  int net_wm_pid;
  
  Window xtransient_for;
  Window xgroup_leader;
  Window xclient_leader;

  /* Initial workspace property */
  int initial_workspace;  
  
  /* Initial timestamp property */
  guint32 initial_timestamp;  
  
  /* Whether this is an override redirect window or not */
  guint override_redirect : 1;

  /* Whether we're maximized */
  guint maximized_horizontally : 1;
  guint maximized_vertically : 1;

  guint32 snap_delay_timestamp;
  guint snap_queued : 1;
  guint zone_queued;
  MetaWindowTileType tile_type;
  MetaWindowTileType resizing_tile_type;
  guint custom_snap_size : 1;
  guint current_proximity_zone;
  guint mouse_on_edge : 1;
  guint maybe_retile_maximize : 1;

  /* Whether we have to maximize/minimize after placement */
  guint maximize_horizontally_after_placement : 1;
  guint maximize_vertically_after_placement : 1;
  guint minimize_after_placement : 1;
  guint tile_after_placement : 1;
  guint move_after_placement : 1;

  /* The current or requested tile mode. If maximized_vertically is true,
   * this is the current mode. If not, it is the mode which will be
   * requested after the window grab is released */
  guint tile_mode : 4;
  guint last_tile_mode : 4;
  guint resize_tile_mode : 4;

  /* The last "full" maximized/unmaximized state. We need to keep track of
   * that to toggle between normal/tiled or maximized/tiled states. */
  guint saved_maximize : 1;
  int tile_monitor_number;

  /* Whether we're shaded */
  guint shaded : 1;

  /* Whether we're fullscreen */
  guint fullscreen : 1;

  /* Whether the urgent flag of WM_HINTS is set */
  guint wm_hints_urgent : 1;

  /* Whether we have to fullscreen after placement */
  guint fullscreen_after_placement : 1;

  /* Area to cover when in fullscreen mode.  If _NET_WM_FULLSCREEN_MONITORS has
   * been overridden (via a client message), the window will cover the union of
   * these monitors.  If not, this is the single monitor which the window's
   * origin is on. */
  long fullscreen_monitors[4];
  
  /* Whether we're trying to constrain the window to be fully onscreen */
  guint require_fully_onscreen : 1;

  /* Whether we're trying to constrain the window to be on a single monitor */
  guint require_on_single_monitor : 1;

  /* Whether we're trying to constrain the window's titlebar to be onscreen */
  guint require_titlebar_visible : 1;

  /* Whether we're sticky in the multi-workspace sense
   * (vs. the not-scroll-with-viewport sense, we don't
   * have no stupid viewports)
   */
  guint on_all_workspaces : 1;

  /* This is true if the client requested sticky, and implies on_all_workspaces == TRUE,
   * however on_all_workspaces can be set TRUE for other internal reasons too, such as
   * being override_redirect or being on the non-primary monitor. */
  guint on_all_workspaces_requested : 1;

  /* Minimize is the state controlled by the minimize button */
  guint minimized : 1;
  guint tab_unminimized : 1;

  /* Whether the window is mapped; actual server-side state
   * see also unmaps_pending
   */
  guint mapped : 1;
  
  /* Whether window has been hidden from view by lowering it to the bottom
   * of window stack.
   */
  guint hidden : 1;

  /* Whether the compositor thinks the window is visible
   */
  guint visible_to_compositor : 1;

  /* When we next show or hide the window, what effect we should
   * tell the compositor to perform.
   */
  guint pending_compositor_effect : 4; /* MetaCompEffect */

  /* Iconic is the state in WM_STATE; happens for workspaces/shading
   * in addition to minimize
   */
  guint iconic : 1;
  /* initially_iconic is the WM_HINTS setting when we first manage
   * the window. It's taken to mean initially minimized.
   */
  guint initially_iconic : 1;

  /* whether an initial workspace was explicitly set */
  guint initial_workspace_set : 1;
  
  /* whether an initial timestamp was explicitly set */
  guint initial_timestamp_set : 1;
  
  /* whether net_wm_user_time has been set yet */
  guint net_wm_user_time_set : 1;
  
  /* These are the flags from WM_PROTOCOLS */
  guint take_focus : 1;
  guint delete_window : 1;
  guint net_wm_ping : 1;
  /* Globally active / No input */
  guint input : 1;
  
  /* MWM hints about features of window */
  guint mwm_decorated : 1;
  guint mwm_border_only : 1;
  guint mwm_has_close_func : 1;
  guint mwm_has_minimize_func : 1;
  guint mwm_has_maximize_func : 1;
  guint mwm_has_move_func : 1;
  guint mwm_has_resize_func : 1;
  
  /* Computed features of window */
  guint decorated : 1;
  guint border_only : 1;
  guint always_sticky : 1;
  guint has_close_func : 1;
  guint has_minimize_func : 1;
  guint has_maximize_func : 1;
  guint has_shade_func : 1;
  guint has_move_func : 1;
  guint has_resize_func : 1;
  guint has_fullscreen_func : 1;
  
  /* Weird "_NET_WM_STATE_MODAL" flag */
  guint wm_state_modal : 1;

  /* TRUE if the client forced these on */
  guint wm_state_skip_taskbar : 1;
  guint wm_state_skip_pager : 1;

  /* Computed whether to skip taskbar or not */
  guint skip_taskbar : 1;
  guint skip_pager : 1;

  /* TRUE if client set these */
  guint wm_state_above : 1;
  guint wm_state_below : 1;

  /* EWHH demands attention flag */
  guint wm_state_demands_attention : 1;
  
  /* this flag tracks receipt of focus_in focus_out */
  guint has_focus : 1;

  /* Have we placed this window? */
  guint placed : 1;

  /* Must we force_save_user_window_placement? */
  guint force_save_user_rect : 1;

  /* Is this not a transient of the focus window which is being denied focus? */
  guint denied_focus_and_not_transient : 1;

  /* Has this window not ever been shown yet? */
  guint showing_for_first_time : 1;

  /* Are we in meta_window_unmanage()? */
  guint unmanaging : 1;

  /* Are we in meta_window_new()? */
  guint constructing : 1;
  
  /* Are we in the various queues? (Bitfield: see META_WINDOW_IS_IN_QUEUE) */
  guint is_in_queues : NUMBER_OF_QUEUES;
 
  /* Used by keybindings.c */
  guint keys_grabbed : 1;     /* normal keybindings grabbed */
  guint grab_on_frame : 1;    /* grabs are on the frame */
  guint all_keys_grabbed : 1; /* AnyKey grabbed */
  
  /* Set if the reason for unmanaging the window is that
   * it was withdrawn
   */
  guint withdrawn : 1;

  /* TRUE if constrain_position should calc placement.
   * only relevant if !window->placed
   */
  guint calc_placement : 1;

  /* Transient parent is a root window */
  guint transient_parent_is_root_window : 1;

  /* Info on which props we got our attributes from */
  guint using_net_wm_name              : 1; /* vs. plain wm_name */
  guint using_net_wm_visible_name      : 1; /* tracked so we can clear it */
  guint using_net_wm_icon_name         : 1; /* vs. plain wm_icon_name */
  guint using_net_wm_visible_icon_name : 1; /* tracked so we can clear it */

  /* has a shape mask */
  guint has_shape : 1;

  /* icon props have changed */
  guint need_reread_icon : 1;
  
  /* if TRUE, window was maximized at start of current grab op */
  guint shaken_loose : 1;

  /* if TRUE we have a grab on the focus click buttons */
  guint have_focus_click_grab : 1;

  /* if TRUE, application is buggy and SYNC resizing is turned off */
  guint disable_sync : 1;

  /* if TRUE, window is attached to its parent */
  guint attached : 1;

  /* if non-NULL, the bounds of the window frame */
  cairo_region_t *frame_bounds;

  /* Note: can be NULL */
  GSList *struts;

#ifdef HAVE_XSYNC
  /* XSync update counter */
  XSyncCounter sync_request_counter;
  guint sync_request_serial;
  GTimeVal sync_request_time;
#endif
  
  /* Number of UnmapNotify that are caused by us, if
   * we get UnmapNotify with none pending then the client
   * is withdrawing the window.
   */
  int unmaps_pending;

  /* See docs for meta_window_get_stable_sequence() */
  guint32 stable_sequence;

  /* set to the most recent user-interaction event timestamp that we
     know about for this window */
  guint32 net_wm_user_time;

  /* window that gets updated net_wm_user_time values */
  Window user_time_window;
  
  /* The size we set the window to last (i.e. what we believe
   * to be its actual size on the server). The x, y are
   * the actual server-side x,y so are relative to the frame
   * (meaning that they just hold the frame width and height) 
   * or the root window (meaning they specify the location
   * of the top left of the inner window) as appropriate.
   */
  MetaRectangle rect;

  /* The geometry to restore when we unmaximize.  The position is in
   * root window coords, even if there's a frame, which contrasts with
   * window->rect above.  Note that this gives the position and size
   * of the client window (i.e. ignoring the frame).
   */
  MetaRectangle saved_rect;

  /* This is the geometry the window had after the last user-initiated
   * move/resize operations. We use this whenever we are moving the
   * implicitly (for example, if we move to avoid a panel, we can snap
   * back to this position if the panel moves again).  Note that this
   * gives the position and size of the client window (i.e. ignoring
   * the frame).
   *
   * Position valid if user_has_moved, size valid if user_has_resized
   *
   * Position always in root coords, unlike window->rect.
   */
  MetaRectangle user_rect;

  MetaRectangle snapped_rect;

  /* Requested geometry */
  int border_width;
  /* x/y/w/h here get filled with ConfigureRequest values */
  XSizeHints size_hints;

  /* Managed by stack.c */
  MetaStackLayer layer;
  int stack_position; /* see comment in stack.h */
  
  /* Current dialog open for this window */
  int dialog_pid;

  /* maintained by group.c */
  MetaGroup *group;

  GObject *compositor_private;

  /* Focused window that is (directly or indirectly) attached to this one */
  MetaWindow *attached_focus_window;

  /* The currently complementary tiled window, if any */
  MetaWindow *tile_match;

  /* Bypass compositor hints */
  guint bypass_compositor : 1;
  guint dont_bypass_compositor : 1;
};

struct _MetaWindowClass
{
  GObjectClass parent_class;

  void (*workspace_changed) (MetaWindow *window, int  old_workspace);
  void (*focus)             (MetaWindow *window);
  void (*raised)            (MetaWindow *window);
  void (*unmanaged)         (MetaWindow *window);
};

/* These differ from window->has_foo_func in that they consider
 * the dynamic window state such as "maximized", not just the
 * window's type
 */
#define META_WINDOW_MAXIMIZED(w)       ((w)->maximized_horizontally && \
                                        (w)->maximized_vertically)
#define META_WINDOW_MAXIMIZED_VERTICALLY(w)    ((w)->maximized_vertically)
#define META_WINDOW_MAXIMIZED_HORIZONTALLY(w)  ((w)->maximized_horizontally)
#define META_WINDOW_TILED(w)                   ((w)->tile_type == META_WINDOW_TILE_TYPE_TILED)
#define META_WINDOW_SNAPPED(w)                 ((w)->tile_type == META_WINDOW_TILE_TYPE_SNAPPED)
#define META_WINDOW_TILED_OR_SNAPPED(w)        (META_WINDOW_SNAPPED (w) || META_WINDOW_TILED (w))
#define META_WINDOW_TILED_LEFT(w)     (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_LEFT)
#define META_WINDOW_TILED_RIGHT(w)    (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_RIGHT)
#define META_WINDOW_TILED_SIDE_BY_SIDE(w)      (META_WINDOW_TILED_LEFT (w) || META_WINDOW_TILED_RIGHT (w))
#define META_WINDOW_TILED_ULC(w)       (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_ULC)
#define META_WINDOW_TILED_LLC(w)       (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_LLC)
#define META_WINDOW_TILED_URC(w)       (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_URC)
#define META_WINDOW_TILED_LRC(w)       (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_LRC)
#define META_WINDOW_TILED_CORNER(w)    (META_WINDOW_TILED_ULC (w) || \
                                        META_WINDOW_TILED_LLC (w) || \
                                        META_WINDOW_TILED_URC (w) || \
                                        META_WINDOW_TILED_LRC (w))

#define META_WINDOW_TILED_TOP(w)     (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_TOP)
#define META_WINDOW_TILED_BOTTOM(w)    (META_WINDOW_TILED_OR_SNAPPED (w) && (w)->tile_mode == META_TILE_BOTTOM)

#define META_WINDOW_TILED_TOP_BOTTOM(w)  (META_WINDOW_TILED_TOP (w) || \
                                          META_WINDOW_TILED_BOTTOM (w))

#define META_WINDOW_ALLOWS_MOVE(w)     ((w)->has_move_func && !(w)->fullscreen)
#define META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS(w) ((w)->has_resize_func && !META_WINDOW_MAXIMIZED (w) && !(w)->fullscreen && !(w)->shaded)
#define META_WINDOW_ALLOWS_RESIZE(w)   (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) &&                \
                                        (((w)->size_hints.min_width < (w)->size_hints.max_width) ||  \
                                         ((w)->size_hints.min_height < (w)->size_hints.max_height)))
#define META_WINDOW_ALLOWS_HORIZONTAL_RESIZE(w) (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) && (w)->size_hints.min_width < (w)->size_hints.max_width && \
                                                !META_WINDOW_TILED_OR_SNAPPED (w))
#define META_WINDOW_ALLOWS_VERTICAL_RESIZE(w)   (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) && (w)->size_hints.min_height < (w)->size_hints.max_height && \
                                                !META_WINDOW_TILED_OR_SNAPPED (w))
#define META_WINDOW_ALLOWS_TOP_RESIZE(w)        (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) && META_WINDOW_TILED_OR_SNAPPED (w) && \
                                                 ((w)->tile_mode == META_TILE_BOTTOM || \
                                                  (w)->tile_mode == META_TILE_LLC || (w)->tile_mode == META_TILE_LRC))
#define META_WINDOW_ALLOWS_BOTTOM_RESIZE(w)        (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) && META_WINDOW_TILED_OR_SNAPPED (w) && \
                                                    ((w)->tile_mode == META_TILE_TOP || \
                                                     (w)->tile_mode == META_TILE_ULC || (w)->tile_mode == META_TILE_URC))
#define META_WINDOW_ALLOWS_LEFT_RESIZE(w)        (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) && META_WINDOW_TILED_OR_SNAPPED (w) && \
                                                  ((w)->tile_mode == META_TILE_RIGHT || \
                                                   (w)->tile_mode == META_TILE_URC || (w)->tile_mode == META_TILE_LRC))
#define META_WINDOW_ALLOWS_RIGHT_RESIZE(w)        (META_WINDOW_ALLOWS_RESIZE_EXCEPT_HINTS (w) && META_WINDOW_TILED_OR_SNAPPED (w) && \
                                                   ((w)->tile_mode == META_TILE_LEFT || \
                                                    (w)->tile_mode == META_TILE_ULC || (w)->tile_mode == META_TILE_LLC))
#define GRAB_OP(w)                                 ((w)->display->grab_op)

#define META_WINDOW_

MetaWindow* meta_window_new                (MetaDisplay *display,
                                            Window       xwindow,
                                            gboolean     must_be_viewable);
MetaWindow* meta_window_new_with_attrs     (MetaDisplay       *display,
                                            Window             xwindow,
                                            gboolean           must_be_viewable,
                                            MetaCompEffect     effect,
                                            XWindowAttributes *attrs);
void        meta_window_unmanage           (MetaWindow  *window,
                                            guint32      timestamp);
void        meta_window_calc_showing       (MetaWindow  *window);
void        meta_window_queue              (MetaWindow  *window,
                                            guint queuebits);
void        meta_window_tile               (MetaWindow        *window,
                                            gboolean           force);
void        meta_window_maximize_internal  (MetaWindow        *window,
                                            MetaMaximizeFlags  directions,
                                            MetaRectangle     *saved_rect);
void        meta_window_unmaximize_with_gravity (MetaWindow        *window,
                                                 MetaMaximizeFlags  directions,
                                                 int                new_width,
                                                 int                new_height,
                                                 int                gravity);
void        meta_window_make_above         (MetaWindow  *window);
void        meta_window_unmake_above       (MetaWindow  *window);
void        meta_window_shade              (MetaWindow  *window,
                                            guint32      timestamp);
void        meta_window_unshade            (MetaWindow  *window,
                                            guint32      timestamp);
void        meta_window_change_workspace   (MetaWindow  *window,
                                            MetaWorkspace *workspace);
void        meta_window_stick              (MetaWindow  *window);
void        meta_window_unstick            (MetaWindow  *window);

void        meta_window_make_fullscreen_internal (MetaWindow    *window);
void        meta_window_make_fullscreen    (MetaWindow  *window);
void        meta_window_unmake_fullscreen  (MetaWindow  *window);
void        meta_window_update_fullscreen_monitors (MetaWindow    *window,
                                                    unsigned long  top,
                                                    unsigned long  bottom,
                                                    unsigned long  left,
                                                    unsigned long  right);

/* args to move are window pos, not frame pos */
void        meta_window_move               (MetaWindow  *window,
                                            gboolean     user_op,
                                            int          root_x_nw,
                                            int          root_y_nw);
void        meta_window_resize             (MetaWindow  *window,
                                            gboolean     user_op,
                                            int          w,
                                            int          h);
void        meta_window_move_resize        (MetaWindow  *window,
                                            gboolean     user_op,
                                            int          root_x_nw,
                                            int          root_y_nw,
                                            int          w,
                                            int          h);
void        meta_window_resize_with_gravity (MetaWindow  *window,
                                             gboolean     user_op,
                                             int          w,
                                             int          h,
                                             int          gravity);


/* Return whether the window should be currently mapped */
gboolean    meta_window_should_be_showing   (MetaWindow  *window);

/* See warning in window.c about this function */
gboolean    __window_is_terminal (MetaWindow *window);

void        meta_window_update_struts      (MetaWindow  *window);

/* this gets root coords */
void        meta_window_get_position       (MetaWindow  *window,
                                            int         *x,
                                            int         *y);

/* Gets root coords for x, y, width & height of client window; uses
 * meta_window_get_position for x & y.
 */
void        meta_window_get_client_root_coords (MetaWindow    *window,
                                                MetaRectangle *rect);

/* gets position we need to set to stay in current position,
 * assuming position will be gravity-compensated. i.e.
 * this is the position a client would send in a configure
 * request.
 */
void        meta_window_get_gravity_position (MetaWindow  *window,
                                              int          gravity,
                                              int         *x,
                                              int         *y);
/* Get geometry for saving in the session; x/y are gravity
 * position, and w/h are in resize inc above the base size.
 */
void        meta_window_get_geometry         (MetaWindow  *window,
                                              int         *x,
                                              int         *y,
                                              int         *width,
                                              int         *height);

void        meta_window_kill               (MetaWindow  *window);
void        meta_window_focus              (MetaWindow  *window,
                                            guint32      timestamp);

void        meta_window_update_unfocused_button_grabs (MetaWindow *window);

/* Sends a client message */
void meta_window_send_icccm_message (MetaWindow *window,
                                     Atom        atom,
                                     guint32     timestamp);


void     meta_window_move_resize_request(MetaWindow *window,
                                         guint       value_mask,
                                         int         gravity,
                                         int         x,
                                         int         y,
                                         int         width,
                                         int         height);
gboolean meta_window_configure_request (MetaWindow *window,
                                        XEvent     *event);
gboolean meta_window_property_notify   (MetaWindow *window,
                                        XEvent     *event);
gboolean meta_window_client_message    (MetaWindow *window,
                                        XEvent     *event);
gboolean meta_window_notify_focus      (MetaWindow *window,
                                        XEvent     *event);

void     meta_window_set_current_workspace_hint (MetaWindow *window);

unsigned long meta_window_get_net_wm_desktop (MetaWindow *window);

void meta_window_show_menu (MetaWindow *window,
                            int         root_x,
                            int         root_y,
                            int         button,
                            guint32     timestamp);

gboolean meta_window_titlebar_is_onscreen    (MetaWindow *window);
void     meta_window_shove_titlebar_onscreen (MetaWindow *window);

void meta_window_set_gravity (MetaWindow *window,
                              int         gravity);

void meta_window_handle_mouse_grab_op_event (MetaWindow *window,
                                             XEvent     *event);

void meta_window_handle_keyboard_grab_op_event (MetaWindow *window,
                                                XEvent     *event);

GList* meta_window_get_workspaces (MetaWindow *window);

gboolean meta_window_located_on_workspace (MetaWindow    *window,
                                           MetaWorkspace *workspace);

void meta_window_get_work_area_current_monitor (MetaWindow    *window,
                                                MetaRectangle *area);
void meta_window_get_work_area_for_monitor     (MetaWindow    *window,
                                                int            which_monitor,
                                                MetaRectangle *area);
void meta_window_get_work_area_all_monitors    (MetaWindow    *window,
                                                MetaRectangle *area);

void meta_window_get_current_tile_area         (MetaWindow    *window,
                                                MetaRectangle *tile_area);

void meta_window_get_tile_threshold_area_for_mode        (MetaWindow    *window,
                                                          MetaRectangle  work_area,
                                                          MetaTileMode   mode,
                                                          MetaRectangle *tile_area,
                                                          gint           zone_width);

gboolean meta_window_same_application (MetaWindow *window,
                                       MetaWindow *other_window);

#define META_WINDOW_IN_NORMAL_TAB_CHAIN_TYPE(w) \
  ((w)->type != META_WINDOW_DOCK && (w)->type != META_WINDOW_DESKTOP)
#define META_WINDOW_IN_NORMAL_TAB_CHAIN(w) \
  (((w)->input || (w)->take_focus ) && META_WINDOW_IN_NORMAL_TAB_CHAIN_TYPE (w) && (!(w)->skip_taskbar))
#define META_WINDOW_IN_DOCK_TAB_CHAIN(w) \
  (((w)->input || (w)->take_focus) && (! META_WINDOW_IN_NORMAL_TAB_CHAIN_TYPE (w) || (w)->skip_taskbar))
#define META_WINDOW_IN_GROUP_TAB_CHAIN(w, g) \
  (((w)->input || (w)->take_focus) && (!g || meta_window_get_group(w)==g))

void meta_window_refresh_resize_popup (MetaWindow *window);

void meta_window_free_delete_dialog (MetaWindow *window);


void meta_window_begin_grab_op (MetaWindow *window,
                                MetaGrabOp  op,
                                gboolean    frame_action,
                                guint32     timestamp);

void meta_window_update_keyboard_resize (MetaWindow *window,
                                         gboolean    update_cursor);
void meta_window_update_keyboard_move   (MetaWindow *window);

void meta_window_update_layer (MetaWindow *window);

void meta_window_recalc_features    (MetaWindow *window);

void meta_window_recalc_window_type (MetaWindow *window);

void meta_window_stack_just_below (MetaWindow *window,
                                   MetaWindow *below_this_one);

void meta_window_set_user_time (MetaWindow *window,
                                guint32     timestamp);

void meta_window_update_icon_now (MetaWindow *window);

void meta_window_update_role (MetaWindow *window);
void meta_window_update_net_wm_type (MetaWindow *window);
void meta_window_update_for_monitors_changed (MetaWindow *window);
void meta_window_update_on_all_workspaces (MetaWindow *window);

void meta_window_propagate_focus_appearance (MetaWindow *window,
                                             gboolean    focused);

gboolean meta_window_should_attach_to_parent (MetaWindow *window);
gboolean meta_window_can_tile_side_by_side   (MetaWindow *window);
gboolean meta_window_can_tile_top_bottom (MetaWindow *window);
gboolean meta_window_can_tile_corner         (MetaWindow *window);
MetaSide meta_window_get_tile_side (MetaWindow *window);

inline void meta_window_get_size_limits   (const MetaWindow        *window,
                                          const MetaFrameBorders  *borders,
                                          gboolean include_frame,
                                          MetaRectangle *min_size,
                                          MetaRectangle *max_size);

void meta_window_compute_tile_match (MetaWindow *window);

HUDTileRestrictions meta_window_get_tile_restrictions (MetaWindow *window);

gboolean meta_window_mouse_on_edge (MetaWindow *window,
                                    gint        x,
                                    gint        y);

guint meta_window_get_current_zone (MetaWindow   *window,
                                    MetaRectangle monitor,
                                    MetaRectangle work_area,
                                    int           x,
                                    int           y,
                                    int           zone_threshold);

void       meta_window_set_tile_type (MetaWindow *window,
                                      MetaWindowTileType  type);
MetaWindowTileType  meta_window_get_tile_type (MetaWindow *window);

#endif
