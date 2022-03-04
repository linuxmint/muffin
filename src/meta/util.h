/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter utilities */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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

#ifndef META_UTIL_H
#define META_UTIL_H

#include <glib.h>
#include <glib-object.h>

#include <meta/common.h>

META_EXPORT
gboolean meta_is_verbose  (void);

META_EXPORT
gboolean meta_is_debugging (void);

META_EXPORT
gboolean meta_is_syncing (void);

META_EXPORT
gboolean meta_is_wayland_compositor (void);

META_EXPORT
void meta_debug_spew_real (const char *format,
                           ...) G_GNUC_PRINTF (1, 2);

META_EXPORT
void meta_verbose_real    (const char *format,
                           ...) G_GNUC_PRINTF (1, 2);

META_EXPORT
void meta_bug        (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);

META_EXPORT
void meta_warning    (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);

META_EXPORT
void meta_fatal      (const char *format,
                      ...) G_GNUC_PRINTF (1, 2) G_GNUC_NORETURN G_ANALYZER_NORETURN;

/**
 * MetaDebugTopic:
 * @META_DEBUG_VERBOSE: verbose logging
 * @META_DEBUG_FOCUS: focus
 * @META_DEBUG_WORKAREA: workarea
 * @META_DEBUG_STACK: stack
 * @META_DEBUG_THEMES: themes
 * @META_DEBUG_SM: session management
 * @META_DEBUG_EVENTS: events
 * @META_DEBUG_WINDOW_STATE: window state
 * @META_DEBUG_WINDOW_OPS: window operations
 * @META_DEBUG_GEOMETRY: geometry
 * @META_DEBUG_PLACEMENT: window placement
 * @META_DEBUG_PING: ping
 * @META_DEBUG_XINERAMA: Xinerama
 * @META_DEBUG_KEYBINDINGS: keybindings
 * @META_DEBUG_SYNC: sync
 * @META_DEBUG_ERRORS: errors
 * @META_DEBUG_STARTUP: startup
 * @META_DEBUG_PREFS: preferences
 * @META_DEBUG_GROUPS: groups
 * @META_DEBUG_RESIZING: resizing
 * @META_DEBUG_SHAPES: shapes
 * @META_DEBUG_COMPOSITOR: compositor
 * @META_DEBUG_EDGE_RESISTANCE: edge resistance
 */
typedef enum
{
  META_DEBUG_VERBOSE         = -1,
  META_DEBUG_FOCUS           = 1 << 0,
  META_DEBUG_WORKAREA        = 1 << 1,
  META_DEBUG_STACK           = 1 << 2,
  META_DEBUG_THEMES          = 1 << 3,
  META_DEBUG_SM              = 1 << 4,
  META_DEBUG_EVENTS          = 1 << 5,
  META_DEBUG_WINDOW_STATE    = 1 << 6,
  META_DEBUG_WINDOW_OPS      = 1 << 7,
  META_DEBUG_GEOMETRY        = 1 << 8,
  META_DEBUG_PLACEMENT       = 1 << 9,
  META_DEBUG_PING            = 1 << 10,
  META_DEBUG_XINERAMA        = 1 << 11,
  META_DEBUG_KEYBINDINGS     = 1 << 12,
  META_DEBUG_SYNC            = 1 << 13,
  META_DEBUG_ERRORS          = 1 << 14,
  META_DEBUG_STARTUP         = 1 << 15,
  META_DEBUG_PREFS           = 1 << 16,
  META_DEBUG_GROUPS          = 1 << 17,
  META_DEBUG_RESIZING        = 1 << 18,
  META_DEBUG_SHAPES          = 1 << 19,
  META_DEBUG_COMPOSITOR      = 1 << 20,
  META_DEBUG_EDGE_RESISTANCE = 1 << 21,
  META_DEBUG_DBUS            = 1 << 22,
  META_DEBUG_INPUT           = 1 << 23
} MetaDebugTopic;

