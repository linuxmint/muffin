/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin common types shared by core.h and ui.h
 *
 * PLEASE KEEP IN SYNC WITH GSETTINGS SCHEMAS!
 */

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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef META_COMMON_H
#define META_COMMON_H

/* Don't include core headers here */
#include <stdlib.h>
#include <X11/Xlib.h>
#include <glib.h>
#include <gtk/gtk.h>

/**
 * SECTION:common
 * @title: Common
 * @short_description: Muffin common types
 */

typedef struct _MetaResizePopup MetaResizePopup;

typedef enum
{
  META_FRAME_ALLOWS_DELETE            = 1 << 0,
  META_FRAME_ALLOWS_MENU              = 1 << 1,
  META_FRAME_ALLOWS_MINIMIZE          = 1 << 2,
  META_FRAME_ALLOWS_MAXIMIZE          = 1 << 3,
  META_FRAME_ALLOWS_LEFT_RESIZE       = 1 << 4,
  META_FRAME_ALLOWS_RIGHT_RESIZE      = 1 << 5,
  META_FRAME_ALLOWS_TOP_RESIZE        = 1 << 6,
  META_FRAME_ALLOWS_BOTTOM_RESIZE     = 1 << 7,
  META_FRAME_HAS_FOCUS                = 1 << 8,
  META_FRAME_SHADED                   = 1 << 9,
  META_FRAME_STUCK                    = 1 << 10,
  META_FRAME_MAXIMIZED                = 1 << 11,
  META_FRAME_ALLOWS_SHADE             = 1 << 12,
  META_FRAME_ALLOWS_MOVE              = 1 << 13,
  META_FRAME_FULLSCREEN               = 1 << 14,
  META_FRAME_IS_FLASHING              = 1 << 15,
  META_FRAME_ABOVE                    = 1 << 16,
  META_FRAME_TILED_LEFT               = 1 << 17,
  META_FRAME_TILED_RIGHT              = 1 << 18,
  META_FRAME_ALLOWS_VERTICAL_RESIZE   = (META_FRAME_ALLOWS_TOP_RESIZE | META_FRAME_ALLOWS_BOTTOM_RESIZE),
  META_FRAME_ALLOWS_HORIZONTAL_RESIZE = (META_FRAME_ALLOWS_LEFT_RESIZE | META_FRAME_ALLOWS_RIGHT_RESIZE)
} MetaFrameFlags;

typedef enum
{
  META_MENU_OP_NONE        = 0,
  META_MENU_OP_DELETE      = 1 << 0,
  META_MENU_OP_MINIMIZE    = 1 << 1,
  META_MENU_OP_UNMAXIMIZE  = 1 << 2,
  META_MENU_OP_MAXIMIZE    = 1 << 3,
  META_MENU_OP_UNSHADE     = 1 << 4,
  META_MENU_OP_SHADE       = 1 << 5,
  META_MENU_OP_UNSTICK     = 1 << 6,
  META_MENU_OP_STICK       = 1 << 7,
  META_MENU_OP_WORKSPACES  = 1 << 8,
  META_MENU_OP_MOVE        = 1 << 9,
  META_MENU_OP_RESIZE      = 1 << 10,
  META_MENU_OP_ABOVE       = 1 << 11,
  META_MENU_OP_UNABOVE     = 1 << 12,
  META_MENU_OP_MOVE_LEFT   = 1 << 13,
  META_MENU_OP_MOVE_RIGHT  = 1 << 14,
  META_MENU_OP_MOVE_UP     = 1 << 15,
  META_MENU_OP_MOVE_DOWN   = 1 << 16,
  META_MENU_OP_RECOVER     = 1 << 17,
  META_MENU_OP_MOVE_NEW    = 1 << 18
} MetaMenuOp;

typedef struct _MetaWindowMenu MetaWindowMenu;

typedef void (* MetaWindowMenuFunc) (MetaWindowMenu *menu,
                                     Display        *xdisplay,
                                     Window          client_xwindow,
                                     guint32         timestamp,
                                     MetaMenuOp      op,
                                     int             workspace,
                                     gpointer        data);

