/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin X display handler */

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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef META_DISPLAY_PRIVATE_H
#define META_DISPLAY_PRIVATE_H

#ifndef PACKAGE
#error "config.h not included"
#endif

#include <glib.h>
#include <X11/Xlib.h>
#include "eventqueue.h"
#include <meta/common.h>
#include <meta/boxes.h>
#include <meta/display.h>
#include "keybindings-private.h"
#include <meta/prefs.h>

#ifdef HAVE_STARTUP_NOTIFICATION
#include <libsn/sn.h>
#endif

#ifdef HAVE_XSYNC
#include <X11/extensions/sync.h>
#endif

typedef struct _MetaStack      MetaStack;
typedef struct _MetaUISlave    MetaUISlave;

typedef struct _MetaGroupPropHooks  MetaGroupPropHooks;
typedef struct _MetaWindowPropHooks MetaWindowPropHooks;

typedef struct MetaEdgeResistanceData MetaEdgeResistanceData;

typedef void (* MetaWindowPingFunc) (MetaDisplay *display,
				     Window       xwindow,
				     guint32      timestamp,
				     gpointer     user_data);

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

typedef enum {
  META_TILE_NONE,
  META_TILE_LEFT,
  META_TILE_RIGHT,
  META_TILE_ULC,
  META_TILE_LLC,
  META_TILE_URC,
  META_TILE_LRC,
  META_TILE_TOP,
  META_TILE_BOTTOM,
  META_TILE_MAXIMIZE
} MetaTileMode;

struct _MetaDisplay
{
  GObject parent_instance;
  
  char *name;
  Display *xdisplay;
  char *hostname;

  Window leader_window;
  Window timestamp_pinging_window;

  /* Pull in all the names of atoms as fields; we will intern them when the
   * class is constructed.
   */
#define item(x)  Atom atom_##x;
#include <meta/atomnames.h>
#undef item

  /* This is the actual window from focus events,
   * not the one we last set
   */
  MetaWindow *focus_window;

  /* window we are expecting a FocusIn event for or the current focus
   * window if we are not expecting any FocusIn/FocusOut events; not
   * perfect because applications can call XSetInputFocus directly.
   * (It could also be messed up if a timestamp later than current
   * time is sent to meta_display_set_input_focus_window, though that
   * would be a programming error).  See bug 154598 for more info.
   */
  MetaWindow *expected_focus_window;

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

  guint static_gravity_works : 1;
  
  /*< private-ish >*/
  guint error_trap_synced_at_last_pop : 1;
  MetaEventQueue *events;
  GSList *screens;
  MetaScreen *active_screen;
  GHashTable *window_ids;
  int error_traps;
  int (* error_trap_handler) (Display     *display,
                              XErrorEvent *error);  
  int server_grab_count;

  /* serials of leave/unmap events that may
   * correspond to an enter event we should
   * ignore
   */
  unsigned long ignored_crossing_serials[N_IGNORED_CROSSING_SERIALS];
  Window ungrab_should_not_cause_focus_window;
  
  guint32 current_time;

  /* We maintain a sequence counter, incremented for each #MetaWindow
   * created.  This is exposed by meta_window_get_stable_sequence()
   * but is otherwise not used inside muffin.
   *
   * It can be useful to plugins which want to sort windows in a
   * stable fashion.
   */
  guint32 window_sequence_counter;

  /* Pings which we're waiting for a reply from */
  GSList     *pending_pings;

  /* Pending autoraise */
  guint       autoraise_timeout_id;
  MetaWindow* autoraise_window;

  /* Alt+click button grabs */
  unsigned int window_grab_modifiers;
  