META_EXPORT
void meta_topic_real      (MetaDebugTopic topic,
                           const char    *format,
                           ...) G_GNUC_PRINTF (2, 3);

META_EXPORT
void meta_add_verbose_topic    (MetaDebugTopic topic);

META_EXPORT
void meta_remove_verbose_topic (MetaDebugTopic topic);

META_EXPORT
void meta_push_no_msg_prefix (void);

META_EXPORT
void meta_pop_no_msg_prefix  (void);

META_EXPORT
gint  meta_unsigned_long_equal (gconstpointer v1,
                                gconstpointer v2);

META_EXPORT
guint meta_unsigned_long_hash  (gconstpointer v);

META_EXPORT
const char* meta_frame_type_to_string (MetaFrameType type);
META_EXPORT
const char* meta_gravity_to_string (MetaGravity gravity);

META_EXPORT
char* meta_external_binding_name_for_action (guint keybinding_action);

META_EXPORT
char* meta_g_utf8_strndup (const gchar *src, gsize n);

META_EXPORT
GPid meta_show_dialog (const char *type,
                       const char *message,
                       const char *timeout,
                       const char *display,
                       const char *ok_text,
                       const char *cancel_text,
                       const char *icon_name,
                       const int transient_for,
                       GSList *columns,
                       GSList *entries);

/* To disable verbose mode, we make these functions into no-ops */
#ifdef WITH_VERBOSE_MODE

#define meta_debug_spew meta_debug_spew_real
#define meta_verbose    meta_verbose_real
#define meta_topic      meta_topic_real

#else

#  ifdef G_HAVE_ISO_VARARGS
#    define meta_debug_spew(...)
#    define meta_verbose(...)
#    define meta_topic(...)
#  elif defined(G_HAVE_GNUC_VARARGS)
#    define meta_debug_spew(format...)
#    define meta_verbose(format...)
#    define meta_topic(format...)
#  else
#    error "This compiler does not support varargs macros and thus verbose mode can't be disabled meaningfully"
#  endif

#endif /* !WITH_VERBOSE_MODE */

/**
 * MetaLaterType:
 * @META_LATER_RESIZE: call in a resize processing phase that is done
 *   before GTK+ repainting (including window borders) is done.
 * @META_LATER_CALC_SHOWING: used by Mutter to compute which windows should be mapped
 * @META_LATER_CHECK_FULLSCREEN: used by Mutter to see if there's a fullscreen window
 * @META_LATER_SYNC_STACK: used by Mutter to send it's idea of the stacking order to the server
 * @META_LATER_BEFORE_REDRAW: call before the stage is redrawn
 * @META_LATER_IDLE: call at a very low priority (can be blocked
 *    by running animations or redrawing applications)
 **/
typedef enum
{
  META_LATER_RESIZE,
  META_LATER_CALC_SHOWING,
  META_LATER_CHECK_FULLSCREEN,
  META_LATER_SYNC_STACK,
  META_LATER_BEFORE_REDRAW,
  META_LATER_IDLE
} MetaLaterType;

META_EXPORT
guint meta_later_add    (MetaLaterType  when,
                         GSourceFunc    func,
                         gpointer       data,
                         GDestroyNotify notify);

META_EXPORT
void  meta_later_remove (guint          later_id);

typedef enum
{
  META_LOCALE_DIRECTION_LTR,
  META_LOCALE_DIRECTION_RTL,
} MetaLocaleDirection;

META_EXPORT
MetaLocaleDirection meta_get_locale_direction (void);

META_EXPORT
void meta_add_clutter_debug_flags (ClutterDebugFlag     debug_flags,
                                   ClutterDrawDebugFlag draw_flags,
                                   ClutterPickDebugFlag pick_flags);

META_EXPORT
void meta_remove_clutter_debug_flags (ClutterDebugFlag     debug_flags,
                                      ClutterDrawDebugFlag draw_flags,
                                      ClutterPickDebugFlag pick_flags);

#endif /* META_UTIL_H */
