/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X property convenience routines */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#ifndef META_XPROPS_H
#define META_XPROPS_H

#include <X11/Xutil.h>
#include <X11/extensions/sync.h>

#include "meta/display.h"

/* Copied from Lesstif by way of GTK. Rudimentary docs can be
 * found in some Motif reference guides online.
 */
typedef struct {
    uint32_t flags;
    uint32_t functions;
    uint32_t decorations;
    uint32_t input_mode;
    uint32_t status;
} MotifWmHints, MwmHints;

#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)

#define MWM_FUNC_ALL            (1L << 0)
#define MWM_FUNC_RESIZE         (1L << 1)
#define MWM_FUNC_MOVE           (1L << 2)
#define MWM_FUNC_MINIMIZE       (1L << 3)
#define MWM_FUNC_MAXIMIZE       (1L << 4)
#define MWM_FUNC_CLOSE          (1L << 5)

#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)

#define MWM_INPUT_MODELESS 0
#define MWM_INPUT_PRIMARY_APPLICATION_MODAL 1
#define MWM_INPUT_SYSTEM_MODAL 2
#define MWM_INPUT_FULL_APPLICATION_MODAL 3
#define MWM_INPUT_APPLICATION_MODAL MWM_INPUT_PRIMARY_APPLICATION_MODAL

#define MWM_TEAROFF_WINDOW	(1L<<0)

/* These all return the memory from Xlib, so require an XFree()
 * when they return TRUE. They return TRUE on success.
 */
gboolean meta_prop_get_motif_hints   (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            xatom,
                                      MotifWmHints  **hints_p);
gboolean meta_prop_get_cardinal_list (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            xatom,
                                      uint32_t      **cardinals_p,
                                      int            *n_cardinals_p);
gboolean meta_prop_get_latin1_string (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            xatom,
                                      char          **str_p);
gboolean meta_prop_get_utf8_list     (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            xatom,
                                      char         ***str_p,
                                      int            *n_str_p);
void     meta_prop_set_utf8_string_hint
                                     (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            atom,
                                      const char     *val);
gboolean meta_prop_get_window        (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            xatom,
                                      Window         *window_p);
gboolean meta_prop_get_cardinal      (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      Atom            xatom,
                                      uint32_t       *cardinal_p);
gboolean meta_prop_get_cardinal_with_atom_type (MetaX11Display *x11_display,
                                                Window          xwindow,
                                                Atom            xatom,
                                                Atom            prop_type,
                                                uint32_t       *cardinal_p);

typedef enum
{
  META_PROP_VALUE_INVALID,
  META_PROP_VALUE_UTF8,
  META_PROP_VALUE_STRING,
  META_PROP_VALUE_STRING_AS_UTF8,
  META_PROP_VALUE_MOTIF_HINTS,
  META_PROP_VALUE_CARDINAL,
  META_PROP_VALUE_WINDOW,
  META_PROP_VALUE_CARDINAL_LIST,
  META_PROP_VALUE_UTF8_LIST,
  META_PROP_VALUE_ATOM_LIST,
  META_PROP_VALUE_TEXT_PROPERTY, /* comes back as UTF-8 string */
  META_PROP_VALUE_WM_HINTS,
  META_PROP_VALUE_CLASS_HINT,
  META_PROP_VALUE_SIZE_HINTS,
  META_PROP_VALUE_SYNC_COUNTER,     /* comes back as CARDINAL */
  META_PROP_VALUE_SYNC_COUNTER_LIST /* comes back as CARDINAL */
} MetaPropValueType;

/* used to request/return/store property values */
typedef struct
{
  MetaPropValueType type;
  Atom atom;
  Atom required_type; /* autofilled if None */

  union
  {
    char *str;
    MotifWmHints *motif_hints;
    Window xwindow;
    uint32_t cardinal;
    XWMHints *wm_hints;
    XClassHint class_hint;
    XSyncCounter xcounter;
    struct
    {
      uint32_t *counters;
      int       n_counters;
    } xcounter_list;

    struct
    {
      XSizeHints   *hints;
      unsigned long flags;
    } size_hints;

    struct
    {
      uint32_t *cardinals;
      int       n_cardinals;
    } cardinal_list;

    struct
    {
      char **strings;
      int    n_strings;
    } string_list;

    struct
    {
      uint32_t *atoms;
      int       n_atoms;
    } atom_list;

  } v;

} MetaPropValue;

/* Each value has type and atom initialized. If there's an error,
 * or property is unset, type comes back as INVALID;
 * else type comes back as it originated, and the data
 * is filled in.
 */
void meta_prop_get_values (MetaX11Display *x11_display,
                           Window          xwindow,
                           MetaPropValue  *values,
                           int             n_values);

void meta_prop_free_values (MetaPropValue *values,
                            int            n_values);

#endif




