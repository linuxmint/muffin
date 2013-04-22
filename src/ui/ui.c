/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin interface for talking to GTK+ UI module */

/* 
 * Copyright (C) 2002 Havoc Pennington
 * stock icon code Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
#include <meta/prefs.h>
#include "ui.h"
#include "frames.h"
#include <meta/util.h>
#include "menu.h"
#include "core.h"
#include "theme-private.h"

#include "inlinepixbufs.h"

#include <string.h>
#include <stdlib.h>
#include <cairo-xlib.h>

static void meta_stock_icons_init (void);
static void meta_ui_accelerator_parse (const char      *accel,
                                       guint           *keysym,
                                       guint           *keycode,
                                       GdkModifierType *keymask);

struct _MetaUI
{
  Display *xdisplay;
  Screen *xscreen;
  MetaFrames *frames;

  /* For double-click tracking */
  guint button_click_number;
  Window button_click_window;
  int button_click_x;
  int button_click_y;
  guint32 button_click_time;
};

LOCAL_SYMBOL void
meta_ui_init (void)
{
  /* As of 2.91.7, Gdk uses XI2 by default, which conflicts with the
   * direct X calls we use - in particular, events caused by calls to
   * XGrabPointer/XGrabKeyboard are no longer understood by GDK, while
   * GDK will no longer generate the core XEvents we process.
   * So at least for now, enforce the previous behavior.
   */
#if GTK_CHECK_VERSION(2, 91, 7)
  gdk_disable_multidevice ();
#endif

  if (!gtk_init_check (NULL, NULL))
    meta_fatal ("Unable to open X display %s\n", XDisplayName (NULL));

  meta_stock_icons_init ();
}

LOCAL_SYMBOL Display*
meta_ui_get_display (void)
{
  return GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
}

/* We do some of our event handling in frames.c, which expects
 * GDK events delivered by GTK+.  However, since the transition to
 * client side windows, we can't let GDK see button events, since the
 * client-side tracking of implicit and explicit grabs it does will
 * get confused by our direct use of X grabs in the core code.
 *
 * So we do a very minimal GDK => GTK event conversion here and send on the
 * events we care about, and then filter them out so they don't go
 * through the normal GDK event handling.
 *
 * To reduce the amount of code, the only events fields filled out
 * below are the ones that frames.c uses. If frames.c is modified to
 * use more fields, more fields need to be filled out below.
 */

