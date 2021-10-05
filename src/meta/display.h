/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#ifndef META_DISPLAY_H
#define META_DISPLAY_H

#include <glib-object.h>
#include <X11/Xlib.h>

#include <meta/types.h>
#include <meta/prefs.h>
#include <meta/common.h>
#include <meta/workspace.h>
#include <meta/meta-sound-player.h>
#include <meta/meta-startup-notification.h>

/**
 * MetaTabList:
 * @META_TAB_LIST_NORMAL: Normal windows
 * @META_TAB_LIST_DOCKS: Dock windows
 * @META_TAB_LIST_GROUP: Groups
 * @META_TAB_LIST_NORMAL_ALL: All windows
 */
typedef enum
{
  META_TAB_LIST_NORMAL,
  META_TAB_LIST_DOCKS,
  META_TAB_LIST_GROUP,
  META_TAB_LIST_NORMAL_ALL
} MetaTabList;

/**
 * MetaTabShowType:
 * @META_TAB_SHOW_ICON: Show icon (Alt-Tab mode)
 * @META_TAB_SHOW_INSTANTLY: Show instantly (Alt-Esc mode)
 */
typedef enum
{
  META_TAB_SHOW_ICON,      /* Alt-Tab mode */
  META_TAB_SHOW_INSTANTLY  /* Alt-Esc mode */
} MetaTabShowType;

typedef enum
{
  META_PAD_ACTION_BUTTON, /* Action is a button */
  META_PAD_ACTION_RING,   /* Action is a ring */
  META_PAD_ACTION_STRIP,  /* Action is a strip */
} MetaPadActionType;

typedef struct _MetaDisplayClass MetaDisplayClass;

#define META_TYPE_DISPLAY              (meta_display_get_type ())
#define META_DISPLAY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), META_TYPE_DISPLAY, MetaDisplay))
#define META_DISPLAY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_DISPLAY, MetaDisplayClass))
#define META_IS_DISPLAY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), META_TYPE_DISPLAY))
#define META_IS_DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_DISPLAY))
#define META_DISPLAY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_DISPLAY, MetaDisplayClass))

META_EXPORT
GType meta_display_get_type (void) G_GNUC_CONST;

#define meta_XFree(p) do { if ((p)) XFree ((p)); } while (0)

META_EXPORT
gboolean meta_display_supports_extended_barriers (MetaDisplay *display);

META_EXPORT
void meta_display_close (MetaDisplay *display,
                         guint32      timestamp);

META_EXPORT
MetaCompositor *meta_display_get_compositor  (MetaDisplay *display);

META_EXPORT
MetaX11Display *meta_display_get_x11_display (MetaDisplay *display);

META_EXPORT
MetaWindow *meta_display_get_focus_window (MetaDisplay *display);

META_EXPORT
gboolean meta_display_xserver_time_is_before (MetaDisplay *display,
                                              guint32      time1,
                                              guint32      time2);

META_EXPORT
guint32 meta_display_get_last_user_time (MetaDisplay *display);

META_EXPORT
guint32 meta_display_get_current_time (MetaDisplay *display);

META_EXPORT
guint32 meta_display_get_current_time_roundtrip (MetaDisplay *display);

META_EXPORT
GList* meta_display_get_tab_list (MetaDisplay   *display,
                                  MetaTabList    type,
                                  MetaWorkspace *workspace);

META_EXPORT
MetaWindow* meta_display_get_tab_next (MetaDisplay   *display,
                                       MetaTabList    type,
                                       MetaWorkspace *workspace,
                                       MetaWindow    *window,
                                       gboolean       backward);

META_EXPORT
MetaWindow* meta_display_get_tab_current (MetaDisplay   *display,
                                          MetaTabList    type,
                                          MetaWorkspace *workspace);

META_EXPORT
gboolean meta_display_begin_grab_op (MetaDisplay *display,
                                     MetaWindow  *window,
                                     MetaGrabOp   op,
                                     gboolean     pointer_already_grabbed,
                                     gboolean     frame_action,
                                     int          button,
                                     gulong       modmask,
                                     guint32      timestamp,
                                     int          root_x,
                                     int          root_y);

META_EXPORT
void     meta_display_end_grab_op   (MetaDisplay *display,
                                     guint32      timestamp);

META_EXPORT
MetaGrabOp meta_display_get_grab_op (MetaDisplay *display);

META_EXPORT
guint meta_display_add_keybinding    (MetaDisplay         *display,
                                      const char          *name,
                                      GSettings           *settings,
                                      MetaKeyBindingFlags  flags,
                                      MetaKeyHandlerFunc   handler,
                                      gpointer             user_data,
                                      GDestroyNotify       free_data);

META_EXPORT
gboolean meta_display_remove_keybinding (MetaDisplay         *display,
                                         const char          *name);