/**
 * MetaGrabOp:
 * @META_GRAB_OP_NONE: None
 * @META_GRAB_OP_MOVING: Moving with pointer
 * @META_GRAB_OP_RESIZING_SE: Resizing SE with pointer
 * @META_GRAB_OP_RESIZING_S: Resizing S with pointer
 * @META_GRAB_OP_RESIZING_SW: Resizing SW with pointer
 * @META_GRAB_OP_RESIZING_N: Resizing N with pointer
 * @META_GRAB_OP_RESIZING_NE: Resizing NE with pointer
 * @META_GRAB_OP_RESIZING_NW: Resizing NW with pointer
 * @META_GRAB_OP_RESIZING_W: Resizing W with pointer
 * @META_GRAB_OP_RESIZING_E: Resizing E with pointer
 * @META_GRAB_OP_KEYBOARD_MOVING: Moving with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN: Resizing with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_S: Resizing S with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_N: Resizing N with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_W: Resizing W with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_E: Resizing E with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_SE: Resizing SE with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_NE: Resizing NE with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_SW: Resizing SW with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_NW: Resizing NS with keyboard
 * @META_GRAB_OP_COMPOSITOR: Compositor asked for grab
 */

/* when changing this enum, there are various switch statements
 * you have to update
 */
typedef enum
{
  META_GRAB_OP_NONE,

  /* Mouse ops */
  META_GRAB_OP_MOVING,
  META_GRAB_OP_RESIZING_SE,
  META_GRAB_OP_RESIZING_S,
  META_GRAB_OP_RESIZING_SW,
  META_GRAB_OP_RESIZING_N,
  META_GRAB_OP_RESIZING_NE,
  META_GRAB_OP_RESIZING_NW,
  META_GRAB_OP_RESIZING_W,
  META_GRAB_OP_RESIZING_E,

  /* Keyboard ops */
  META_GRAB_OP_KEYBOARD_MOVING,
  META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN,
  META_GRAB_OP_KEYBOARD_RESIZING_S,
  META_GRAB_OP_KEYBOARD_RESIZING_N,
  META_GRAB_OP_KEYBOARD_RESIZING_W,
  META_GRAB_OP_KEYBOARD_RESIZING_E,
  META_GRAB_OP_KEYBOARD_RESIZING_SE,
  META_GRAB_OP_KEYBOARD_RESIZING_NE,
  META_GRAB_OP_KEYBOARD_RESIZING_SW,
  META_GRAB_OP_KEYBOARD_RESIZING_NW,

  /* Alt+Tab */
  META_GRAB_OP_KEYBOARD_TABBING_NORMAL,
  META_GRAB_OP_KEYBOARD_TABBING_DOCK,

  /* Alt+Esc */
  META_GRAB_OP_KEYBOARD_ESCAPING_NORMAL,
  META_GRAB_OP_KEYBOARD_ESCAPING_DOCK,

  META_GRAB_OP_KEYBOARD_ESCAPING_GROUP,
  
  /* Alt+F6 */
  META_GRAB_OP_KEYBOARD_TABBING_GROUP,
  
  META_GRAB_OP_KEYBOARD_WORKSPACE_SWITCHING,
  
  /* Special grab op when the compositor asked for a grab */
  META_GRAB_OP_COMPOSITOR
} MetaGrabOp;

typedef enum
{
  META_CURSOR_DEFAULT,
  META_CURSOR_NORTH_RESIZE,
  META_CURSOR_SOUTH_RESIZE,
  META_CURSOR_WEST_RESIZE,
  META_CURSOR_EAST_RESIZE,
  META_CURSOR_SE_RESIZE,
  META_CURSOR_SW_RESIZE,
  META_CURSOR_NE_RESIZE,
  META_CURSOR_NW_RESIZE,
  META_CURSOR_MOVE_OR_RESIZE_WINDOW,
  META_CURSOR_BUSY

} MetaCursor;

typedef enum
{
  META_FRAME_TYPE_NORMAL,
  META_FRAME_TYPE_DIALOG,
  META_FRAME_TYPE_MODAL_DIALOG,
  META_FRAME_TYPE_UTILITY,
  META_FRAME_TYPE_MENU,
  META_FRAME_TYPE_BORDER,
  META_FRAME_TYPE_ATTACHED,
  META_FRAME_TYPE_LAST
} MetaFrameType;

typedef enum
{
  /* Create gratuitous divergence from regular
   * X mod bits, to be sure we find bugs
   */
  META_VIRTUAL_SHIFT_MASK    = 1 << 5,
  META_VIRTUAL_CONTROL_MASK  = 1 << 6,
  META_VIRTUAL_ALT_MASK      = 1 << 7,  
  META_VIRTUAL_META_MASK     = 1 << 8,
  META_VIRTUAL_SUPER_MASK    = 1 << 9,
  META_VIRTUAL_HYPER_MASK    = 1 << 10,
  META_VIRTUAL_MOD2_MASK     = 1 << 11,
  META_VIRTUAL_MOD3_MASK     = 1 << 12,
  META_VIRTUAL_MOD4_MASK     = 1 << 13,
  META_VIRTUAL_MOD5_MASK     = 1 << 14
} MetaVirtualModifier;