static gboolean
maybe_redirect_mouse_event (XEvent *xevent)
{
  GdkDisplay *gdisplay;
  GdkDeviceManager *gmanager;
  GdkDevice *gdevice;
  MetaUI *ui;
  GdkEvent *gevent;
  GdkWindow *gdk_window;
  Window window;

  switch (xevent->type)
    {
    case ButtonPress:
    case ButtonRelease:
      window = xevent->xbutton.window;
      break;
    case MotionNotify:
      window = xevent->xmotion.window;
      break;
    case EnterNotify:
    case LeaveNotify:
      window = xevent->xcrossing.window;
      break;
    default:
      return FALSE;
    }

  gdisplay = gdk_x11_lookup_xdisplay (xevent->xany.display);
  ui = g_object_get_data (G_OBJECT (gdisplay), "meta-ui");
  if (!ui)
    return FALSE;

  gdk_window = gdk_x11_window_lookup_for_display (gdisplay, window);
  if (gdk_window == NULL)
    return FALSE;

  gmanager = gdk_display_get_device_manager (gdisplay);
  gdevice = gdk_device_manager_get_client_pointer (gmanager);

  /* If GDK already thinks it has a grab, we better let it see events; this
   * is the menu-navigation case and events need to get sent to the appropriate
   * (client-side) subwindow for individual menu items.
   */
  if (gdk_display_device_is_grabbed (gdisplay, gdevice))
    return FALSE;

  switch (xevent->type)
    {
    case ButtonPress:
    case ButtonRelease:
      if (xevent->type == ButtonPress)
        {
          GtkSettings *settings = gtk_settings_get_default ();
          int double_click_time;
          int double_click_distance;

          g_object_get (settings,
                        "gtk-double-click-time", &double_click_time,
                        "gtk-double-click-distance", &double_click_distance,
                        NULL);

          if (xevent->xbutton.button == ui->button_click_number &&
              xevent->xbutton.window == ui->button_click_window &&
              xevent->xbutton.time < ui->button_click_time + double_click_time &&
              ABS (xevent->xbutton.x - ui->button_click_x) <= double_click_distance &&
              ABS (xevent->xbutton.y - ui->button_click_y) <= double_click_distance)
            {
              gevent = gdk_event_new (GDK_2BUTTON_PRESS);

              ui->button_click_number = 0;
            }
          else
            {
              gevent = gdk_event_new (GDK_BUTTON_PRESS);
              ui->button_click_number = xevent->xbutton.button;
              ui->button_click_window = xevent->xbutton.window;
              ui->button_click_time = xevent->xbutton.time;
              ui->button_click_x = xevent->xbutton.x;
              ui->button_click_y = xevent->xbutton.y;
            }
        }
      else
        {
          gevent = gdk_event_new (GDK_BUTTON_RELEASE);
        }

      gevent->button.window = g_object_ref (gdk_window);
      gevent->button.button = xevent->xbutton.button;
      gevent->button.time = xevent->xbutton.time;
      gevent->button.x = xevent->xbutton.x;
      gevent->button.y = xevent->xbutton.y;
      gevent->button.x_root = xevent->xbutton.x_root;
      gevent->button.y_root = xevent->xbutton.y_root;

      break;
    case MotionNotify:
      gevent = gdk_event_new (GDK_MOTION_NOTIFY);
      gevent->motion.type = GDK_MOTION_NOTIFY;
      gevent->motion.window = g_object_ref (gdk_window);
      break;
    case EnterNotify:
    case LeaveNotify:
      gevent = gdk_event_new (xevent->type == EnterNotify ? GDK_ENTER_NOTIFY : GDK_LEAVE_NOTIFY);
      gevent->crossing.window = g_object_ref (gdk_window);
      gevent->crossing.x = xevent->xcrossing.x;
      gevent->crossing.y = xevent->xcrossing.y;
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  /* If we've gotten here, we've created the gdk_event and should send it on */
  gdk_event_set_device (gevent, gdevice);
  gtk_main_do_event (gevent);
  gdk_event_free (gevent);

  return TRUE;
}

typedef struct _EventFunc EventFunc;

struct _EventFunc
{
  MetaEventFunc func;
  gpointer data;
};

static EventFunc *ef = NULL;

static GdkFilterReturn
filter_func (GdkXEvent *xevent,
             GdkEvent *event,
             gpointer data)
{
  g_return_val_if_fail (ef != NULL, GDK_FILTER_CONTINUE);

  if ((* ef->func) (xevent, ef->data) ||
      maybe_redirect_mouse_event (xevent))
    return GDK_FILTER_REMOVE;
  else
    return GDK_FILTER_CONTINUE;
}

LOCAL_SYMBOL void
meta_ui_add_event_func (Display       *xdisplay,
                        MetaEventFunc  func,
                        gpointer       data)
{
  g_return_if_fail (ef == NULL);

  ef = g_new (EventFunc, 1);
  ef->func = func;
  ef->data = data;

  gdk_window_add_filter (NULL, filter_func, ef);
}

/* removal is by data due to proxy function */
LOCAL_SYMBOL void
meta_ui_remove_event_func (Display       *xdisplay,
                           MetaEventFunc  func,
                           gpointer       data)
{
  g_return_if_fail (ef != NULL);
  
  gdk_window_remove_filter (NULL, filter_func, ef);

  g_free (ef);
  ef = NULL;
}

LOCAL_SYMBOL MetaUI*
meta_ui_new (Display *xdisplay,
             Screen  *screen)
{
  GdkDisplay *gdisplay;
  MetaUI *ui;

  ui = g_new0 (MetaUI, 1);
  ui->xdisplay = xdisplay;
  ui->xscreen = screen;

  gdisplay = gdk_x11_lookup_xdisplay (xdisplay);
  g_assert (gdisplay == gdk_display_get_default ());

  ui->frames = meta_frames_new (XScreenNumberOfScreen (screen));
  /* This does not actually show any widget. MetaFrames has been hacked so
   * that showing it doesn't actually do anything. But we need the flags
   * set for GTK to deliver events properly. */
  gtk_widget_show (GTK_WIDGET (ui->frames));

  g_object_set_data (G_OBJECT (gdisplay), "meta-ui", ui);

  return ui;
}

LOCAL_SYMBOL void
meta_ui_free (MetaUI *ui)
{
  GdkDisplay *gdisplay;

  gtk_widget_destroy (GTK_WIDGET (ui->frames));

  gdisplay = gdk_x11_lookup_xdisplay (ui->xdisplay);
  g_object_set_data (G_OBJECT (gdisplay), "meta-ui", NULL);

  g_free (ui);
}

LOCAL_SYMBOL void
meta_ui_get_frame_borders (MetaUI *ui,
                           Window frame_xwindow,
                           MetaFrameBorders *borders)
{
  meta_frames_get_borders (ui->frames, frame_xwindow,
                           borders);
}

LOCAL_SYMBOL void
meta_ui_get_corner_radiuses (MetaUI *ui,
                             Window  xwindow,
                             float  *top_left,
                             float  *top_right,
                             float  *bottom_left,
                             float  *bottom_right)
{
  meta_frames_get_corner_radiuses (ui->frames, xwindow,
                                   top_left, top_right,
                                   bottom_left, bottom_right);
}

LOCAL_SYMBOL Window
meta_ui_create_frame_window (MetaUI *ui,
                             Display *xdisplay,
                             Visual *xvisual,
			     gint x,
			     gint y,
			     gint width,
			     gint height,
			     gint screen_no,
                             gulong *create_serial)
{
  GdkDisplay *display = gdk_x11_lookup_xdisplay (xdisplay);
  GdkScreen *screen = gdk_display_get_screen (display, screen_no);
  GdkWindowAttr attrs;
  gint attributes_mask;
  GdkWindow *window;
  GdkVisual *visual;
  
  /* Default depth/visual handles clients with weird visuals; they can
   * always be children of the root depth/visual obviously, but
   * e.g. DRI games can't be children of a parent that has the same
   * visual as the client.
   */
  if (!xvisual)
    visual = gdk_screen_get_system_visual (screen);
  else
    {
      visual = gdk_x11_screen_lookup_visual (screen,
                                             XVisualIDFromVisual (xvisual));
    }

  attrs.title = NULL;

  /* frame.c is going to replace the event mask immediately, but
   * we still have to set it here to let GDK know what it is.
   */
  attrs.event_mask =
    GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
    GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
    GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_FOCUS_CHANGE_MASK;
  attrs.x = x;
  attrs.y = y;
  attrs.wclass = GDK_INPUT_OUTPUT;
  attrs.visual = visual;
  attrs.window_type = GDK_WINDOW_CHILD;
  attrs.cursor = NULL;
  attrs.wmclass_name = NULL;
  attrs.wmclass_class = NULL;
  attrs.override_redirect = FALSE;

  attrs.width  = width;
  attrs.height = height;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  /* We make an assumption that gdk_window_new() is going to call
   * XCreateWindow as it's first operation; this seems to be true currently
   * as long as you pass in a colormap.
   */
  if (create_serial)
    *create_serial = XNextRequest (xdisplay);
  window =
    gdk_window_new (gdk_screen_get_root_window(screen),
		    &attrs, attributes_mask);

  gdk_window_resize (window, width, height);
  
  meta_frames_manage_window (ui->frames, GDK_WINDOW_XID (window), window);

  return GDK_WINDOW_XID (window);
}

LOCAL_SYMBOL void
meta_ui_destroy_frame_window (MetaUI *ui,
			      Window  xwindow)
{
  meta_frames_unmanage_window (ui->frames, xwindow);
}

LOCAL_SYMBOL void
meta_ui_move_resize_frame (MetaUI *ui,
			   Window frame,
			   int x,
			   int y,
			   int width,
			   int height)
{
  meta_frames_move_resize_frame (ui->frames, frame, x, y, width, height);
}

LOCAL_SYMBOL void
meta_ui_map_frame   (MetaUI *ui,
                     Window  xwindow)
{
  GdkWindow *window;
  GdkDisplay *display;

  display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  if (window)
    gdk_window_show_unraised (window);
}

LOCAL_SYMBOL void
meta_ui_unmap_frame (MetaUI *ui,
                     Window  xwindow)
{
  GdkWindow *window;
  GdkDisplay *display;

  display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  if (window)
    gdk_window_hide (window);
}

LOCAL_SYMBOL void
meta_ui_unflicker_frame_bg (MetaUI *ui,
                            Window  xwindow,
                            int     target_width,
                            int     target_height)
{
  meta_frames_unflicker_bg (ui->frames, xwindow,
                            target_width, target_height);
}

LOCAL_SYMBOL void
meta_ui_update_frame_style (MetaUI  *ui,
                            Window   xwindow)
{
  meta_frames_update_frame_style (ui->frames, xwindow);
}

LOCAL_SYMBOL void
meta_ui_repaint_frame (MetaUI *ui,
                       Window xwindow)
{
  meta_frames_repaint_frame (ui->frames, xwindow);
}

LOCAL_SYMBOL void
meta_ui_reset_frame_bg (MetaUI *ui,
                        Window xwindow)
{
  meta_frames_reset_bg (ui->frames, xwindow);
}

LOCAL_SYMBOL cairo_region_t *
meta_ui_get_frame_bounds (MetaUI  *ui,
                          Window   xwindow,
                          int      window_width,
                          int      window_height)
{
  return meta_frames_get_frame_bounds (ui->frames, xwindow,
                                       window_width, window_height);
}

LOCAL_SYMBOL void
meta_ui_queue_frame_draw (MetaUI *ui,
                          Window xwindow)
{
  meta_frames_queue_draw (ui->frames, xwindow);
}

LOCAL_SYMBOL void
meta_ui_set_frame_title (MetaUI     *ui,
                         Window      xwindow,
                         const char *title)
{
  meta_frames_set_title (ui->frames, xwindow, title);
}

LOCAL_SYMBOL MetaWindowMenu*
meta_ui_window_menu_new  (MetaUI             *ui,
                          Window              client_xwindow,
                          MetaMenuOp          ops,
                          MetaMenuOp          insensitive,
                          unsigned long       active_workspace,
                          int                 n_workspaces,
                          MetaWindowMenuFunc  func,
                          gpointer            data)
{
  return meta_window_menu_new (ui->frames,
                               ops, insensitive,
                               client_xwindow,
                               active_workspace,
                               n_workspaces,
                               func, data);
}

LOCAL_SYMBOL void
meta_ui_window_menu_popup (MetaWindowMenu     *menu,
                           int                 root_x,
                           int                 root_y,
                           int                 button,
                           guint32             timestamp)
{
  meta_window_menu_popup (menu, root_x, root_y, button, timestamp);
}

LOCAL_SYMBOL void
meta_ui_window_menu_free (MetaWindowMenu *menu)
{
  meta_window_menu_free (menu);
}

LOCAL_SYMBOL GdkPixbuf*
meta_gdk_pixbuf_get_from_pixmap (Pixmap       xpixmap,
                                 int          src_x,
                                 int          src_y,
                                 int          width,
                                 int          height)
{
  cairo_surface_t *surface;
  Display *display;
  Window root_return;
  int x_ret, y_ret;
  unsigned int w_ret, h_ret, bw_ret, depth_ret;
  XWindowAttributes attrs;
  GdkPixbuf *retval;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  if (!XGetGeometry (display, xpixmap, &root_return,
                     &x_ret, &y_ret, &w_ret, &h_ret, &bw_ret, &depth_ret))
    return NULL;

  if (depth_ret == 1)
    {
      surface = cairo_xlib_surface_create_for_bitmap (display,
                                                      xpixmap,
                                                      GDK_SCREEN_XSCREEN (gdk_screen_get_default ()),
                                                      w_ret,
                                                      h_ret);
    }
  else
    {
      if (!XGetWindowAttributes (display, root_return, &attrs))
        return NULL;

      surface = cairo_xlib_surface_create (display,
                                           xpixmap,
                                           attrs.visual,
                                           w_ret, h_ret);
    }

  retval = gdk_pixbuf_get_from_surface (surface,
                                        src_x,
                                        src_y,
                                        width,
                                        height);
  cairo_surface_destroy (surface);

  return retval;
}

LOCAL_SYMBOL void
meta_ui_push_delay_exposes (MetaUI *ui)
{
  meta_frames_push_delay_exposes (ui->frames);
}

LOCAL_SYMBOL void
meta_ui_pop_delay_exposes  (MetaUI *ui)
{
  meta_frames_pop_delay_exposes (ui->frames);
}

LOCAL_SYMBOL GdkPixbuf*
meta_ui_get_default_window_icon (MetaUI *ui)
{
  static GdkPixbuf *default_icon = NULL;

  if (default_icon == NULL)
    {
      GtkIconTheme *theme;
      gboolean icon_exists;

      theme = gtk_icon_theme_get_default ();

      icon_exists = gtk_icon_theme_has_icon (theme, META_DEFAULT_ICON_NAME);

      if (icon_exists)
          default_icon = gtk_icon_theme_load_icon (theme,
                                                   META_DEFAULT_ICON_NAME,
                                                   META_ICON_WIDTH,
                                                   0,
                                                   NULL);
      else
          default_icon = gtk_icon_theme_load_icon (theme,
                                                   "image-missing",
                                                   META_ICON_WIDTH,
                                                   0,
                                                   NULL);

      g_assert (default_icon);
    }

  g_object_ref (G_OBJECT (default_icon));
  
  return default_icon;
}

LOCAL_SYMBOL GdkPixbuf*
meta_ui_get_default_mini_icon (MetaUI *ui)
{
  static GdkPixbuf *default_icon = NULL;

  if (default_icon == NULL)
    {
      GtkIconTheme *theme;
      gboolean icon_exists;

      theme = gtk_icon_theme_get_default ();

      icon_exists = gtk_icon_theme_has_icon (theme, META_DEFAULT_ICON_NAME);

      if (icon_exists)
          default_icon = gtk_icon_theme_load_icon (theme,
                                                   META_DEFAULT_ICON_NAME,
                                                   META_MINI_ICON_WIDTH,
                                                   0,
                                                   NULL);
      else
          default_icon = gtk_icon_theme_load_icon (theme,
                                                   "image-missing",
                                                   META_MINI_ICON_WIDTH,
                                                   0,
                                                   NULL);

      g_assert (default_icon);
    }

  g_object_ref (G_OBJECT (default_icon));
  
  return default_icon;
}

LOCAL_SYMBOL gboolean
meta_ui_window_should_not_cause_focus (Display *xdisplay,
                                       Window   xwindow)
{
  GdkWindow *window;
  GdkDisplay *display;

  display = gdk_x11_lookup_xdisplay (xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  /* we shouldn't cause focus if we're an override redirect
   * toplevel which is not foreign
   */
  if (window && gdk_window_get_window_type (window) == GDK_WINDOW_TEMP)
    return TRUE;
  else
    return FALSE;
}

LOCAL_SYMBOL char*
meta_text_property_to_utf8 (Display             *xdisplay,
                            const XTextProperty *prop)
{
  GdkDisplay *display;
  char **list;
  int count;
  char *retval;
  
  list = NULL;

  display = gdk_x11_lookup_xdisplay (xdisplay);
  count = gdk_text_property_to_utf8_list_for_display (display,
                                                      gdk_x11_xatom_to_atom_for_display (display, prop->encoding),
                                                      prop->format,
                                                      prop->value,
                                                      prop->nitems,
                                                      &list);

  if (count == 0)
    retval = NULL;
  else
    {
  retval = list[0];
  list[0] = g_strdup (""); /* something to free */
    }
  
  g_strfreev (list);

  return retval;
}

LOCAL_SYMBOL void
meta_ui_theme_get_frame_borders (MetaUI *ui,
                                 MetaFrameType      type,
                                 MetaFrameFlags     flags,
                                 MetaFrameBorders  *borders)
{
  int text_height;
  GtkStyleContext *style = NULL;
  PangoContext *context;
  const PangoFontDescription *font_desc;

  if (meta_ui_have_a_theme ())
    {
      context = gtk_widget_get_pango_context (GTK_WIDGET (ui->frames));
      font_desc = meta_prefs_get_titlebar_font ();

      if (!font_desc)
        {
          style = gtk_style_context_new ();
          font_desc = gtk_style_context_get_font (style, 0);
        }

      text_height = meta_pango_font_desc_get_text_height (font_desc, context);

      meta_theme_get_frame_borders (meta_theme_get_current (),
                                    type, text_height, flags,
                                    borders);
    }
  else
    {
      meta_frame_borders_clear (borders);
    }

  if (style != NULL)
    g_object_unref (style);
}

LOCAL_SYMBOL void
meta_ui_set_current_theme (const char *name,
                           gboolean    force_reload)
{
  meta_theme_set_current (name, force_reload);
  meta_invalidate_default_icons ();
}

LOCAL_SYMBOL gboolean
meta_ui_have_a_theme (void)
{
  return meta_theme_get_current () != NULL;
}

static void
meta_ui_accelerator_parse (const char      *accel,
                           guint           *keysym,
                           guint           *keycode,
                           GdkModifierType *keymask)
{
  const char *above_tab;

  if (accel[0] == '0' && accel[1] == 'x')
    {
      *keysym = 0;
      *keycode = (guint) strtoul (accel, NULL, 16);
      *keymask = 0;

      return;
    }

  /* The key name 'Above_Tab' is special - it's not an actual keysym name,
   * but rather refers to the key above the tab key. In order to use
   * the GDK parsing for modifiers in combination with it, we substitute
   * it with 'Tab' temporarily before calling gtk_accelerator_parse().
   */
#define is_word_character(c) (g_ascii_isalnum(c) || ((c) == '_'))
#define ABOVE_TAB "Above_Tab"
#define ABOVE_TAB_LEN 9

  above_tab = strstr (accel, ABOVE_TAB);
  if (above_tab &&
      (above_tab == accel || !is_word_character (above_tab[-1])) &&
      !is_word_character (above_tab[ABOVE_TAB_LEN]))
    {
      char *before = g_strndup (accel, above_tab - accel);
      char *after = g_strdup (above_tab + ABOVE_TAB_LEN);
      char *replaced = g_strconcat (before, "Tab", after, NULL);

      gtk_accelerator_parse (replaced, NULL, keymask);

      g_free (before);
      g_free (after);
      g_free (replaced);

      *keysym = META_KEY_ABOVE_TAB;
      return;
    }

#undef is_word_character
#undef ABOVE_TAB
#undef ABOVE_TAB_LEN

  gtk_accelerator_parse (accel, keysym, keymask);
}

LOCAL_SYMBOL gboolean
meta_ui_parse_accelerator (const char          *accel,
                           unsigned int        *keysym,
                           unsigned int        *keycode,
                           MetaVirtualModifier *mask)
{
  GdkModifierType gdk_mask = 0;
  guint gdk_sym = 0;
  guint gdk_code = 0;
  
  *keysym = 0;
  *keycode = 0;
  *mask = 0;

  if (!accel[0] || strcmp (accel, "disabled") == 0)
    return TRUE;
  
  meta_ui_accelerator_parse (accel, &gdk_sym, &gdk_code, &gdk_mask);
  if (gdk_mask == 0 && gdk_sym == 0 && gdk_code == 0)
    return FALSE;

  if (gdk_sym == None && gdk_code == 0)
    return FALSE;
  
  if (gdk_mask & GDK_RELEASE_MASK) /* we don't allow this */
    return FALSE;
  
  *keysym = gdk_sym;
  *keycode = gdk_code;

  if (gdk_mask & GDK_SHIFT_MASK)
    *mask |= META_VIRTUAL_SHIFT_MASK;
  if (gdk_mask & GDK_CONTROL_MASK)
    *mask |= META_VIRTUAL_CONTROL_MASK;
  if (gdk_mask & GDK_MOD1_MASK)
    *mask |= META_VIRTUAL_ALT_MASK;
  if (gdk_mask & GDK_MOD2_MASK)
    *mask |= META_VIRTUAL_MOD2_MASK;
  if (gdk_mask & GDK_MOD3_MASK)
    *mask |= META_VIRTUAL_MOD3_MASK;
  if (gdk_mask & GDK_MOD4_MASK)
    *mask |= META_VIRTUAL_MOD4_MASK;
  if (gdk_mask & GDK_MOD5_MASK)
    *mask |= META_VIRTUAL_MOD5_MASK;
  if (gdk_mask & GDK_SUPER_MASK)
    *mask |= META_VIRTUAL_SUPER_MASK;
  if (gdk_mask & GDK_HYPER_MASK)
    *mask |= META_VIRTUAL_HYPER_MASK;
  if (gdk_mask & GDK_META_MASK)
    *mask |= META_VIRTUAL_META_MASK;
  
  return TRUE;
}

/* Caller responsible for freeing return string of meta_ui_accelerator_name! */
LOCAL_SYMBOL gchar*
meta_ui_accelerator_name  (unsigned int        keysym,
                           MetaVirtualModifier mask)
{
  GdkModifierType mods = 0;
        
  if (keysym == 0 && mask == 0)
    {
      return g_strdup ("disabled");
    }

  if (mask & META_VIRTUAL_SHIFT_MASK)
    mods |= GDK_SHIFT_MASK;
  if (mask & META_VIRTUAL_CONTROL_MASK)
    mods |= GDK_CONTROL_MASK;
  if (mask & META_VIRTUAL_ALT_MASK)
    mods |= GDK_MOD1_MASK;
  if (mask & META_VIRTUAL_MOD2_MASK)
    mods |= GDK_MOD2_MASK;
  if (mask & META_VIRTUAL_MOD3_MASK)
    mods |= GDK_MOD3_MASK;
  if (mask & META_VIRTUAL_MOD4_MASK)
    mods |= GDK_MOD4_MASK;
  if (mask & META_VIRTUAL_MOD5_MASK)
    mods |= GDK_MOD5_MASK;
  if (mask & META_VIRTUAL_SUPER_MASK)
    mods |= GDK_SUPER_MASK;
  if (mask & META_VIRTUAL_HYPER_MASK)
    mods |= GDK_HYPER_MASK;
  if (mask & META_VIRTUAL_META_MASK)
    mods |= GDK_META_MASK;

  return gtk_accelerator_name (keysym, mods);

}

LOCAL_SYMBOL gboolean
meta_ui_parse_modifier (const char          *accel,
                        MetaVirtualModifier *mask)
{
  GdkModifierType gdk_mask = 0;
  guint gdk_sym = 0;
  guint gdk_code = 0;
  
  *mask = 0;

  if (accel == NULL || !accel[0] || strcmp (accel, "disabled") == 0)
    return TRUE;
  
  meta_ui_accelerator_parse (accel, &gdk_sym, &gdk_code, &gdk_mask);
  if (gdk_mask == 0 && gdk_sym == 0 && gdk_code == 0)
    return FALSE;

  if (gdk_sym != None || gdk_code != 0)
    return FALSE;
  
  if (gdk_mask & GDK_RELEASE_MASK) /* we don't allow this */
    return FALSE;

  if (gdk_mask & GDK_SHIFT_MASK)
    *mask |= META_VIRTUAL_SHIFT_MASK;
  if (gdk_mask & GDK_CONTROL_MASK)
    *mask |= META_VIRTUAL_CONTROL_MASK;
  if (gdk_mask & GDK_MOD1_MASK)
    *mask |= META_VIRTUAL_ALT_MASK;
  if (gdk_mask & GDK_MOD2_MASK)
    *mask |= META_VIRTUAL_MOD2_MASK;
  if (gdk_mask & GDK_MOD3_MASK)
    *mask |= META_VIRTUAL_MOD3_MASK;
  if (gdk_mask & GDK_MOD4_MASK)
    *mask |= META_VIRTUAL_MOD4_MASK;
  if (gdk_mask & GDK_MOD5_MASK)
    *mask |= META_VIRTUAL_MOD5_MASK;
  if (gdk_mask & GDK_SUPER_MASK)
    *mask |= META_VIRTUAL_SUPER_MASK;
  if (gdk_mask & GDK_HYPER_MASK)
    *mask |= META_VIRTUAL_HYPER_MASK;
  if (gdk_mask & GDK_META_MASK)
    *mask |= META_VIRTUAL_META_MASK;
  
  return TRUE;
}

LOCAL_SYMBOL gboolean
meta_ui_window_is_widget (MetaUI *ui,
                          Window  xwindow)
{
  GdkDisplay *display;
  GdkWindow *window;

  display = gdk_x11_lookup_xdisplay (ui->xdisplay);
  window = gdk_x11_window_lookup_for_display (display, xwindow);

  if (window)
    {
      void *user_data = NULL;
      gdk_window_get_user_data (window, &user_data);
      return user_data != NULL && user_data != ui->frames;
    }
  else
    return FALSE;
}

/* stock icon code Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org> */
typedef struct
{
  char *stock_id;
  const guint8 *icon_data;
} MetaStockIcon;

static void
meta_stock_icons_init (void)
{
  GtkIconFactory *factory;
  int i;

  MetaStockIcon items[] =
  {
    { METACITY_STOCK_DELETE,   stock_delete_data   },
    { METACITY_STOCK_MINIMIZE, stock_minimize_data },
    { METACITY_STOCK_MAXIMIZE, stock_maximize_data }
  };

  factory = gtk_icon_factory_new ();
  gtk_icon_factory_add_default (factory);

  for (i = 0; i < (gint) G_N_ELEMENTS (items); i++)
    {
      GtkIconSet *icon_set;
      GdkPixbuf *pixbuf;

      pixbuf = gdk_pixbuf_new_from_inline (-1, items[i].icon_data,
					   FALSE,
					   NULL);

      icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
      gtk_icon_factory_add (factory, items[i].stock_id, icon_set);
      gtk_icon_set_unref (icon_set);
      
      g_object_unref (G_OBJECT (pixbuf));
    }

  g_object_unref (G_OBJECT (factory));
}

LOCAL_SYMBOL MetaUIDirection
meta_ui_get_direction (void)
{
  if (gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL)
    return META_UI_DIRECTION_RTL;

  return META_UI_DIRECTION_LTR;
}