META_EXPORT
guint    meta_display_grab_accelerator   (MetaDisplay         *display,
                                          const char          *accelerator,
                                          MetaKeyBindingFlags  flags);

META_EXPORT
gboolean meta_display_ungrab_accelerator (MetaDisplay *display,
                                          guint        action_id);

META_EXPORT
guint meta_display_get_keybinding_action (MetaDisplay  *display,
                                          unsigned int  keycode,
                                          unsigned long mask);

META_EXPORT
GSList *meta_display_sort_windows_by_stacking (MetaDisplay *display,
                                               GSList      *windows);

META_EXPORT
void meta_display_add_ignored_crossing_serial (MetaDisplay  *display,
                                               unsigned long serial);

META_EXPORT
void meta_display_clear_mouse_mode (MetaDisplay *display);

META_EXPORT
void meta_display_freeze_keyboard (MetaDisplay *display,
                                   guint32      timestamp);

META_EXPORT
void meta_display_ungrab_keyboard (MetaDisplay *display,
                                   guint32      timestamp);

META_EXPORT
void meta_display_unfreeze_keyboard (MetaDisplay *display,
                                     guint32      timestamp);

META_EXPORT
gboolean meta_display_is_pointer_emulating_sequence (MetaDisplay          *display,
                                                     ClutterEventSequence *sequence);

META_EXPORT
void    meta_display_request_pad_osd      (MetaDisplay        *display,
                                           ClutterInputDevice *pad,
                                           gboolean            edition_mode);

META_EXPORT
gchar * meta_display_get_pad_action_label (MetaDisplay        *display,
                                           ClutterInputDevice *pad,
                                           MetaPadActionType   action_type,
                                           guint               action_number);

META_EXPORT
void meta_display_get_size (MetaDisplay *display,
                            int         *width,
                            int         *height);

META_EXPORT
void meta_display_set_cursor (MetaDisplay *display,
                              MetaCursor   cursor);

/**
 * MetaDisplayDirection:
 * @META_DISPLAY_UP: up
 * @META_DISPLAY_DOWN: down
 * @META_DISPLAY_LEFT: left
 * @META_DISPLAY_RIGHT: right
 */
typedef enum
{
  META_DISPLAY_UP,
  META_DISPLAY_DOWN,
  META_DISPLAY_LEFT,
  META_DISPLAY_RIGHT
} MetaDisplayDirection;

META_EXPORT
int  meta_display_get_n_monitors       (MetaDisplay   *display);

META_EXPORT
int  meta_display_get_primary_monitor  (MetaDisplay   *display);

META_EXPORT
int  meta_display_get_current_monitor  (MetaDisplay   *display);

META_EXPORT
void meta_display_get_monitor_geometry (MetaDisplay   *display,
                                        int            monitor,
                                        MetaRectangle *geometry);

META_EXPORT
float meta_display_get_monitor_scale (MetaDisplay *display,
                                      int          monitor);

META_EXPORT
gboolean meta_display_get_monitor_in_fullscreen (MetaDisplay *display,
                                                 int          monitor);

META_EXPORT
int meta_display_get_monitor_index_for_rect (MetaDisplay   *display,
                                             MetaRectangle *rect);

META_EXPORT
int meta_display_get_monitor_neighbor_index (MetaDisplay         *display,
                                             int                  which_monitor,
                                             MetaDisplayDirection dir);

META_EXPORT
void meta_display_focus_default_window (MetaDisplay *display,
                                        guint32      timestamp);

/**
 * MetaDisplayCorner:
 * @META_DISPLAY_TOPLEFT: top-left corner
 * @META_DISPLAY_TOPRIGHT: top-right corner
 * @META_DISPLAY_BOTTOMLEFT: bottom-left corner
 * @META_DISPLAY_BOTTOMRIGHT: bottom-right corner
 */
typedef enum
{
  META_DISPLAY_TOPLEFT,
  META_DISPLAY_TOPRIGHT,
  META_DISPLAY_BOTTOMLEFT,
  META_DISPLAY_BOTTOMRIGHT
} MetaDisplayCorner;

META_EXPORT
MetaWorkspaceManager *meta_display_get_workspace_manager (MetaDisplay *display);

/**
 * meta_display_get_startup_notification: (skip)
 */
META_EXPORT
MetaStartupNotification * meta_display_get_startup_notification (MetaDisplay *display);

META_EXPORT
MetaSoundPlayer * meta_display_get_sound_player (MetaDisplay *display);

META_EXPORT
MetaSelection * meta_display_get_selection (MetaDisplay *display);

META_EXPORT
void meta_display_set_input_focus   (MetaDisplay *display,
                                     MetaWindow  *window,
                                     gboolean     focus_frame,
                                     guint32      timestamp);
META_EXPORT
void meta_display_unset_input_focus (MetaDisplay *display,
                                     guint32      timestamp);

#endif