/* Relative directions or sides seem to come up all over the place... */
/* FIXME: Replace
 *   screen.[ch]:MetaScreenDirection,
 *   workspace.[ch]:MetaMotionDirection,
 * with the use of MetaDirection.
 */
typedef enum
{
  META_DIRECTION_LEFT       = 1 << 0,
  META_DIRECTION_RIGHT      = 1 << 1,
  META_DIRECTION_TOP        = 1 << 2,
  META_DIRECTION_BOTTOM     = 1 << 3,

  /* Some aliases for making code more readable for various circumstances. */
  META_DIRECTION_UP         = META_DIRECTION_TOP,
  META_DIRECTION_DOWN       = META_DIRECTION_BOTTOM,

  /* A few more definitions using aliases */
  META_DIRECTION_HORIZONTAL = META_DIRECTION_LEFT | META_DIRECTION_RIGHT,
  META_DIRECTION_VERTICAL   = META_DIRECTION_UP   | META_DIRECTION_DOWN,
} MetaDirection;

/* Negative to avoid conflicting with real workspace
 * numbers
 */
typedef enum
{
  META_MOTION_UP = -1,
  META_MOTION_DOWN = -2,
  META_MOTION_LEFT = -3,
  META_MOTION_RIGHT = -4,
  /* These are only used for effects */
  META_MOTION_UP_LEFT = -5,
  META_MOTION_UP_RIGHT = -6,
  META_MOTION_DOWN_LEFT = -7,
  META_MOTION_DOWN_RIGHT = -8,
  META_MOTION_NOT_EXIST_YET = -30
} MetaMotionDirection;

/* Sometimes we want to talk about sides instead of directions; note
 * that the values must be as follows or meta_window_update_struts()
 * won't work. Using these values also is a safety blanket since
 * MetaDirection used to be used as a side.
 */
typedef enum
{
  META_SIDE_LEFT            = META_DIRECTION_LEFT,
  META_SIDE_RIGHT           = META_DIRECTION_RIGHT,
  META_SIDE_TOP             = META_DIRECTION_TOP,
  META_SIDE_BOTTOM          = META_DIRECTION_BOTTOM
} MetaSide;

/* Function a window button can have.  Note, you can't add stuff here
 * without extending the theme format to draw a new function and
 * breaking all existing themes.
 */
typedef enum
{
  META_BUTTON_FUNCTION_MENU,
  META_BUTTON_FUNCTION_MINIMIZE,
  META_BUTTON_FUNCTION_MAXIMIZE,
  META_BUTTON_FUNCTION_CLOSE,
  META_BUTTON_FUNCTION_SHADE,
  META_BUTTON_FUNCTION_ABOVE,
  META_BUTTON_FUNCTION_STICK,
  META_BUTTON_FUNCTION_UNSHADE,
  META_BUTTON_FUNCTION_UNABOVE,
  META_BUTTON_FUNCTION_UNSTICK,
  META_BUTTON_FUNCTION_LAST
} MetaButtonFunction;

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

typedef enum {
    META_WINDOW_TILE_TYPE_NONE,
    META_WINDOW_TILE_TYPE_TILED,
    META_WINDOW_TILE_TYPE_SNAPPED
} MetaWindowTileType;

typedef enum {
    META_BELL_TYPE_NONE,
    META_BELL_TYPE_STICKY_KEYS,
    META_BELL_TYPE_SLOW_KEYS,
    META_BELL_TYPE_BOUNCE_KEYS
} MetaBellType;

#define MAX_BUTTONS_PER_CORNER META_BUTTON_FUNCTION_LAST

/* Keep array size in sync with MAX_BUTTONS_PER_CORNER */
/**
 * MetaButtonLayout:
 * @left_buttons: (array fixed-size=10):
 * @right_buttons: (array fixed-size=10):
 * @left_buttons_has_spacer: (array fixed-size=10):
 * @right_buttons_has_spacer: (array fixed-size=10):
 */
typedef struct _MetaButtonLayout MetaButtonLayout;
struct _MetaButtonLayout
{
  /* buttons in the group on the left side */
  MetaButtonFunction left_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean left_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];

  /* buttons in the group on the right side */
  MetaButtonFunction right_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean right_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];
};

typedef struct _MetaFrameBorders MetaFrameBorders;
struct _MetaFrameBorders
{
  /* The frame border is made up of two pieces - an inner visible portion
   * and an outer portion that is invisible but responds to events.
   */
  GtkBorder visible;
  GtkBorder invisible;