  /* current window operation */
  MetaGrabOp  grab_op;
  MetaScreen *grab_screen;
  MetaWindow *grab_window;
  Window      grab_xwindow;
  int         grab_button;
  int         grab_anchor_root_x;
  int         grab_anchor_root_y;
  MetaRectangle grab_anchor_window_pos;
  MetaTileMode  grab_tile_mode;
  int           grab_tile_monitor_number;
  int         grab_latest_motion_x;
  int         grab_latest_motion_y;
  gulong      grab_mask;
  guint       grab_have_pointer : 1;
  guint       grab_have_keyboard : 1;
  guint       grab_frame_action : 1;
  /* During a resize operation, the directions in which we've broken
   * out of the initial maximization state */
  guint       grab_resize_unmaximize : 2; /* MetaMaximizeFlags */
  MetaRectangle grab_initial_window_pos;
  int         grab_initial_x, grab_initial_y;  /* These are only relevant for */
  gboolean    grab_threshold_movement_reached; /* raise_on_click == FALSE.    */
  MetaResizePopup *grab_resize_popup;
  GTimeVal    grab_last_moveresize_time;
  guint32     grab_motion_notify_time;
  GList*      grab_old_window_stacking;
  MetaEdgeResistanceData *grab_edge_resistance_data;
  unsigned int grab_last_user_action_was_snap;

  /* we use property updates as sentinels for certain window focus events
   * to avoid some race conditions on EnterNotify events
   */
  int         sentinel_counter;

#ifdef HAVE_XKB
  int         xkb_base_event_type;
  guint32     last_bell_time;
#endif
#ifdef HAVE_XSYNC
  /* alarm monitoring client's _NET_WM_SYNC_REQUEST_COUNTER */
  XSyncAlarm  grab_sync_request_alarm;
#endif
  int	      grab_resize_timeout_id;

  /* Keybindings stuff */
  MetaKeyBinding *key_bindings;
  int             n_key_bindings;
  int             min_keycode;
  int             max_keycode;
  KeySym *keymap;
  int keysyms_per_keycode;
  XModifierKeymap *modmap;
  unsigned int above_tab_keycode;
  unsigned int ignored_modifier_mask;
  unsigned int num_lock_mask;
  unsigned int scroll_lock_mask;
  unsigned int hyper_mask;
  unsigned int super_mask;
  unsigned int meta_mask;
  MetaKeyCombo overlay_key_combo;
  gboolean overlay_key_only_pressed;
  
  /* Monitor cache */
  unsigned int monitor_cache_invalidated : 1;

  /* Opening the display */
  unsigned int display_opening : 1;

  /* Closing down the display */
  int closing;

  /* Managed by group.c */
  GHashTable *groups_by_leader;

  /* currently-active window menu if any */
  MetaWindowMenu *window_menu;
  MetaWindow *window_with_menu;

  /* Managed by window-props.c */
  MetaWindowPropHooks *prop_hooks_table;
  GHashTable *prop_hooks;
  int n_prop_hooks;

  /* Managed by group-props.c */
  MetaGroupPropHooks *group_prop_hooks;

  /* Managed by compositor.c */
  MetaCompositor *compositor;

  int render_event_base;
  int render_error_base;

