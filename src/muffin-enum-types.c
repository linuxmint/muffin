
/* Generated data (by glib-mkenums) */

#include "muffin-enum-types.h"

/* enumerations from "meta/boxes.h" */
#include "meta/boxes.h"

GType
meta_edge_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_EDGE_WINDOW, "META_EDGE_WINDOW", "window" },
        { META_EDGE_MONITOR, "META_EDGE_MONITOR", "monitor" },
        { META_EDGE_SCREEN, "META_EDGE_SCREEN", "screen" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaEdgeType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/common.h" */
#include "meta/common.h"

GType
meta_frame_flags_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_FRAME_ALLOWS_DELETE, "META_FRAME_ALLOWS_DELETE", "allows-delete" },
        { META_FRAME_ALLOWS_MENU, "META_FRAME_ALLOWS_MENU", "allows-menu" },
        { META_FRAME_ALLOWS_MINIMIZE, "META_FRAME_ALLOWS_MINIMIZE", "allows-minimize" },
        { META_FRAME_ALLOWS_MAXIMIZE, "META_FRAME_ALLOWS_MAXIMIZE", "allows-maximize" },
        { META_FRAME_ALLOWS_VERTICAL_RESIZE, "META_FRAME_ALLOWS_VERTICAL_RESIZE", "allows-vertical-resize" },
        { META_FRAME_ALLOWS_HORIZONTAL_RESIZE, "META_FRAME_ALLOWS_HORIZONTAL_RESIZE", "allows-horizontal-resize" },
        { META_FRAME_HAS_FOCUS, "META_FRAME_HAS_FOCUS", "has-focus" },
        { META_FRAME_SHADED, "META_FRAME_SHADED", "shaded" },
        { META_FRAME_STUCK, "META_FRAME_STUCK", "stuck" },
        { META_FRAME_MAXIMIZED, "META_FRAME_MAXIMIZED", "maximized" },
        { META_FRAME_ALLOWS_SHADE, "META_FRAME_ALLOWS_SHADE", "allows-shade" },
        { META_FRAME_ALLOWS_MOVE, "META_FRAME_ALLOWS_MOVE", "allows-move" },
        { META_FRAME_FULLSCREEN, "META_FRAME_FULLSCREEN", "fullscreen" },
        { META_FRAME_IS_FLASHING, "META_FRAME_IS_FLASHING", "is-flashing" },
        { META_FRAME_ABOVE, "META_FRAME_ABOVE", "above" },
        { META_FRAME_TILED_LEFT, "META_FRAME_TILED_LEFT", "tiled-left" },
        { META_FRAME_TILED_RIGHT, "META_FRAME_TILED_RIGHT", "tiled-right" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaFrameFlags"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_menu_op_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_MENU_OP_NONE, "META_MENU_OP_NONE", "none" },
        { META_MENU_OP_DELETE, "META_MENU_OP_DELETE", "delete" },
        { META_MENU_OP_MINIMIZE, "META_MENU_OP_MINIMIZE", "minimize" },
        { META_MENU_OP_UNMAXIMIZE, "META_MENU_OP_UNMAXIMIZE", "unmaximize" },
        { META_MENU_OP_MAXIMIZE, "META_MENU_OP_MAXIMIZE", "maximize" },
        { META_MENU_OP_UNSHADE, "META_MENU_OP_UNSHADE", "unshade" },
        { META_MENU_OP_SHADE, "META_MENU_OP_SHADE", "shade" },
        { META_MENU_OP_UNSTICK, "META_MENU_OP_UNSTICK", "unstick" },
        { META_MENU_OP_STICK, "META_MENU_OP_STICK", "stick" },
        { META_MENU_OP_WORKSPACES, "META_MENU_OP_WORKSPACES", "workspaces" },
        { META_MENU_OP_MOVE, "META_MENU_OP_MOVE", "move" },
        { META_MENU_OP_RESIZE, "META_MENU_OP_RESIZE", "resize" },
        { META_MENU_OP_ABOVE, "META_MENU_OP_ABOVE", "above" },
        { META_MENU_OP_UNABOVE, "META_MENU_OP_UNABOVE", "unabove" },
        { META_MENU_OP_MOVE_LEFT, "META_MENU_OP_MOVE_LEFT", "move-left" },
        { META_MENU_OP_MOVE_RIGHT, "META_MENU_OP_MOVE_RIGHT", "move-right" },
        { META_MENU_OP_MOVE_UP, "META_MENU_OP_MOVE_UP", "move-up" },
        { META_MENU_OP_MOVE_DOWN, "META_MENU_OP_MOVE_DOWN", "move-down" },
        { META_MENU_OP_RECOVER, "META_MENU_OP_RECOVER", "recover" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaMenuOp"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_grab_op_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_GRAB_OP_NONE, "META_GRAB_OP_NONE", "none" },
        { META_GRAB_OP_MOVING, "META_GRAB_OP_MOVING", "moving" },
        { META_GRAB_OP_RESIZING_SE, "META_GRAB_OP_RESIZING_SE", "resizing-se" },
        { META_GRAB_OP_RESIZING_S, "META_GRAB_OP_RESIZING_S", "resizing-s" },
        { META_GRAB_OP_RESIZING_SW, "META_GRAB_OP_RESIZING_SW", "resizing-sw" },
        { META_GRAB_OP_RESIZING_N, "META_GRAB_OP_RESIZING_N", "resizing-n" },
        { META_GRAB_OP_RESIZING_NE, "META_GRAB_OP_RESIZING_NE", "resizing-ne" },
        { META_GRAB_OP_RESIZING_NW, "META_GRAB_OP_RESIZING_NW", "resizing-nw" },
        { META_GRAB_OP_RESIZING_W, "META_GRAB_OP_RESIZING_W", "resizing-w" },
        { META_GRAB_OP_RESIZING_E, "META_GRAB_OP_RESIZING_E", "resizing-e" },
        { META_GRAB_OP_KEYBOARD_MOVING, "META_GRAB_OP_KEYBOARD_MOVING", "keyboard-moving" },
        { META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN, "META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN", "keyboard-resizing-unknown" },
        { META_GRAB_OP_KEYBOARD_RESIZING_S, "META_GRAB_OP_KEYBOARD_RESIZING_S", "keyboard-resizing-s" },
        { META_GRAB_OP_KEYBOARD_RESIZING_N, "META_GRAB_OP_KEYBOARD_RESIZING_N", "keyboard-resizing-n" },
        { META_GRAB_OP_KEYBOARD_RESIZING_W, "META_GRAB_OP_KEYBOARD_RESIZING_W", "keyboard-resizing-w" },
        { META_GRAB_OP_KEYBOARD_RESIZING_E, "META_GRAB_OP_KEYBOARD_RESIZING_E", "keyboard-resizing-e" },
        { META_GRAB_OP_KEYBOARD_RESIZING_SE, "META_GRAB_OP_KEYBOARD_RESIZING_SE", "keyboard-resizing-se" },
        { META_GRAB_OP_KEYBOARD_RESIZING_NE, "META_GRAB_OP_KEYBOARD_RESIZING_NE", "keyboard-resizing-ne" },
        { META_GRAB_OP_KEYBOARD_RESIZING_SW, "META_GRAB_OP_KEYBOARD_RESIZING_SW", "keyboard-resizing-sw" },
        { META_GRAB_OP_KEYBOARD_RESIZING_NW, "META_GRAB_OP_KEYBOARD_RESIZING_NW", "keyboard-resizing-nw" },
        { META_GRAB_OP_KEYBOARD_TABBING_NORMAL, "META_GRAB_OP_KEYBOARD_TABBING_NORMAL", "keyboard-tabbing-normal" },
        { META_GRAB_OP_KEYBOARD_TABBING_DOCK, "META_GRAB_OP_KEYBOARD_TABBING_DOCK", "keyboard-tabbing-dock" },
        { META_GRAB_OP_KEYBOARD_ESCAPING_NORMAL, "META_GRAB_OP_KEYBOARD_ESCAPING_NORMAL", "keyboard-escaping-normal" },
        { META_GRAB_OP_KEYBOARD_ESCAPING_DOCK, "META_GRAB_OP_KEYBOARD_ESCAPING_DOCK", "keyboard-escaping-dock" },
        { META_GRAB_OP_KEYBOARD_ESCAPING_GROUP, "META_GRAB_OP_KEYBOARD_ESCAPING_GROUP", "keyboard-escaping-group" },
        { META_GRAB_OP_KEYBOARD_TABBING_GROUP, "META_GRAB_OP_KEYBOARD_TABBING_GROUP", "keyboard-tabbing-group" },
        { META_GRAB_OP_KEYBOARD_WORKSPACE_SWITCHING, "META_GRAB_OP_KEYBOARD_WORKSPACE_SWITCHING", "keyboard-workspace-switching" },
        { META_GRAB_OP_CLICKING_MINIMIZE, "META_GRAB_OP_CLICKING_MINIMIZE", "clicking-minimize" },
        { META_GRAB_OP_CLICKING_MAXIMIZE, "META_GRAB_OP_CLICKING_MAXIMIZE", "clicking-maximize" },
        { META_GRAB_OP_CLICKING_UNMAXIMIZE, "META_GRAB_OP_CLICKING_UNMAXIMIZE", "clicking-unmaximize" },
        { META_GRAB_OP_CLICKING_DELETE, "META_GRAB_OP_CLICKING_DELETE", "clicking-delete" },
        { META_GRAB_OP_CLICKING_MENU, "META_GRAB_OP_CLICKING_MENU", "clicking-menu" },
        { META_GRAB_OP_CLICKING_SHADE, "META_GRAB_OP_CLICKING_SHADE", "clicking-shade" },
        { META_GRAB_OP_CLICKING_UNSHADE, "META_GRAB_OP_CLICKING_UNSHADE", "clicking-unshade" },
        { META_GRAB_OP_CLICKING_ABOVE, "META_GRAB_OP_CLICKING_ABOVE", "clicking-above" },
        { META_GRAB_OP_CLICKING_UNABOVE, "META_GRAB_OP_CLICKING_UNABOVE", "clicking-unabove" },
        { META_GRAB_OP_CLICKING_STICK, "META_GRAB_OP_CLICKING_STICK", "clicking-stick" },
        { META_GRAB_OP_CLICKING_UNSTICK, "META_GRAB_OP_CLICKING_UNSTICK", "clicking-unstick" },
        { META_GRAB_OP_COMPOSITOR, "META_GRAB_OP_COMPOSITOR", "compositor" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaGrabOp"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_cursor_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_CURSOR_DEFAULT, "META_CURSOR_DEFAULT", "default" },
        { META_CURSOR_NORTH_RESIZE, "META_CURSOR_NORTH_RESIZE", "north-resize" },
        { META_CURSOR_SOUTH_RESIZE, "META_CURSOR_SOUTH_RESIZE", "south-resize" },
        { META_CURSOR_WEST_RESIZE, "META_CURSOR_WEST_RESIZE", "west-resize" },
        { META_CURSOR_EAST_RESIZE, "META_CURSOR_EAST_RESIZE", "east-resize" },
        { META_CURSOR_SE_RESIZE, "META_CURSOR_SE_RESIZE", "se-resize" },
        { META_CURSOR_SW_RESIZE, "META_CURSOR_SW_RESIZE", "sw-resize" },
        { META_CURSOR_NE_RESIZE, "META_CURSOR_NE_RESIZE", "ne-resize" },
        { META_CURSOR_NW_RESIZE, "META_CURSOR_NW_RESIZE", "nw-resize" },
        { META_CURSOR_MOVE_OR_RESIZE_WINDOW, "META_CURSOR_MOVE_OR_RESIZE_WINDOW", "move-or-resize-window" },
        { META_CURSOR_BUSY, "META_CURSOR_BUSY", "busy" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaCursor"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_focus_mode_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_FOCUS_MODE_CLICK, "META_FOCUS_MODE_CLICK", "click" },
        { META_FOCUS_MODE_SLOPPY, "META_FOCUS_MODE_SLOPPY", "sloppy" },
        { META_FOCUS_MODE_MOUSE, "META_FOCUS_MODE_MOUSE", "mouse" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaFocusMode"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_focus_new_windows_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_FOCUS_NEW_WINDOWS_SMART, "META_FOCUS_NEW_WINDOWS_SMART", "smart" },
        { META_FOCUS_NEW_WINDOWS_STRICT, "META_FOCUS_NEW_WINDOWS_STRICT", "strict" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaFocusNewWindows"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_action_titlebar_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_ACTION_TITLEBAR_TOGGLE_SHADE, "META_ACTION_TITLEBAR_TOGGLE_SHADE", "toggle-shade" },
        { META_ACTION_TITLEBAR_TOGGLE_MAXIMIZE, "META_ACTION_TITLEBAR_TOGGLE_MAXIMIZE", "toggle-maximize" },
        { META_ACTION_TITLEBAR_TOGGLE_MAXIMIZE_HORIZONTALLY, "META_ACTION_TITLEBAR_TOGGLE_MAXIMIZE_HORIZONTALLY", "toggle-maximize-horizontally" },
        { META_ACTION_TITLEBAR_TOGGLE_MAXIMIZE_VERTICALLY, "META_ACTION_TITLEBAR_TOGGLE_MAXIMIZE_VERTICALLY", "toggle-maximize-vertically" },
        { META_ACTION_TITLEBAR_MINIMIZE, "META_ACTION_TITLEBAR_MINIMIZE", "minimize" },
        { META_ACTION_TITLEBAR_NONE, "META_ACTION_TITLEBAR_NONE", "none" },
        { META_ACTION_TITLEBAR_LOWER, "META_ACTION_TITLEBAR_LOWER", "lower" },
        { META_ACTION_TITLEBAR_MENU, "META_ACTION_TITLEBAR_MENU", "menu" },
        { META_ACTION_TITLEBAR_LAST, "META_ACTION_TITLEBAR_LAST", "last" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaActionTitlebar"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_frame_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_FRAME_TYPE_NORMAL, "META_FRAME_TYPE_NORMAL", "normal" },
        { META_FRAME_TYPE_DIALOG, "META_FRAME_TYPE_DIALOG", "dialog" },
        { META_FRAME_TYPE_MODAL_DIALOG, "META_FRAME_TYPE_MODAL_DIALOG", "modal-dialog" },
        { META_FRAME_TYPE_UTILITY, "META_FRAME_TYPE_UTILITY", "utility" },
        { META_FRAME_TYPE_MENU, "META_FRAME_TYPE_MENU", "menu" },
        { META_FRAME_TYPE_BORDER, "META_FRAME_TYPE_BORDER", "border" },
        { META_FRAME_TYPE_ATTACHED, "META_FRAME_TYPE_ATTACHED", "attached" },
        { META_FRAME_TYPE_LAST, "META_FRAME_TYPE_LAST", "last" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaFrameType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_virtual_modifier_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_VIRTUAL_SHIFT_MASK, "META_VIRTUAL_SHIFT_MASK", "shift-mask" },
        { META_VIRTUAL_CONTROL_MASK, "META_VIRTUAL_CONTROL_MASK", "control-mask" },
        { META_VIRTUAL_ALT_MASK, "META_VIRTUAL_ALT_MASK", "alt-mask" },
        { META_VIRTUAL_META_MASK, "META_VIRTUAL_META_MASK", "meta-mask" },
        { META_VIRTUAL_SUPER_MASK, "META_VIRTUAL_SUPER_MASK", "super-mask" },
        { META_VIRTUAL_HYPER_MASK, "META_VIRTUAL_HYPER_MASK", "hyper-mask" },
        { META_VIRTUAL_MOD2_MASK, "META_VIRTUAL_MOD2_MASK", "mod2-mask" },
        { META_VIRTUAL_MOD3_MASK, "META_VIRTUAL_MOD3_MASK", "mod3-mask" },
        { META_VIRTUAL_MOD4_MASK, "META_VIRTUAL_MOD4_MASK", "mod4-mask" },
        { META_VIRTUAL_MOD5_MASK, "META_VIRTUAL_MOD5_MASK", "mod5-mask" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaVirtualModifier"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_direction_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_DIRECTION_LEFT, "META_DIRECTION_LEFT", "left" },
        { META_DIRECTION_RIGHT, "META_DIRECTION_RIGHT", "right" },
        { META_DIRECTION_TOP, "META_DIRECTION_TOP", "top" },
        { META_DIRECTION_BOTTOM, "META_DIRECTION_BOTTOM", "bottom" },
        { META_DIRECTION_UP, "META_DIRECTION_UP", "up" },
        { META_DIRECTION_DOWN, "META_DIRECTION_DOWN", "down" },
        { META_DIRECTION_HORIZONTAL, "META_DIRECTION_HORIZONTAL", "horizontal" },
        { META_DIRECTION_VERTICAL, "META_DIRECTION_VERTICAL", "vertical" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaDirection"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_motion_direction_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_MOTION_UP, "META_MOTION_UP", "up" },
        { META_MOTION_DOWN, "META_MOTION_DOWN", "down" },
        { META_MOTION_LEFT, "META_MOTION_LEFT", "left" },
        { META_MOTION_RIGHT, "META_MOTION_RIGHT", "right" },
        { META_MOTION_UP_LEFT, "META_MOTION_UP_LEFT", "up-left" },
        { META_MOTION_UP_RIGHT, "META_MOTION_UP_RIGHT", "up-right" },
        { META_MOTION_DOWN_LEFT, "META_MOTION_DOWN_LEFT", "down-left" },
        { META_MOTION_DOWN_RIGHT, "META_MOTION_DOWN_RIGHT", "down-right" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaMotionDirection"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_side_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_SIDE_LEFT, "META_SIDE_LEFT", "left" },
        { META_SIDE_RIGHT, "META_SIDE_RIGHT", "right" },
        { META_SIDE_TOP, "META_SIDE_TOP", "top" },
        { META_SIDE_BOTTOM, "META_SIDE_BOTTOM", "bottom" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaSide"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_button_function_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_BUTTON_FUNCTION_MENU, "META_BUTTON_FUNCTION_MENU", "menu" },
        { META_BUTTON_FUNCTION_MINIMIZE, "META_BUTTON_FUNCTION_MINIMIZE", "minimize" },
        { META_BUTTON_FUNCTION_MAXIMIZE, "META_BUTTON_FUNCTION_MAXIMIZE", "maximize" },
        { META_BUTTON_FUNCTION_CLOSE, "META_BUTTON_FUNCTION_CLOSE", "close" },
        { META_BUTTON_FUNCTION_SHADE, "META_BUTTON_FUNCTION_SHADE", "shade" },
        { META_BUTTON_FUNCTION_ABOVE, "META_BUTTON_FUNCTION_ABOVE", "above" },
        { META_BUTTON_FUNCTION_STICK, "META_BUTTON_FUNCTION_STICK", "stick" },
        { META_BUTTON_FUNCTION_UNSHADE, "META_BUTTON_FUNCTION_UNSHADE", "unshade" },
        { META_BUTTON_FUNCTION_UNABOVE, "META_BUTTON_FUNCTION_UNABOVE", "unabove" },
        { META_BUTTON_FUNCTION_UNSTICK, "META_BUTTON_FUNCTION_UNSTICK", "unstick" },
        { META_BUTTON_FUNCTION_LAST, "META_BUTTON_FUNCTION_LAST", "last" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaButtonFunction"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_stack_layer_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_LAYER_DESKTOP, "META_LAYER_DESKTOP", "desktop" },
        { META_LAYER_BOTTOM, "META_LAYER_BOTTOM", "bottom" },
        { META_LAYER_NORMAL, "META_LAYER_NORMAL", "normal" },
        { META_LAYER_TOP, "META_LAYER_TOP", "top" },
        { META_LAYER_DOCK, "META_LAYER_DOCK", "dock" },
        { META_LAYER_FULLSCREEN, "META_LAYER_FULLSCREEN", "fullscreen" },
        { META_LAYER_FOCUSED_WINDOW, "META_LAYER_FOCUSED_WINDOW", "focused-window" },
        { META_LAYER_OVERRIDE_REDIRECT, "META_LAYER_OVERRIDE_REDIRECT", "override-redirect" },
        { META_LAYER_LAST, "META_LAYER_LAST", "last" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaStackLayer"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/compositor.h" */
#include "meta/compositor.h"

GType
meta_comp_effect_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_COMP_EFFECT_CREATE, "META_COMP_EFFECT_CREATE", "create" },
        { META_COMP_EFFECT_UNMINIMIZE, "META_COMP_EFFECT_UNMINIMIZE", "unminimize" },
        { META_COMP_EFFECT_DESTROY, "META_COMP_EFFECT_DESTROY", "destroy" },
        { META_COMP_EFFECT_MINIMIZE, "META_COMP_EFFECT_MINIMIZE", "minimize" },
        { META_COMP_EFFECT_NONE, "META_COMP_EFFECT_NONE", "none" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaCompEffect"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/display.h" */
#include "meta/display.h"

GType
meta_tab_list_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_TAB_LIST_NORMAL, "META_TAB_LIST_NORMAL", "normal" },
        { META_TAB_LIST_DOCKS, "META_TAB_LIST_DOCKS", "docks" },
        { META_TAB_LIST_GROUP, "META_TAB_LIST_GROUP", "group" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaTabList"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_tab_show_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_TAB_SHOW_ICON, "META_TAB_SHOW_ICON", "icon" },
        { META_TAB_SHOW_INSTANTLY, "META_TAB_SHOW_INSTANTLY", "instantly" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaTabShowType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_atom_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_ATOM_FIRST, "META_ATOM_FIRST", "first" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaAtom"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/gradient.h" */
#include "meta/gradient.h"

GType
meta_gradient_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_GRADIENT_VERTICAL, "META_GRADIENT_VERTICAL", "vertical" },
        { META_GRADIENT_HORIZONTAL, "META_GRADIENT_HORIZONTAL", "horizontal" },
        { META_GRADIENT_DIAGONAL, "META_GRADIENT_DIAGONAL", "diagonal" },
        { META_GRADIENT_LAST, "META_GRADIENT_LAST", "last" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaGradientType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/main.h" */
#include "meta/main.h"

GType
meta_exit_code_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_EXIT_SUCCESS, "META_EXIT_SUCCESS", "success" },
        { META_EXIT_ERROR, "META_EXIT_ERROR", "error" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaExitCode"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/meta-plugin.h" */
#include "meta/meta-plugin.h"

GType
meta_modal_options_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_MODAL_POINTER_ALREADY_GRABBED, "META_MODAL_POINTER_ALREADY_GRABBED", "pointer-already-grabbed" },
        { META_MODAL_KEYBOARD_ALREADY_GRABBED, "META_MODAL_KEYBOARD_ALREADY_GRABBED", "keyboard-already-grabbed" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaModalOptions"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/prefs.h" */
#include "meta/prefs.h"

GType
meta_preference_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_PREF_MOUSE_BUTTON_MODS, "META_PREF_MOUSE_BUTTON_MODS", "mouse-button-mods" },
        { META_PREF_FOCUS_MODE, "META_PREF_FOCUS_MODE", "focus-mode" },
        { META_PREF_FOCUS_NEW_WINDOWS, "META_PREF_FOCUS_NEW_WINDOWS", "focus-new-windows" },
        { META_PREF_ATTACH_MODAL_DIALOGS, "META_PREF_ATTACH_MODAL_DIALOGS", "attach-modal-dialogs" },
        { META_PREF_RAISE_ON_CLICK, "META_PREF_RAISE_ON_CLICK", "raise-on-click" },
        { META_PREF_ACTION_DOUBLE_CLICK_TITLEBAR, "META_PREF_ACTION_DOUBLE_CLICK_TITLEBAR", "action-double-click-titlebar" },
        { META_PREF_ACTION_MIDDLE_CLICK_TITLEBAR, "META_PREF_ACTION_MIDDLE_CLICK_TITLEBAR", "action-middle-click-titlebar" },
        { META_PREF_ACTION_RIGHT_CLICK_TITLEBAR, "META_PREF_ACTION_RIGHT_CLICK_TITLEBAR", "action-right-click-titlebar" },
        { META_PREF_AUTO_RAISE, "META_PREF_AUTO_RAISE", "auto-raise" },
        { META_PREF_AUTO_RAISE_DELAY, "META_PREF_AUTO_RAISE_DELAY", "auto-raise-delay" },
        { META_PREF_THEME, "META_PREF_THEME", "theme" },
        { META_PREF_TITLEBAR_FONT, "META_PREF_TITLEBAR_FONT", "titlebar-font" },
        { META_PREF_NUM_WORKSPACES, "META_PREF_NUM_WORKSPACES", "num-workspaces" },
        { META_PREF_APPLICATION_BASED, "META_PREF_APPLICATION_BASED", "application-based" },
        { META_PREF_KEYBINDINGS, "META_PREF_KEYBINDINGS", "keybindings" },
        { META_PREF_DISABLE_WORKAROUNDS, "META_PREF_DISABLE_WORKAROUNDS", "disable-workarounds" },
        { META_PREF_COMMANDS, "META_PREF_COMMANDS", "commands" },
        { META_PREF_TERMINAL_COMMAND, "META_PREF_TERMINAL_COMMAND", "terminal-command" },
        { META_PREF_BUTTON_LAYOUT, "META_PREF_BUTTON_LAYOUT", "button-layout" },
        { META_PREF_WORKSPACE_NAMES, "META_PREF_WORKSPACE_NAMES", "workspace-names" },
        { META_PREF_VISUAL_BELL, "META_PREF_VISUAL_BELL", "visual-bell" },
        { META_PREF_AUDIBLE_BELL, "META_PREF_AUDIBLE_BELL", "audible-bell" },
        { META_PREF_VISUAL_BELL_TYPE, "META_PREF_VISUAL_BELL_TYPE", "visual-bell-type" },
        { META_PREF_GNOME_ACCESSIBILITY, "META_PREF_GNOME_ACCESSIBILITY", "gnome-accessibility" },
        { META_PREF_GNOME_ANIMATIONS, "META_PREF_GNOME_ANIMATIONS", "gnome-animations" },
        { META_PREF_CURSOR_THEME, "META_PREF_CURSOR_THEME", "cursor-theme" },
        { META_PREF_CURSOR_SIZE, "META_PREF_CURSOR_SIZE", "cursor-size" },
        { META_PREF_RESIZE_WITH_RIGHT_BUTTON, "META_PREF_RESIZE_WITH_RIGHT_BUTTON", "resize-with-right-button" },
        { META_PREF_EDGE_TILING, "META_PREF_EDGE_TILING", "edge-tiling" },
        { META_PREF_FORCE_FULLSCREEN, "META_PREF_FORCE_FULLSCREEN", "force-fullscreen" },
        { META_PREF_LIVE_HIDDEN_WINDOWS, "META_PREF_LIVE_HIDDEN_WINDOWS", "live-hidden-windows" },
        { META_PREF_WORKSPACES_ONLY_ON_PRIMARY, "META_PREF_WORKSPACES_ONLY_ON_PRIMARY", "workspaces-only-on-primary" },
        { META_PREF_NO_TAB_POPUP, "META_PREF_NO_TAB_POPUP", "no-tab-popup" },
        { META_PREF_DRAGGABLE_BORDER_WIDTH, "META_PREF_DRAGGABLE_BORDER_WIDTH", "draggable-border-width" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaPreference"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_key_binding_action_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_KEYBINDING_ACTION_NONE, "META_KEYBINDING_ACTION_NONE", "none" },
        { META_KEYBINDING_ACTION_WORKSPACE_1, "META_KEYBINDING_ACTION_WORKSPACE_1", "workspace-1" },
        { META_KEYBINDING_ACTION_WORKSPACE_2, "META_KEYBINDING_ACTION_WORKSPACE_2", "workspace-2" },
        { META_KEYBINDING_ACTION_WORKSPACE_3, "META_KEYBINDING_ACTION_WORKSPACE_3", "workspace-3" },
        { META_KEYBINDING_ACTION_WORKSPACE_4, "META_KEYBINDING_ACTION_WORKSPACE_4", "workspace-4" },
        { META_KEYBINDING_ACTION_WORKSPACE_5, "META_KEYBINDING_ACTION_WORKSPACE_5", "workspace-5" },
        { META_KEYBINDING_ACTION_WORKSPACE_6, "META_KEYBINDING_ACTION_WORKSPACE_6", "workspace-6" },
        { META_KEYBINDING_ACTION_WORKSPACE_7, "META_KEYBINDING_ACTION_WORKSPACE_7", "workspace-7" },
        { META_KEYBINDING_ACTION_WORKSPACE_8, "META_KEYBINDING_ACTION_WORKSPACE_8", "workspace-8" },
        { META_KEYBINDING_ACTION_WORKSPACE_9, "META_KEYBINDING_ACTION_WORKSPACE_9", "workspace-9" },
        { META_KEYBINDING_ACTION_WORKSPACE_10, "META_KEYBINDING_ACTION_WORKSPACE_10", "workspace-10" },
        { META_KEYBINDING_ACTION_WORKSPACE_11, "META_KEYBINDING_ACTION_WORKSPACE_11", "workspace-11" },
        { META_KEYBINDING_ACTION_WORKSPACE_12, "META_KEYBINDING_ACTION_WORKSPACE_12", "workspace-12" },
        { META_KEYBINDING_ACTION_WORKSPACE_LEFT, "META_KEYBINDING_ACTION_WORKSPACE_LEFT", "workspace-left" },
        { META_KEYBINDING_ACTION_WORKSPACE_RIGHT, "META_KEYBINDING_ACTION_WORKSPACE_RIGHT", "workspace-right" },
        { META_KEYBINDING_ACTION_WORKSPACE_UP, "META_KEYBINDING_ACTION_WORKSPACE_UP", "workspace-up" },
        { META_KEYBINDING_ACTION_WORKSPACE_DOWN, "META_KEYBINDING_ACTION_WORKSPACE_DOWN", "workspace-down" },
        { META_KEYBINDING_ACTION_SWITCH_GROUP, "META_KEYBINDING_ACTION_SWITCH_GROUP", "switch-group" },
        { META_KEYBINDING_ACTION_SWITCH_GROUP_BACKWARD, "META_KEYBINDING_ACTION_SWITCH_GROUP_BACKWARD", "switch-group-backward" },
        { META_KEYBINDING_ACTION_SWITCH_WINDOWS, "META_KEYBINDING_ACTION_SWITCH_WINDOWS", "switch-windows" },
        { META_KEYBINDING_ACTION_SWITCH_WINDOWS_BACKWARD, "META_KEYBINDING_ACTION_SWITCH_WINDOWS_BACKWARD", "switch-windows-backward" },
        { META_KEYBINDING_ACTION_SWITCH_PANELS, "META_KEYBINDING_ACTION_SWITCH_PANELS", "switch-panels" },
        { META_KEYBINDING_ACTION_SWITCH_PANELS_BACKWARD, "META_KEYBINDING_ACTION_SWITCH_PANELS_BACKWARD", "switch-panels-backward" },
        { META_KEYBINDING_ACTION_CYCLE_GROUP, "META_KEYBINDING_ACTION_CYCLE_GROUP", "cycle-group" },
        { META_KEYBINDING_ACTION_CYCLE_GROUP_BACKWARD, "META_KEYBINDING_ACTION_CYCLE_GROUP_BACKWARD", "cycle-group-backward" },
        { META_KEYBINDING_ACTION_CYCLE_WINDOWS, "META_KEYBINDING_ACTION_CYCLE_WINDOWS", "cycle-windows" },
        { META_KEYBINDING_ACTION_CYCLE_WINDOWS_BACKWARD, "META_KEYBINDING_ACTION_CYCLE_WINDOWS_BACKWARD", "cycle-windows-backward" },
        { META_KEYBINDING_ACTION_CYCLE_PANELS, "META_KEYBINDING_ACTION_CYCLE_PANELS", "cycle-panels" },
        { META_KEYBINDING_ACTION_CYCLE_PANELS_BACKWARD, "META_KEYBINDING_ACTION_CYCLE_PANELS_BACKWARD", "cycle-panels-backward" },
        { META_KEYBINDING_ACTION_TAB_POPUP_SELECT, "META_KEYBINDING_ACTION_TAB_POPUP_SELECT", "tab-popup-select" },
        { META_KEYBINDING_ACTION_TAB_POPUP_CANCEL, "META_KEYBINDING_ACTION_TAB_POPUP_CANCEL", "tab-popup-cancel" },
        { META_KEYBINDING_ACTION_SHOW_DESKTOP, "META_KEYBINDING_ACTION_SHOW_DESKTOP", "show-desktop" },
        { META_KEYBINDING_ACTION_PANEL_MAIN_MENU, "META_KEYBINDING_ACTION_PANEL_MAIN_MENU", "panel-main-menu" },
        { META_KEYBINDING_ACTION_PANEL_RUN_DIALOG, "META_KEYBINDING_ACTION_PANEL_RUN_DIALOG", "panel-run-dialog" },
        { META_KEYBINDING_ACTION_TOGGLE_RECORDING, "META_KEYBINDING_ACTION_TOGGLE_RECORDING", "toggle-recording" },
        { META_KEYBINDING_ACTION_COMMAND_1, "META_KEYBINDING_ACTION_COMMAND_1", "command-1" },
        { META_KEYBINDING_ACTION_COMMAND_2, "META_KEYBINDING_ACTION_COMMAND_2", "command-2" },
        { META_KEYBINDING_ACTION_COMMAND_3, "META_KEYBINDING_ACTION_COMMAND_3", "command-3" },
        { META_KEYBINDING_ACTION_COMMAND_4, "META_KEYBINDING_ACTION_COMMAND_4", "command-4" },
        { META_KEYBINDING_ACTION_COMMAND_5, "META_KEYBINDING_ACTION_COMMAND_5", "command-5" },
        { META_KEYBINDING_ACTION_COMMAND_6, "META_KEYBINDING_ACTION_COMMAND_6", "command-6" },
        { META_KEYBINDING_ACTION_COMMAND_7, "META_KEYBINDING_ACTION_COMMAND_7", "command-7" },
        { META_KEYBINDING_ACTION_COMMAND_8, "META_KEYBINDING_ACTION_COMMAND_8", "command-8" },
        { META_KEYBINDING_ACTION_COMMAND_9, "META_KEYBINDING_ACTION_COMMAND_9", "command-9" },
        { META_KEYBINDING_ACTION_COMMAND_10, "META_KEYBINDING_ACTION_COMMAND_10", "command-10" },
        { META_KEYBINDING_ACTION_COMMAND_11, "META_KEYBINDING_ACTION_COMMAND_11", "command-11" },
        { META_KEYBINDING_ACTION_COMMAND_12, "META_KEYBINDING_ACTION_COMMAND_12", "command-12" },
        { META_KEYBINDING_ACTION_COMMAND_13, "META_KEYBINDING_ACTION_COMMAND_13", "command-13" },
        { META_KEYBINDING_ACTION_COMMAND_14, "META_KEYBINDING_ACTION_COMMAND_14", "command-14" },
        { META_KEYBINDING_ACTION_COMMAND_15, "META_KEYBINDING_ACTION_COMMAND_15", "command-15" },
        { META_KEYBINDING_ACTION_COMMAND_16, "META_KEYBINDING_ACTION_COMMAND_16", "command-16" },
        { META_KEYBINDING_ACTION_COMMAND_17, "META_KEYBINDING_ACTION_COMMAND_17", "command-17" },
        { META_KEYBINDING_ACTION_COMMAND_18, "META_KEYBINDING_ACTION_COMMAND_18", "command-18" },
        { META_KEYBINDING_ACTION_COMMAND_19, "META_KEYBINDING_ACTION_COMMAND_19", "command-19" },
        { META_KEYBINDING_ACTION_COMMAND_20, "META_KEYBINDING_ACTION_COMMAND_20", "command-20" },
        { META_KEYBINDING_ACTION_COMMAND_21, "META_KEYBINDING_ACTION_COMMAND_21", "command-21" },
        { META_KEYBINDING_ACTION_COMMAND_22, "META_KEYBINDING_ACTION_COMMAND_22", "command-22" },
        { META_KEYBINDING_ACTION_COMMAND_23, "META_KEYBINDING_ACTION_COMMAND_23", "command-23" },
        { META_KEYBINDING_ACTION_COMMAND_24, "META_KEYBINDING_ACTION_COMMAND_24", "command-24" },
        { META_KEYBINDING_ACTION_COMMAND_25, "META_KEYBINDING_ACTION_COMMAND_25", "command-25" },
        { META_KEYBINDING_ACTION_COMMAND_26, "META_KEYBINDING_ACTION_COMMAND_26", "command-26" },
        { META_KEYBINDING_ACTION_COMMAND_27, "META_KEYBINDING_ACTION_COMMAND_27", "command-27" },
        { META_KEYBINDING_ACTION_COMMAND_28, "META_KEYBINDING_ACTION_COMMAND_28", "command-28" },
        { META_KEYBINDING_ACTION_COMMAND_29, "META_KEYBINDING_ACTION_COMMAND_29", "command-29" },
        { META_KEYBINDING_ACTION_COMMAND_30, "META_KEYBINDING_ACTION_COMMAND_30", "command-30" },
        { META_KEYBINDING_ACTION_COMMAND_31, "META_KEYBINDING_ACTION_COMMAND_31", "command-31" },
        { META_KEYBINDING_ACTION_COMMAND_32, "META_KEYBINDING_ACTION_COMMAND_32", "command-32" },
        { META_KEYBINDING_ACTION_COMMAND_SCREENSHOT, "META_KEYBINDING_ACTION_COMMAND_SCREENSHOT", "command-screenshot" },
        { META_KEYBINDING_ACTION_COMMAND_WINDOW_SCREENSHOT, "META_KEYBINDING_ACTION_COMMAND_WINDOW_SCREENSHOT", "command-window-screenshot" },
        { META_KEYBINDING_ACTION_COMMAND_TERMINAL, "META_KEYBINDING_ACTION_COMMAND_TERMINAL", "command-terminal" },
        { META_KEYBINDING_ACTION_SET_SPEW_MARK, "META_KEYBINDING_ACTION_SET_SPEW_MARK", "set-spew-mark" },
        { META_KEYBINDING_ACTION_ACTIVATE_WINDOW_MENU, "META_KEYBINDING_ACTION_ACTIVATE_WINDOW_MENU", "activate-window-menu" },
        { META_KEYBINDING_ACTION_TOGGLE_FULLSCREEN, "META_KEYBINDING_ACTION_TOGGLE_FULLSCREEN", "toggle-fullscreen" },
        { META_KEYBINDING_ACTION_TOGGLE_MAXIMIZED, "META_KEYBINDING_ACTION_TOGGLE_MAXIMIZED", "toggle-maximized" },
        { META_KEYBINDING_ACTION_TOGGLE_ABOVE, "META_KEYBINDING_ACTION_TOGGLE_ABOVE", "toggle-above" },
        { META_KEYBINDING_ACTION_MAXIMIZE, "META_KEYBINDING_ACTION_MAXIMIZE", "maximize" },
        { META_KEYBINDING_ACTION_UNMAXIMIZE, "META_KEYBINDING_ACTION_UNMAXIMIZE", "unmaximize" },
        { META_KEYBINDING_ACTION_TOGGLE_SHADED, "META_KEYBINDING_ACTION_TOGGLE_SHADED", "toggle-shaded" },
        { META_KEYBINDING_ACTION_MINIMIZE, "META_KEYBINDING_ACTION_MINIMIZE", "minimize" },
        { META_KEYBINDING_ACTION_CLOSE, "META_KEYBINDING_ACTION_CLOSE", "close" },
        { META_KEYBINDING_ACTION_BEGIN_MOVE, "META_KEYBINDING_ACTION_BEGIN_MOVE", "begin-move" },
        { META_KEYBINDING_ACTION_BEGIN_RESIZE, "META_KEYBINDING_ACTION_BEGIN_RESIZE", "begin-resize" },
        { META_KEYBINDING_ACTION_TOGGLE_ON_ALL_WORKSPACES, "META_KEYBINDING_ACTION_TOGGLE_ON_ALL_WORKSPACES", "toggle-on-all-workspaces" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_1, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_1", "move-to-workspace-1" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_2, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_2", "move-to-workspace-2" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_3, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_3", "move-to-workspace-3" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_4, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_4", "move-to-workspace-4" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_5, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_5", "move-to-workspace-5" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_6, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_6", "move-to-workspace-6" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_7, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_7", "move-to-workspace-7" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_8, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_8", "move-to-workspace-8" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_9, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_9", "move-to-workspace-9" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_10, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_10", "move-to-workspace-10" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_11, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_11", "move-to-workspace-11" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_12, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_12", "move-to-workspace-12" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_LEFT, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_LEFT", "move-to-workspace-left" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_RIGHT, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_RIGHT", "move-to-workspace-right" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_UP, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_UP", "move-to-workspace-up" },
        { META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_DOWN, "META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_DOWN", "move-to-workspace-down" },
        { META_KEYBINDING_ACTION_RAISE_OR_LOWER, "META_KEYBINDING_ACTION_RAISE_OR_LOWER", "raise-or-lower" },
        { META_KEYBINDING_ACTION_RAISE, "META_KEYBINDING_ACTION_RAISE", "raise" },
        { META_KEYBINDING_ACTION_LOWER, "META_KEYBINDING_ACTION_LOWER", "lower" },
        { META_KEYBINDING_ACTION_MAXIMIZE_VERTICALLY, "META_KEYBINDING_ACTION_MAXIMIZE_VERTICALLY", "maximize-vertically" },
        { META_KEYBINDING_ACTION_MAXIMIZE_HORIZONTALLY, "META_KEYBINDING_ACTION_MAXIMIZE_HORIZONTALLY", "maximize-horizontally" },
        { META_KEYBINDING_ACTION_MOVE_TO_CORNER_NW, "META_KEYBINDING_ACTION_MOVE_TO_CORNER_NW", "move-to-corner-nw" },
        { META_KEYBINDING_ACTION_MOVE_TO_CORNER_NE, "META_KEYBINDING_ACTION_MOVE_TO_CORNER_NE", "move-to-corner-ne" },
        { META_KEYBINDING_ACTION_MOVE_TO_CORNER_SW, "META_KEYBINDING_ACTION_MOVE_TO_CORNER_SW", "move-to-corner-sw" },
        { META_KEYBINDING_ACTION_MOVE_TO_CORNER_SE, "META_KEYBINDING_ACTION_MOVE_TO_CORNER_SE", "move-to-corner-se" },
        { META_KEYBINDING_ACTION_MOVE_TO_SIDE_N, "META_KEYBINDING_ACTION_MOVE_TO_SIDE_N", "move-to-side-n" },
        { META_KEYBINDING_ACTION_MOVE_TO_SIDE_S, "META_KEYBINDING_ACTION_MOVE_TO_SIDE_S", "move-to-side-s" },
        { META_KEYBINDING_ACTION_MOVE_TO_SIDE_E, "META_KEYBINDING_ACTION_MOVE_TO_SIDE_E", "move-to-side-e" },
        { META_KEYBINDING_ACTION_MOVE_TO_SIDE_W, "META_KEYBINDING_ACTION_MOVE_TO_SIDE_W", "move-to-side-w" },
        { META_KEYBINDING_ACTION_MOVE_TO_CENTER, "META_KEYBINDING_ACTION_MOVE_TO_CENTER", "move-to-center" },
        { META_KEYBINDING_ACTION_LAST, "META_KEYBINDING_ACTION_LAST", "last" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaKeyBindingAction"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_visual_bell_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_VISUAL_BELL_INVALID, "META_VISUAL_BELL_INVALID", "invalid" },
        { META_VISUAL_BELL_FULLSCREEN_FLASH, "META_VISUAL_BELL_FULLSCREEN_FLASH", "fullscreen-flash" },
        { META_VISUAL_BELL_FRAME_FLASH, "META_VISUAL_BELL_FRAME_FLASH", "frame-flash" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaVisualBellType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/screen.h" */
#include "meta/screen.h"

GType
meta_screen_corner_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_SCREEN_TOPLEFT, "META_SCREEN_TOPLEFT", "topleft" },
        { META_SCREEN_TOPRIGHT, "META_SCREEN_TOPRIGHT", "topright" },
        { META_SCREEN_BOTTOMLEFT, "META_SCREEN_BOTTOMLEFT", "bottomleft" },
        { META_SCREEN_BOTTOMRIGHT, "META_SCREEN_BOTTOMRIGHT", "bottomright" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaScreenCorner"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/util.h" */
#include "meta/util.h"

GType
meta_debug_topic_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_DEBUG_VERBOSE, "META_DEBUG_VERBOSE", "verbose" },
        { META_DEBUG_FOCUS, "META_DEBUG_FOCUS", "focus" },
        { META_DEBUG_WORKAREA, "META_DEBUG_WORKAREA", "workarea" },
        { META_DEBUG_STACK, "META_DEBUG_STACK", "stack" },
        { META_DEBUG_THEMES, "META_DEBUG_THEMES", "themes" },
        { META_DEBUG_SM, "META_DEBUG_SM", "sm" },
        { META_DEBUG_EVENTS, "META_DEBUG_EVENTS", "events" },
        { META_DEBUG_WINDOW_STATE, "META_DEBUG_WINDOW_STATE", "window-state" },
        { META_DEBUG_WINDOW_OPS, "META_DEBUG_WINDOW_OPS", "window-ops" },
        { META_DEBUG_GEOMETRY, "META_DEBUG_GEOMETRY", "geometry" },
        { META_DEBUG_PLACEMENT, "META_DEBUG_PLACEMENT", "placement" },
        { META_DEBUG_PING, "META_DEBUG_PING", "ping" },
        { META_DEBUG_XINERAMA, "META_DEBUG_XINERAMA", "xinerama" },
        { META_DEBUG_KEYBINDINGS, "META_DEBUG_KEYBINDINGS", "keybindings" },
        { META_DEBUG_SYNC, "META_DEBUG_SYNC", "sync" },
        { META_DEBUG_ERRORS, "META_DEBUG_ERRORS", "errors" },
        { META_DEBUG_STARTUP, "META_DEBUG_STARTUP", "startup" },
        { META_DEBUG_PREFS, "META_DEBUG_PREFS", "prefs" },
        { META_DEBUG_GROUPS, "META_DEBUG_GROUPS", "groups" },
        { META_DEBUG_RESIZING, "META_DEBUG_RESIZING", "resizing" },
        { META_DEBUG_SHAPES, "META_DEBUG_SHAPES", "shapes" },
        { META_DEBUG_COMPOSITOR, "META_DEBUG_COMPOSITOR", "compositor" },
        { META_DEBUG_EDGE_RESISTANCE, "META_DEBUG_EDGE_RESISTANCE", "edge-resistance" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaDebugTopic"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_later_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_LATER_RESIZE, "META_LATER_RESIZE", "resize" },
        { META_LATER_BEFORE_REDRAW, "META_LATER_BEFORE_REDRAW", "before-redraw" },
        { META_LATER_IDLE, "META_LATER_IDLE", "idle" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaLaterType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* enumerations from "meta/window.h" */
#include "meta/window.h"

GType
meta_window_type_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GEnumValue values[] = {
        { META_WINDOW_NORMAL, "META_WINDOW_NORMAL", "normal" },
        { META_WINDOW_DESKTOP, "META_WINDOW_DESKTOP", "desktop" },
        { META_WINDOW_DOCK, "META_WINDOW_DOCK", "dock" },
        { META_WINDOW_DIALOG, "META_WINDOW_DIALOG", "dialog" },
        { META_WINDOW_MODAL_DIALOG, "META_WINDOW_MODAL_DIALOG", "modal-dialog" },
        { META_WINDOW_TOOLBAR, "META_WINDOW_TOOLBAR", "toolbar" },
        { META_WINDOW_MENU, "META_WINDOW_MENU", "menu" },
        { META_WINDOW_UTILITY, "META_WINDOW_UTILITY", "utility" },
        { META_WINDOW_SPLASHSCREEN, "META_WINDOW_SPLASHSCREEN", "splashscreen" },
        { META_WINDOW_DROPDOWN_MENU, "META_WINDOW_DROPDOWN_MENU", "dropdown-menu" },
        { META_WINDOW_POPUP_MENU, "META_WINDOW_POPUP_MENU", "popup-menu" },
        { META_WINDOW_TOOLTIP, "META_WINDOW_TOOLTIP", "tooltip" },
        { META_WINDOW_NOTIFICATION, "META_WINDOW_NOTIFICATION", "notification" },
        { META_WINDOW_COMBO, "META_WINDOW_COMBO", "combo" },
        { META_WINDOW_DND, "META_WINDOW_DND", "dnd" },
        { META_WINDOW_OVERRIDE_OTHER, "META_WINDOW_OVERRIDE_OTHER", "override-other" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_enum_register_static (g_intern_static_string ("MetaWindowType"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}
GType
meta_maximize_flags_get_type (void)
{
  static volatile gsize g_enum_type_id__volatile = 0;

  if (g_once_init_enter (&g_enum_type_id__volatile))
    {
      static const GFlagsValue values[] = {
        { META_MAXIMIZE_HORIZONTAL, "META_MAXIMIZE_HORIZONTAL", "horizontal" },
        { META_MAXIMIZE_VERTICAL, "META_MAXIMIZE_VERTICAL", "vertical" },
        { 0, NULL, NULL }
      };
      GType g_enum_type_id;

      g_enum_type_id =
        g_flags_register_static (g_intern_static_string ("MetaMaximizeFlags"), values);

      g_once_init_leave (&g_enum_type_id__volatile, g_enum_type_id);
    }

  return g_enum_type_id__volatile;
}

/* Generated data ends here */