  /* For convenience, we have a "total" border which is equal to the sum
   * of the two borders above. */
  GtkBorder total;
};

/* sets all dimensions to zero */
void meta_frame_borders_clear (MetaFrameBorders *self);

#define META_DEFAULT_ICON_NAME "window"

/* Main loop priorities determine when activity in the GLib
 * will take precendence over the others. Priorities are sometimes
 * used to enforce ordering: give A a higher priority than B if
 * A must occur before B. But that poses a problem since then
 * if A occurs frequently enough, B will never occur.
 *
 * Anything we want to occur more or less immediately should
 * have a priority of G_PRIORITY_DEFAULT. When we want to
 * coelesce multiple things together, the appropriate place to
 * do it is usually META_PRIORITY_BEFORE_REDRAW.
 *
 * Note that its usually better to use meta_later_add() rather
 * than calling g_idle_add() directly; this will make sure things
 * get run when added from a clutter event handler without
 * waiting for another repaint cycle.
 *
 * If something has a priority lower than the redraw priority
 * (such as a default priority idle), then it may be arbitrarily
 * delayed. This happens if the screen is updating rapidly: we
 * are spending all our time either redrawing or waiting for a
 * vblank-synced buffer swap. (When X is improved to allow
 * clutter to do the buffer-swap asychronously, this will get
 * better.)
 */

/* G_PRIORITY_DEFAULT:
 *  events
 *  many timeouts
 */

/* GTK_PRIORITY_RESIZE:         (G_PRIORITY_HIGH_IDLE + 10) */
#define META_PRIORITY_RESIZE    (G_PRIORITY_HIGH_IDLE + 15)
/* GTK_PRIORITY_REDRAW:         (G_PRIORITY_HIGH_IDLE + 20) */

#define META_PRIORITY_BEFORE_REDRAW  (G_PRIORITY_HIGH_IDLE + 40)
/*  calc-showing idle
 *  update-icon idle
 */

/* CLUTTER_PRIORITY_REDRAW:     (G_PRIORITY_HIGH_IDLE + 50) */
#define META_PRIORITY_REDRAW    (G_PRIORITY_HIGH_IDLE + 50)

/* ==== Anything below here can be starved arbitrarily ==== */

/* G_PRIORITY_DEFAULT_IDLE:
 *  Muffin plugin unloading
 */

#define META_PRIORITY_PREFS_NOTIFY   (G_PRIORITY_DEFAULT_IDLE + 10)

/************************************************************/

#define POINT_IN_RECT(xcoord, ycoord, rect) \
 ((xcoord) >= (rect).x &&                   \
  (xcoord) <  ((rect).x + (rect).width) &&  \
  (ycoord) >= (rect).y &&                   \
  (ycoord) <  ((rect).y + (rect).height))

#define POINT_IN_RECT_POINTER(xcoord, ycoord, rect) \
 ((xcoord) >= (rect)->x &&                   \
  (xcoord) <  ((rect)->x + (rect)->width) &&  \
  (ycoord) >= (rect)->y &&                   \
  (ycoord) <  ((rect)->y + (rect)->height))

/*
 * Layers a window can be in.
 * These MUST be in the order of stacking.
 */
typedef enum
{
  META_LAYER_DESKTOP	       = 0,
  META_LAYER_BOTTOM	       = 1,
  META_LAYER_NORMAL	       = 2,
  META_LAYER_TOP	       = 4, /* Same as DOCK; see EWMH and bug 330717 */
  META_LAYER_DOCK	       = 4,
  META_LAYER_FULLSCREEN	       = 5,
  META_LAYER_FOCUSED_WINDOW    = 6,
  META_LAYER_OVERRIDE_REDIRECT = 7,
  META_LAYER_LAST	       = 8
} MetaStackLayer;


/*
 * Placement mode
 */
typedef enum
{
  META_PLACEMENT_MODE_AUTOMATIC,
  META_PLACEMENT_MODE_POINTER,
  META_PLACEMENT_MODE_MANUAL,
  META_PLACEMENT_MODE_CENTER
} MetaPlacementMode;

/*
 * Background transition
 */
typedef enum
{
  META_BACKGROUND_TRANSITION_NONE,
  META_BACKGROUND_TRANSITION_FADEIN,
  META_BACKGROUND_TRANSITION_BLEND
} MetaBackgroundTransition;

typedef enum
{
  META_SYNC_NONE = 0,
  META_SYNC_FALLBACK,
  META_SYNC_SWAP_THROTTLING,
  META_SYNC_PRESENTATION_TIME
} MetaSyncMethod;

#endif