  int composite_event_base;
  int composite_error_base;
  int composite_major_version;
  int composite_minor_version;
  int damage_event_base;
  int damage_error_base;
  int xfixes_event_base;
  int xfixes_error_base;
  
#ifdef HAVE_STARTUP_NOTIFICATION
  SnDisplay *sn_display;
#endif
#ifdef HAVE_XSYNC
  int xsync_event_base;
  int xsync_error_base;
#endif
#ifdef HAVE_SHAPE
  int shape_event_base;
  int shape_error_base;
#endif
#ifdef HAVE_XSYNC
  unsigned int have_xsync : 1;
#define META_DISPLAY_HAS_XSYNC(display) ((display)->have_xsync)
#else
#define META_DISPLAY_HAS_XSYNC(display) FALSE
#endif
#ifdef HAVE_SHAPE
  unsigned int have_shape : 1;
#define META_DISPLAY_HAS_SHAPE(display) ((display)->have_shape)
#else
#define META_DISPLAY_HAS_SHAPE(display) FALSE
#endif
  unsigned int have_render : 1;
#define META_DISPLAY_HAS_RENDER(display) ((display)->have_render)
  unsigned int have_composite : 1;
  unsigned int have_damage : 1;
  unsigned int have_xfixes : 1;
#define META_DISPLAY_HAS_COMPOSITE(display) ((display)->have_composite)
#define META_DISPLAY_HAS_DAMAGE(display) ((display)->have_damage)
#define META_DISPLAY_HAS_XFIXES(display) ((display)->have_xfixes)
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
void          meta_display_close               (MetaDisplay *display,
                                                guint32      timestamp);
MetaScreen*   meta_display_screen_for_x_screen (MetaDisplay *display,
                                                Screen      *screen);
MetaScreen*   meta_display_screen_for_xwindow  (MetaDisplay *display,
                                                Window       xindow);
void          meta_display_grab                (MetaDisplay *display);
void          meta_display_ungrab              (MetaDisplay *display);

void          meta_display_unmanage_windows_for_screen (MetaDisplay *display,
                                                        MetaScreen  *screen,
                                                        guint32      timestamp);

/* Utility function to compare the stacking of two windows */
int           meta_display_stack_cmp           (const void *a,
                                                const void *b);

/* A given MetaWindow may have various X windows that "belong"
 * to it, such as the frame window.
 */
MetaWindow* meta_display_lookup_x_window     (MetaDisplay *display,
                                              Window       xwindow);
void        meta_display_register_x_window   (MetaDisplay *display,
                                              Window      *xwindowp,
                                              MetaWindow  *window);
void        meta_display_unregister_x_window (MetaDisplay *display,
                                              Window       xwindow);

void        meta_display_notify_window_created (MetaDisplay  *display,
                                                MetaWindow   *window);

MetaDisplay* meta_display_for_x_display  (Display     *xdisplay);
MetaDisplay* meta_get_display            (void);

Cursor         meta_display_create_x_cursor (MetaDisplay *display,
                                             MetaCursor   cursor);

void     meta_display_set_grab_op_cursor (MetaDisplay *display,
                                          MetaScreen  *screen,
                                          MetaGrabOp   op,
                                          gboolean     change_pointer,
                                          Window       grab_xwindow,
                                          guint32      timestamp);

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

/* make a request to ensure the event serial has changed */
void     meta_display_increment_event_serial (MetaDisplay *display);

void     meta_display_update_active_window_hint (MetaDisplay *display);

/* utility goo */
const char* meta_event_mode_to_string   (int m);
const char* meta_event_detail_to_string (int d);

void meta_display_queue_retheme_all_windows (MetaDisplay *display);
void meta_display_retheme_all (void);

void meta_display_set_cursor_theme (const char *theme, 
				    int         size);

void meta_display_ping_window      (MetaDisplay        *display,
                                    MetaWindow         *window,
                                    guint32             timestamp,
                                    MetaWindowPingFunc  ping_reply_func,
                                    MetaWindowPingFunc  ping_timeout_func,
                                    void               *user_data);
gboolean meta_display_window_has_pending_pings (MetaDisplay        *display,
						MetaWindow         *window);

int meta_resize_gravity_from_grab_op (MetaGrabOp op);

gboolean meta_grab_op_is_moving   (MetaGrabOp op);
gboolean meta_grab_op_is_resizing (MetaGrabOp op);

void meta_display_devirtualize_modifiers (MetaDisplay        *display,
                                          MetaVirtualModifier modifiers,
                                          unsigned int       *mask);

void meta_display_increment_focus_sentinel (MetaDisplay *display);
void meta_display_decrement_focus_sentinel (MetaDisplay *display);
gboolean meta_display_focus_sentinel_clear (MetaDisplay *display);

void meta_display_queue_autoraise_callback  (MetaDisplay *display,
                                             MetaWindow  *window);
void meta_display_remove_autoraise_callback (MetaDisplay *display);

void meta_display_overlay_key_activate (MetaDisplay *display);

/* In above-tab-keycode.c */
guint meta_display_get_above_tab_keycode (MetaDisplay *display);

#endif
