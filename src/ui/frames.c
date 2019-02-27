/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Metacity window frame manager widget */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 2005, 2006 Elijah Newren
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
#include <math.h>
#include <string.h>
#include <meta/boxes.h>
#include "frames.h"
#include <meta/util.h>
#include "core.h"
#include "menu.h"
#include "window-private.h"
#include <meta/theme.h>
#include <meta/prefs.h>
#include "ui.h"

#include <cairo-xlib.h>

#ifdef HAVE_SHAPE
#include <X11/extensions/shape.h>
#endif

#define DEFAULT_INNER_BUTTON_BORDER 3

static void meta_frames_destroy       (GtkWidget       *object);
static void meta_frames_finalize      (GObject         *object);
static void meta_frames_style_updated (GtkWidget       *widget);

static void meta_frames_update_prelit_control (MetaFrames      *frames,
                                               MetaUIFrame     *frame,
                                               MetaFrameControl control);
static gboolean meta_frames_button_press_event    (GtkWidget           *widget,
                                                   GdkEventButton      *event);
static gboolean meta_frames_button_release_event  (GtkWidget           *widget,
                                                   GdkEventButton      *event);
static gboolean meta_frames_motion_notify_event   (GtkWidget           *widget,
                                                   GdkEventMotion      *event);
static gboolean meta_frames_destroy_event         (GtkWidget           *widget,
                                                   GdkEventAny         *event);
static gboolean meta_frames_draw                  (GtkWidget           *widget,
                                                   cairo_t             *cr);
static gboolean meta_frames_enter_notify_event    (GtkWidget           *widget,
                                                   GdkEventCrossing    *event);
static gboolean meta_frames_leave_notify_event    (GtkWidget           *widget,
                                                   GdkEventCrossing    *event);

static void meta_frames_attach_style (MetaFrames  *frames,
                                      MetaUIFrame *frame);

static void meta_frames_paint        (MetaFrames   *frames,
                                      MetaUIFrame  *frame,
                                      cairo_t      *cr);

static void meta_frames_ensure_layout (MetaFrames      *frames,
                                       MetaUIFrame     *frame);

static MetaUIFrame* meta_frames_lookup_window (MetaFrames *frames,
                                               Window      xwindow);

static void meta_frames_font_changed          (MetaFrames *frames);
static void meta_frames_button_layout_changed (MetaFrames *frames);


static GdkRectangle*    control_rect (MetaFrameControl   control,
                                      MetaFrameGeometry *fgeom);
static MetaFrameControl get_control  (MetaFrames        *frames,
                                      MetaUIFrame       *frame,
                                      int                x,
                                      int                y);
static void invalidate_all_caches (MetaFrames *frames);
static void invalidate_whole_window (MetaFrames *frames,
                                     MetaUIFrame *frame);

G_DEFINE_TYPE (MetaFrames, meta_frames, GTK_TYPE_WINDOW);

static GObject *
meta_frames_constructor (GType                  gtype,
                         guint                  n_properties,
                         GObjectConstructParam *properties)
{
  GObject *object;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (meta_frames_parent_class);
  object = gobject_class->constructor (gtype, n_properties, properties);

  g_object_set (object,
                "type", GTK_WINDOW_POPUP,
                NULL);

  return object;
}

static void
meta_frames_class_init (MetaFramesClass *class)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS (class);
  widget_class = (GtkWidgetClass*) class;

  gobject_class->constructor = meta_frames_constructor;
  gobject_class->finalize = meta_frames_finalize;

  widget_class->destroy = meta_frames_destroy;

  widget_class->style_updated = meta_frames_style_updated;

  widget_class->draw = meta_frames_draw;
  widget_class->destroy_event = meta_frames_destroy_event;  
  widget_class->button_press_event = meta_frames_button_press_event;
  widget_class->button_release_event = meta_frames_button_release_event;
  widget_class->motion_notify_event = meta_frames_motion_notify_event;
  widget_class->enter_notify_event = meta_frames_enter_notify_event;
  widget_class->leave_notify_event = meta_frames_leave_notify_event;
}

static gint
unsigned_long_equal (gconstpointer v1,
                     gconstpointer v2)
{
  return *((const gulong*) v1) == *((const gulong*) v2);
}

static guint
unsigned_long_hash (gconstpointer v)
{
  gulong val = * (const gulong *) v;

  /* I'm not sure this works so well. */
#if GLIB_SIZEOF_LONG > 4
  return (guint) (val ^ (val >> 32));
#else
  return val;
#endif
}

static void
prefs_changed_callback (MetaPreference pref,
                        void          *data)
{
  switch (pref)
    {
    case META_PREF_TITLEBAR_FONT:
      meta_frames_font_changed (META_FRAMES (data));
      break;
    case META_PREF_BUTTON_LAYOUT:
      meta_frames_button_layout_changed (META_FRAMES (data));
      break;
    default:
      break;
    }
}

static GtkStyleContext *
create_style_context (MetaFrames  *frames,
                      const gchar *variant)
{
  GtkStyleContext *style;
  GdkScreen *screen;
  char *theme_name;

  screen = gtk_widget_get_screen (GTK_WIDGET (frames));
  g_object_get (gtk_settings_get_for_screen (screen),
                "gtk-theme-name", &theme_name,
                NULL);

  style = gtk_style_context_new ();
  gtk_style_context_set_path (style,
                              gtk_widget_get_path (GTK_WIDGET (frames)));

  if (theme_name && *theme_name)
    {
      GtkCssProvider *provider;

      provider = gtk_css_provider_get_named (theme_name, variant);
      gtk_style_context_add_provider (style,
                                      GTK_STYLE_PROVIDER (provider),
                                      GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
    }

  g_free (theme_name);

  return style;
}

static GtkStyleContext *
meta_frames_get_theme_variant (MetaFrames  *frames,
                               const gchar *variant)
{
  GtkStyleContext *style;

  style = g_hash_table_lookup (frames->style_variants, variant);
  if (style == NULL)
    {
      style = create_style_context (frames, variant);
      g_hash_table_insert (frames->style_variants, g_strdup (variant), style);
    }

  return style;
}

static void
update_style_contexts (MetaFrames *frames)
{
  GtkStyleContext *style;
  GList *variant_list, *variant;

  if (frames->normal_style)
    g_object_unref (frames->normal_style);
  frames->normal_style = create_style_context (frames, NULL);

  variant_list = g_hash_table_get_keys (frames->style_variants);
  for (variant = variant_list; variant; variant = variant->next)
    {
      style = create_style_context (frames, (char *)variant->data);
      g_hash_table_insert (frames->style_variants,
                           g_strdup (variant->data), style);
    }
  g_list_free (variant_list);
}

static void
meta_frames_init (MetaFrames *frames)
{
  frames->text_heights = g_hash_table_new (NULL, NULL);
  
  frames->frames = g_hash_table_new (unsigned_long_hash, unsigned_long_equal);

  frames->invalidate_cache_timeout_id = 0;
  frames->invalidate_frames = NULL;
  frames->cache = g_hash_table_new (g_direct_hash, g_direct_equal);

  frames->style_variants = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_object_unref);
  frames->entered = FALSE;
  frames->last_cursor_x = -1;
  frames->last_cursor_y = -1;
  frames->last_control = META_FRAME_CONTROL_NONE;

  frames->display = meta_get_display ();
  frames->xdisplay = meta_display_get_xdisplay (frames->display);

  update_style_contexts (frames);

  gtk_widget_set_double_buffered (GTK_WIDGET (frames), FALSE);

  meta_prefs_add_listener (prefs_changed_callback, frames);
}

static void
listify_func (gpointer key, gpointer value, gpointer data)
{
  GSList **listp;

  listp = data;
  *listp = g_slist_prepend (*listp, value);
}

static void
meta_frames_destroy (GtkWidget *object)
{
  GSList *winlist;
  GSList *tmp;
  MetaFrames *frames;
  
  frames = META_FRAMES (object);

  winlist = NULL;
  g_hash_table_foreach (frames->frames, listify_func, &winlist);

  /* Unmanage all frames */
  for (tmp = winlist; tmp != NULL; tmp = tmp->next)
    {
      MetaUIFrame *frame;

      frame = tmp->data;

      meta_frames_unmanage_window (frames, frame->xwindow);
    }
  g_slist_free (winlist);

  if (frames->normal_style)
    {
      g_object_unref (frames->normal_style);
      frames->normal_style = NULL;
    }

  if (frames->style_variants)
    {
      g_hash_table_destroy (frames->style_variants);
      frames->style_variants = NULL;
    }

  GTK_WIDGET_CLASS (meta_frames_parent_class)->destroy (object);
}

static void
meta_frames_finalize (GObject *object)
{
  MetaFrames *frames;
  
  frames = META_FRAMES (object);

  meta_prefs_remove_listener (prefs_changed_callback, frames);
  
  g_hash_table_destroy (frames->text_heights);

  invalidate_all_caches (frames);
  if (frames->invalidate_cache_timeout_id) {
    g_source_remove (frames->invalidate_cache_timeout_id);
    frames->invalidate_cache_timeout_id = 0;
  }
  
  g_assert (g_hash_table_size (frames->frames) == 0);
  g_hash_table_destroy (frames->frames);
  g_hash_table_destroy (frames->cache);

  G_OBJECT_CLASS (meta_frames_parent_class)->finalize (object);
}

typedef struct
{
  cairo_rectangle_int_t rect;
  cairo_surface_t *pixmap;
} CachedFramePiece;

typedef struct
{
  /* Caches of the four rendered sides in a MetaFrame.
   * Order: top (titlebar), left, right, bottom.
   */
  CachedFramePiece piece[4];
} CachedPixels;

static CachedPixels *
get_cache (MetaFrames *frames,
           MetaUIFrame *frame)
{
  CachedPixels *pixels;
  
  pixels = g_hash_table_lookup (frames->cache, frame);

  if (!pixels)
    {
      pixels = g_new0 (CachedPixels, 1);
      g_hash_table_insert (frames->cache, frame, pixels);
    }

  return pixels;
}

static void
invalidate_cache (MetaFrames *frames,
                  MetaUIFrame *frame)
{
  CachedPixels *pixels = get_cache (frames, frame);
  int i;
  
  for (i = 0; i < 4; i++)
    if (pixels->piece[i].pixmap)
      cairo_surface_destroy (pixels->piece[i].pixmap);
  
  g_free (pixels);
  g_hash_table_remove (frames->cache, frame);
}

static void
invalidate_all_caches (MetaFrames *frames)
{
  GList *l;

  for (l = frames->invalidate_frames; l; l = l->next)
    {
      MetaUIFrame *frame = l->data;

      invalidate_cache (frames, frame);
    }
  
  g_list_free (frames->invalidate_frames);
  frames->invalidate_frames = NULL;
}

static gboolean
invalidate_cache_timeout (gpointer data)
{
  MetaFrames *frames = data;
  
  invalidate_all_caches (frames);
  frames->invalidate_cache_timeout_id = 0;
  return FALSE;
}

static void
queue_recalc_func (gpointer key, gpointer value, gpointer data)
{
  MetaUIFrame *frame;
  MetaFrames *frames;

  frames = META_FRAMES (data);
  frame = value;

  invalidate_whole_window (frames, frame);
  meta_core_queue_frame_resize (frames->xdisplay, frame->xwindow);
  if (frame->layout)
    {
      /* save title to recreate layout */
      g_free (frame->title);
      
      frame->title = g_strdup (pango_layout_get_text (frame->layout));

      g_object_unref (G_OBJECT (frame->layout));
      frame->layout = NULL;
    }
}

static void
meta_frames_font_changed (MetaFrames *frames)
{
  if (g_hash_table_size (frames->text_heights) > 0)
    {
      g_hash_table_destroy (frames->text_heights);
      frames->text_heights = g_hash_table_new (NULL, NULL);
    }
  
  /* Queue a draw/resize on all frames */
  g_hash_table_foreach (frames->frames,
                        queue_recalc_func, frames);

}

static void
queue_draw_func (gpointer key, gpointer value, gpointer data)
{
  MetaUIFrame *frame;
  MetaFrames *frames;

  frames = META_FRAMES (data);
  frame = value;

  invalidate_whole_window (frames, frame);
}

static void
meta_frames_button_layout_changed (MetaFrames *frames)
{
  g_hash_table_foreach (frames->frames,
                        queue_draw_func, frames);
}

static void
reattach_style_func (gpointer key, gpointer value, gpointer data)
{
  MetaUIFrame *frame;
  MetaFrames *frames;

  frames = META_FRAMES (data);
  frame = value;

  meta_frames_attach_style (frames, frame);
}

static void
meta_frames_style_updated  (GtkWidget *widget)
{
  MetaFrames *frames;

  frames = META_FRAMES (widget);

  meta_frames_font_changed (frames);

  update_style_contexts (frames);

  g_hash_table_foreach (frames->frames,
                        reattach_style_func, frames);

  GTK_WIDGET_CLASS (meta_frames_parent_class)->style_updated (widget);
}

static void
meta_frames_ensure_layout (MetaFrames  *frames,
                           MetaUIFrame *frame)
{
  GtkWidget *widget;
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaFrameStyle *style;

  widget = GTK_WIDGET (frames);

  g_return_if_fail (gtk_widget_get_realized (widget));

  flags = meta_frame_get_flags (frame->meta_window->frame);
  type = meta_window_get_frame_type (frame->meta_window);

  style = meta_theme_get_frame_style (meta_theme_get_current (),
                                      type, flags);

  if (style != frame->cache_style)
    {
      if (frame->layout)
        {
          /* save title to recreate layout */
          g_free (frame->title);
          
          frame->title = g_strdup (pango_layout_get_text (frame->layout));

          g_object_unref (G_OBJECT (frame->layout));
          frame->layout = NULL;
        }
    }

  frame->cache_style = style;
  
  if (frame->layout == NULL)
    {
      gpointer key, value;
      PangoFontDescription *font_desc;
      double scale;
      int size;
      
      scale = meta_theme_get_title_scale (meta_theme_get_current (),
                                          type,
                                          flags);
      
      frame->layout = gtk_widget_create_pango_layout (widget, frame->title);

      pango_layout_set_ellipsize (frame->layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_auto_dir (frame->layout, FALSE);
      
      font_desc = meta_gtk_widget_get_font_desc (widget, scale,
                                                 meta_prefs_get_titlebar_font ());

      size = pango_font_description_get_size (font_desc);

      if (g_hash_table_lookup_extended (frames->text_heights,
                                        GINT_TO_POINTER (size),
                                        &key, &value))
        {
          frame->text_height = GPOINTER_TO_INT (value);
        }
      else
        {
          frame->text_height =
            meta_pango_font_desc_get_text_height (font_desc,
                                                  gtk_widget_get_pango_context (widget));

          g_hash_table_replace (frames->text_heights,
                                GINT_TO_POINTER (size),
                                GINT_TO_POINTER (frame->text_height));
        }
      
      pango_layout_set_font_description (frame->layout, 
                                         font_desc);
      
      pango_font_description_free (font_desc);

      /* Save some RAM */
      g_free (frame->title);
      frame->title = NULL;
    }
}

void
meta_frames_calc_geometry (MetaFrames *frames,
                           Window      xwindow)
{
  MetaUIFrame *frame;

  frame = g_hash_table_lookup (frames->frames, &xwindow);

  if (frame == NULL)
    return;

  int width = frame->meta_window->rect.width;
  int height = frame->meta_window->rect.height;
  MetaFrameGeometry fgeom;
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaButtonLayout button_layout;

  flags = meta_frame_get_flags (frame->meta_window->frame);
  type = meta_window_get_frame_type (frame->meta_window);

  meta_frames_ensure_layout (frames, frame);

  meta_prefs_get_button_layout (&button_layout);
  
  meta_theme_calc_geometry (meta_theme_get_current (),
                            type,
                            frame->text_height,
                            flags,
                            width,
                            height,
                            &button_layout,
                            &fgeom);
  frame->fgeom = fgeom;
}

LOCAL_SYMBOL MetaFrames*
meta_frames_new (int screen_number)
{
  GdkScreen *screen;
  MetaFrames *frames;

  screen = gdk_display_get_screen (gdk_display_get_default (),
                                   screen_number);

  frames = g_object_new (META_TYPE_FRAMES,
                         "screen", screen,
                         "type", GTK_WINDOW_POPUP,
                         NULL);

  /* Put the window at an arbitrary offscreen location; the one place
   * it can't be is at -100x-100, since the meta_window_new() will
   * mistake it for a window created via meta_create_offscreen_window()
   * and ignore it, and we need this window to get frame-synchronization
   * messages so that GTK+'s style change handling works.
   */
  gtk_window_move (GTK_WINDOW (frames), -200, -200);
  gtk_window_resize (GTK_WINDOW (frames), 1, 1);

  return frames;
}

/* In order to use a style with a window it has to be attached to that
 * window. Actually, the colormaps just have to match, but since GTK+
 * already takes care of making sure that its cheap to attach a style
 * to multiple windows with the same colormap, we can just go ahead
 * and attach separately for each window.
 */
static void
meta_frames_attach_style (MetaFrames  *frames,
                          MetaUIFrame *frame)
{
  char *variant = NULL;

  if (frame->style != NULL)
    g_object_unref (frame->style);

  variant = frame->meta_window->gtk_theme_variant;

  if (variant == NULL || strcmp(variant, "normal") == 0)
    frame->style = g_object_ref (frames->normal_style);
  else
    frame->style = g_object_ref (meta_frames_get_theme_variant (frames,
                                                                variant));
}

LOCAL_SYMBOL void
meta_frames_manage_window (MetaFrames *frames,
                           MetaWindow *meta_window,
                           Window      xwindow,
                           GdkWindow  *window)
{
  MetaUIFrame *frame;

  g_assert (window);

  frame = g_new (MetaUIFrame, 1);
  
  frame->window = window;

  gdk_window_set_user_data (frame->window, frames);

  frame->style = NULL;

  /* Don't set event mask here, it's in frame.c */
  
  frame->xwindow = xwindow;
  frame->cache_style = NULL;
  frame->layout = NULL;
  frame->meta_window = meta_window;
  frame->text_height = -1;
  frame->title = NULL;
  frame->shape_applied = FALSE;
  frame->prelit_control = META_FRAME_CONTROL_NONE;
  frame->button_state = META_BUTTON_STATE_NORMAL;

  meta_display_grab_window_buttons (meta_window->display, frame->xwindow);

  g_hash_table_replace (frames->frames, &frame->xwindow, frame);

  meta_frames_calc_geometry (frames, meta_window->xwindow);
}

LOCAL_SYMBOL void
meta_frames_unmanage_window (MetaFrames *frames,
                             Window      xwindow)
{
  MetaUIFrame *frame;

  frame = g_hash_table_lookup (frames->frames, &xwindow);

  if (frame)
    {
      /* invalidating all caches ensures the frame
       * is not actually referenced anymore
       */
      invalidate_all_caches (frames);
      
      /* restore the cursor */
      meta_core_set_screen_cursor (frames->xdisplay,
                                   frame->xwindow,
                                   META_CURSOR_DEFAULT);

      gdk_window_set_user_data (frame->window, NULL);

      g_hash_table_remove (frames->frames, &frame->xwindow);

      g_object_unref (frame->style);

      gdk_window_destroy (frame->window);

      if (frame->layout)
        g_object_unref (G_OBJECT (frame->layout));

      g_free (frame->title);

      g_free (frame);
    }
  else
    meta_warning ("Frame 0x%lx not managed, can't unmanage\n", xwindow);
}

static MetaUIFrame*
meta_frames_lookup_window (MetaFrames *frames,
                           Window      xwindow)
{
  MetaUIFrame *frame;

  frame = g_hash_table_lookup (frames->frames, &xwindow);

  return frame;
}

LOCAL_SYMBOL void
meta_frames_get_borders (MetaFrames *frames,
                         Window xwindow,
                         MetaFrameBorders *borders)
{
  MetaFrameFlags flags;
  MetaUIFrame *frame;
  MetaFrameType type;
  
  frame = meta_frames_lookup_window (frames, xwindow);

  if (frame == NULL)
    meta_bug ("No such frame 0x%lx\n", xwindow);

  flags = meta_frame_get_flags (frame->meta_window->frame);
  type = meta_window_get_frame_type (frame->meta_window);

  g_return_if_fail (type < META_FRAME_TYPE_LAST);

  meta_frames_ensure_layout (frames, frame);
  
  /* We can't get the full geometry, because that depends on
   * the client window size and probably we're being called
   * by the core move/resize code to decide on the client
   * window size
   */
  meta_theme_get_frame_borders (meta_theme_get_current (),
                                type,
                                frame->text_height,
                                flags,
                                borders);
}

LOCAL_SYMBOL void
meta_frames_get_corner_radiuses (MetaFrames *frames,
                                 Window      xwindow,
                                 float      *top_left,
                                 float      *top_right,
                                 float      *bottom_left,
                                 float      *bottom_right)
{
  MetaUIFrame *frame;

  frame = meta_frames_lookup_window (frames, xwindow);

  if (frame == NULL)
    return;

  /* For compatibility with the code in get_visible_rect(), there's
   * a mysterious sqrt() added to the corner radiuses:
   *
   *   const float radius = sqrt(corner) + corner;
   *
   * It's unclear why the radius is calculated like this, but we
   * need to be consistent with it.
   */

  if (top_left)
    *top_left = frame->fgeom.top_left_corner_rounded_radius + sqrt(frame->fgeom.top_left_corner_rounded_radius);
  if (top_right)
    *top_right = frame->fgeom.top_right_corner_rounded_radius + sqrt(frame->fgeom.top_right_corner_rounded_radius);
  if (bottom_left)
    *bottom_left = frame->fgeom.bottom_left_corner_rounded_radius + sqrt(frame->fgeom.bottom_left_corner_rounded_radius);
  if (bottom_right)
    *bottom_right = frame->fgeom.bottom_right_corner_rounded_radius + sqrt(frame->fgeom.bottom_right_corner_rounded_radius);
}

/* The visible frame rectangle surrounds the visible portion of the
 * frame window; it subtracts only the invisible borders from the frame
 * window's size.
 */
static void
get_visible_frame_rect (MetaFrameGeometry     *fgeom,
                        int                    window_width,
                        int                    window_height,
                        cairo_rectangle_int_t *rect)
{
  rect->x = fgeom->borders.invisible.left;
  rect->y = fgeom->borders.invisible.top;
  rect->width = window_width - fgeom->borders.invisible.right - rect->x;
  rect->height = window_height - fgeom->borders.invisible.bottom - rect->y;
}

static cairo_region_t *
get_visible_region (MetaFrames        *frames,
                    MetaUIFrame       *frame,
                    MetaFrameGeometry *fgeom,
                    int                window_width,
                    int                window_height)
{
  cairo_region_t *corners_region;
  cairo_region_t *visible_region;
  cairo_rectangle_int_t rect;
  cairo_rectangle_int_t frame_rect;

  corners_region = cairo_region_create ();
  get_visible_frame_rect (fgeom, window_width, window_height, &frame_rect);
  
  if (fgeom->top_left_corner_rounded_radius != 0)
    {
      const int corner = fgeom->top_left_corner_rounded_radius;
      const float radius = sqrt(corner) + corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x;
          rect.y = frame_rect.y + i;
          rect.width = width;
          rect.height = 1;
          
          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  if (fgeom->top_right_corner_rounded_radius != 0)
    {
      const int corner = fgeom->top_right_corner_rounded_radius;
      const float radius = sqrt(corner) + corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x + frame_rect.width - width;
          rect.y = frame_rect.y + i;
          rect.width = width;
          rect.height = 1;
          
          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  if (fgeom->bottom_left_corner_rounded_radius != 0)
    {
      const int corner = fgeom->bottom_left_corner_rounded_radius;
      const float radius = sqrt(corner) + corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x;
          rect.y = frame_rect.y + frame_rect.height - i - 1;
          rect.width = width;
          rect.height = 1;
          
          cairo_region_union_rectangle (corners_region, &rect);
        }
    }

  if (fgeom->bottom_right_corner_rounded_radius != 0)
    {
      const int corner = fgeom->bottom_right_corner_rounded_radius;
      const float radius = sqrt(corner) + corner;
      int i;

      for (i=0; i<corner; i++)
        {
          const int width = floor(0.5 + radius - sqrt(radius*radius - (radius-(i+0.5))*(radius-(i+0.5))));
          rect.x = frame_rect.x + frame_rect.width - width;
          rect.y = frame_rect.y + frame_rect.height - i - 1;
          rect.width = width;
          rect.height = 1;
          
          cairo_region_union_rectangle (corners_region, &rect);
        }
    }
  
  visible_region = cairo_region_create_rectangle (&frame_rect);
  cairo_region_subtract (visible_region, corners_region);
  cairo_region_destroy (corners_region);

  return visible_region;
}

LOCAL_SYMBOL cairo_region_t *
meta_frames_get_frame_bounds (MetaFrames *frames,
                              Window      xwindow,
                              int         window_width,
                              int         window_height)
{
  MetaUIFrame *frame;

  frame = meta_frames_lookup_window (frames, xwindow);
  g_return_val_if_fail (frame != NULL, NULL);

  return get_visible_region (frames, frame,
                             &frame->fgeom,
                             window_width, window_height);
}

LOCAL_SYMBOL void
meta_frames_move_resize_frame (MetaFrames *frames,
                               Window      xwindow,
                               int         x,
                               int         y,
                               int         width,
                               int         height)
{
  MetaUIFrame *frame = meta_frames_lookup_window (frames, xwindow);
  int old_width, old_height;
  
  old_width = gdk_window_get_width (frame->window);
  old_height = gdk_window_get_height (frame->window);

  gdk_window_move_resize (frame->window, x, y, width, height);

  if (old_width != width || old_height != height)
    invalidate_whole_window (frames, frame);

  meta_frames_calc_geometry (frames, xwindow);
}

LOCAL_SYMBOL void
meta_frames_queue_draw (MetaFrames *frames,
                        Window      xwindow)
{
  MetaUIFrame *frame;
  
  frame = meta_frames_lookup_window (frames, xwindow);

  invalidate_whole_window (frames, frame);
}

LOCAL_SYMBOL void
meta_frames_set_title (MetaFrames *frames,
                       Window      xwindow,
                       const char *title)
{
  MetaUIFrame *frame;
  
  frame = meta_frames_lookup_window (frames, xwindow);

  g_assert (frame);
  
  g_free (frame->title);
  frame->title = g_strdup (title);
  
  if (frame->layout)
    {
      g_object_unref (frame->layout);
      frame->layout = NULL;
    }

  invalidate_whole_window (frames, frame);
}

LOCAL_SYMBOL void
meta_frames_update_frame_style (MetaFrames *frames,
                                Window      xwindow)
{
  MetaUIFrame *frame;

  frame = meta_frames_lookup_window (frames, xwindow);

  g_assert (frame);

  meta_frames_attach_style (frames, frame);
  invalidate_whole_window (frames, frame);
}

LOCAL_SYMBOL void
meta_frames_repaint_frame (MetaFrames *frames,
                           Window      xwindow)
{
  MetaUIFrame *frame;
  
  frame = meta_frames_lookup_window (frames, xwindow);

  g_assert (frame);

  /* repaint everything, so the other frame don't
   * lag behind if they are exposed
   */
  gdk_window_process_all_updates ();
}

static void
redraw_control (MetaFrames *frames,
                MetaUIFrame *frame,
                MetaFrameControl control)
{
  GdkRectangle *rect;

  rect = control_rect (control, &frame->fgeom);

  gdk_window_invalidate_rect (frame->window, rect, FALSE);
  invalidate_cache (frames, frame);
}

enum
{
    MOUSEWHEEL_UP   = 4,
    MOUSEWHEEL_DOWN = 5
};

static gboolean
meta_frame_titlebar_event (MetaUIFrame    *frame,
                           GdkEventButton *event,
                           int            action)
{
  MetaFrameFlags flags;
  Display *display;

  display = meta_display_get_xdisplay (meta_get_display ());

  switch (action)
    {
    case C_DESKTOP_TITLEBAR_ACTION_TOGGLE_SHADE:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ALLOWS_SHADE)
          {
            if (flags & META_FRAME_SHADED)
              meta_core_unshade (display,
                                 frame->xwindow,
                                 event->time);
            else
              meta_core_shade (display,
                               frame->xwindow,
                               event->time);
          }
      }
      break;          
      
    case C_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize (display, frame->xwindow);
          }
      }
      break;

    case C_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_HORIZONTALLY:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize_horizontally (display, frame->xwindow);
          }
      }
      break;

    case C_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_VERTICALLY:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize_vertically (display, frame->xwindow);
          }
      }
      break;

    case C_DESKTOP_TITLEBAR_ACTION_TOGGLE_STUCK:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_STUCK)
          meta_core_unstick (display,
                             frame->xwindow);
        else
          meta_core_stick (display,
                           frame->xwindow);
      }
      break;

    case C_DESKTOP_TITLEBAR_ACTION_TOGGLE_ABOVE:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ABOVE)
          meta_core_unmake_above (display,
                                  frame->xwindow);
        else
          meta_core_make_above (display,
                                frame->xwindow);
      }
      break;

    case C_DESKTOP_TITLEBAR_ACTION_MINIMIZE:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ALLOWS_MINIMIZE)
          {
            meta_core_minimize (display, frame->xwindow);
          }
      }
      break;

    case C_DESKTOP_TITLEBAR_ACTION_NONE:
      /* Yaay, a sane user that doesn't use that other weird crap! */
      break;
    
    case C_DESKTOP_TITLEBAR_ACTION_LOWER:
      meta_core_user_lower_and_unfocus (display,
                                        frame->xwindow,
                                        event->time);
      break;

    case C_DESKTOP_TITLEBAR_ACTION_MENU:
      meta_core_show_window_menu (display,
                                  frame->xwindow,
                                  event->x_root,
                                  event->y_root,
                                  event->button,
                                  event->time);
      break;

    /* These last 3 are CDesktopTitlebarScrollAction but
       since we're working with ints, it doesn't matter */

    case C_DESKTOP_TITLEBAR_SCROLL_ACTION_SHADE:
      {
        flags = meta_frame_get_flags (frame->meta_window->frame);

        if (flags & META_FRAME_ALLOWS_SHADE)
          {
            if (event->button == MOUSEWHEEL_DOWN &&
                flags & META_FRAME_SHADED)
              meta_core_unshade (display,
                                 frame->xwindow,
                                 event->time);
            else if (event->button == MOUSEWHEEL_UP &&
                     flags & ~META_FRAME_SHADED)
              meta_core_shade (display,
                               frame->xwindow,
                               event->time);
          }
      }
      break;

    case C_DESKTOP_TITLEBAR_SCROLL_ACTION_OPACITY:
      {
        if (event->button == MOUSEWHEEL_UP) {
            meta_core_adjust_opacity (display,
                                      frame->xwindow,
                                      TRUE); /* TRUE = increase */
        } else {
            meta_core_adjust_opacity (display,
                                      frame->xwindow,
                                      FALSE); /* decrease */
        }
      }
      break;

    case C_DESKTOP_TITLEBAR_SCROLL_ACTION_NONE:
      break;
    }
  
  return TRUE;
}

static gboolean
meta_frame_double_click_event (MetaUIFrame    *frame,
                               GdkEventButton *event)
{
  int action = meta_prefs_get_action_double_click_titlebar ();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_middle_click_event (MetaUIFrame    *frame,
                               GdkEventButton *event)
{
  int action = meta_prefs_get_action_middle_click_titlebar();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_right_click_event(MetaUIFrame     *frame,
                             GdkEventButton  *event)
{
  int action = meta_prefs_get_action_right_click_titlebar();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_scroll_wheel_event (MetaUIFrame     *frame,
                               GdkEventButton  *event)
{
  int action = meta_prefs_get_action_scroll_wheel_titlebar();

  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frames_try_grab_op (MetaFrames  *frames,
                         MetaUIFrame *frame,
                         MetaGrabOp   op,
                         gdouble      grab_x,
                         gdouble      grab_y,
                         guint32      time)
{
  gboolean ret;

  ret = meta_display_begin_grab_op (frame->meta_window->display,
                                    frame->meta_window->screen,
                                    frame->meta_window,
                                    op, FALSE,
                                    TRUE,
                                    frame->grab_button, 0,
                                    time, grab_x, grab_y);
  if (!ret)
    {
      frames->current_grab_op = op;
      frames->grab_frame = frame;
      frames->grab_x = grab_x;
      frames->grab_y = grab_y;
    }

  return ret;
}

static gboolean
meta_frames_retry_grab_op (MetaFrames *frames,
                           guint       time)
{
  Display *display;
  MetaGrabOp op;

  if (frames->current_grab_op == META_GRAB_OP_NONE)
    return TRUE;

  op = frames->current_grab_op;
  frames->current_grab_op = META_GRAB_OP_NONE;
  display = frames->xdisplay;

  return meta_core_begin_grab_op (display,
                                  frames->grab_frame->xwindow,
                                  op,
                                  FALSE,
                                  TRUE,
                                  frames->grab_frame->grab_button,
                                  0,
                                  time,
                                  frames->grab_x,
                                  frames->grab_y);
}

static gboolean
meta_frame_double_click_edge_event (MetaUIFrame        *frame,
                                    GdkEventButton     *event,
                                    MetaFrameControl    control)
{
    switch (control) {
        case META_FRAME_CONTROL_RESIZE_N:
        case META_FRAME_CONTROL_RESIZE_S:
            return meta_frame_titlebar_event (frame,
                                              event,
                                              C_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_VERTICALLY);
        case META_FRAME_CONTROL_RESIZE_E:
        case META_FRAME_CONTROL_RESIZE_W:
            return meta_frame_titlebar_event (frame,
                                              event,
                                              C_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_HORIZONTALLY);
        default:
            return FALSE;
        }
}

static gboolean
meta_frames_button_press_event (GtkWidget      *widget,
                                GdkEventButton *event)
{
  MetaUIFrame *frame;
  MetaFrames *frames;
  MetaFrameControl control;
  Display *display;
  
  frames = META_FRAMES (widget);
  display = frames->xdisplay;

  /* Remember that the display may have already done something with this event.
   * If so there's probably a GrabOp in effect.
   */
  
  frame = meta_frames_lookup_window (frames, GDK_WINDOW_XID (event->window));
  if (frame == NULL)
    return FALSE;

  control = get_control (frames, frame, event->x, event->y);

  /* focus on click, even if click was on client area */
  if (event->button == 1 &&
      !(control == META_FRAME_CONTROL_MINIMIZE ||
        control == META_FRAME_CONTROL_DELETE ||
        control == META_FRAME_CONTROL_MAXIMIZE))
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Focusing window with frame 0x%lx due to button 1 press\n",
                  frame->xwindow);
      meta_core_user_focus (display,
                            frame->xwindow,
                            event->time);      
    }

  /* don't do the rest of this if on client area */
  if (control == META_FRAME_CONTROL_CLIENT_AREA)
    return FALSE; /* not on the frame, just passed through from client */
  
  /* We want to shade even if we have a GrabOp, since we'll have a move grab
   * if we double click the titlebar.
   */
  if (control == META_FRAME_CONTROL_TITLE &&
      event->button == 1 &&
      event->type == GDK_2BUTTON_PRESS)
    {
      meta_core_end_grab_op (display, event->time);
      return meta_frame_double_click_event (frame, event);
    }

  if (event->button == 1 &&
      event->type == GDK_2BUTTON_PRESS &&
      (control == META_FRAME_CONTROL_RESIZE_N ||
       control == META_FRAME_CONTROL_RESIZE_S ||
       control == META_FRAME_CONTROL_RESIZE_E ||
       control == META_FRAME_CONTROL_RESIZE_W))
    {
      meta_core_end_grab_op (display, event->time);
      return meta_frame_double_click_edge_event (frame, event, control);
    }

  if (meta_core_get_grab_op (display) != META_GRAB_OP_NONE)
    return FALSE; /* already up to something */

  frame->grab_button = event->button;

  if (event->button == 1 &&
      (control == META_FRAME_CONTROL_MAXIMIZE ||
       control == META_FRAME_CONTROL_UNMAXIMIZE ||
       control == META_FRAME_CONTROL_MINIMIZE ||
       control == META_FRAME_CONTROL_DELETE ||
       control == META_FRAME_CONTROL_SHADE ||
       control == META_FRAME_CONTROL_UNSHADE ||
       control == META_FRAME_CONTROL_ABOVE ||
       control == META_FRAME_CONTROL_UNABOVE ||
       control == META_FRAME_CONTROL_STICK ||
       control == META_FRAME_CONTROL_UNSTICK ||
       control == META_FRAME_CONTROL_MENU))
    {
      frames->grab_xwindow = frame->xwindow;

      frame->grab_button = event->button;
      frame->button_state = META_BUTTON_STATE_PRESSED;
      frame->prelit_control = control;
      redraw_control (frames, frame, control);

      if (control == META_FRAME_CONTROL_MENU)
        {
          GdkRectangle *rect;
          int dx, dy;

          rect = control_rect (META_FRAME_CONTROL_MENU, &frame->fgeom);

          /* get delta to convert to root coords */
          dx = event->x_root - event->x;
          dy = event->y_root - event->y;
          
          /* Align to the right end of the menu rectangle if RTL */
          if (meta_ui_get_direction() == META_UI_DIRECTION_RTL)
            dx += rect->width;

          meta_core_show_window_menu (display,
                                      frame->xwindow,
                                      rect->x + dx,
                                      rect->y + rect->height + dy,
                                      event->button,
                                      event->time);
        }
    }
  else if (event->button == 1 &&
           (control == META_FRAME_CONTROL_RESIZE_SE ||
            control == META_FRAME_CONTROL_RESIZE_S ||
            control == META_FRAME_CONTROL_RESIZE_SW ||
            control == META_FRAME_CONTROL_RESIZE_NE ||
            control == META_FRAME_CONTROL_RESIZE_N ||
            control == META_FRAME_CONTROL_RESIZE_NW ||
            control == META_FRAME_CONTROL_RESIZE_E ||
            control == META_FRAME_CONTROL_RESIZE_W))
    {
      MetaGrabOp op;
      
      op = META_GRAB_OP_NONE;
      
      switch (control)
        {
        case META_FRAME_CONTROL_RESIZE_SE:
          op = META_GRAB_OP_RESIZING_SE;
          break;
        case META_FRAME_CONTROL_RESIZE_S:
          op = META_GRAB_OP_RESIZING_S;
          break;
        case META_FRAME_CONTROL_RESIZE_SW:
          op = META_GRAB_OP_RESIZING_SW;
          break;
        case META_FRAME_CONTROL_RESIZE_NE:
          op = META_GRAB_OP_RESIZING_NE;
          break;
        case META_FRAME_CONTROL_RESIZE_N:
          op = META_GRAB_OP_RESIZING_N;
          break;
        case META_FRAME_CONTROL_RESIZE_NW:
          op = META_GRAB_OP_RESIZING_NW;
          break;
        case META_FRAME_CONTROL_RESIZE_E:
          op = META_GRAB_OP_RESIZING_E;
          break;
        case META_FRAME_CONTROL_RESIZE_W:
          op = META_GRAB_OP_RESIZING_W;
          break;
        default:
          g_assert_not_reached ();
          break;
        }

      meta_frames_try_grab_op (frames, frame, op,
                               event->x_root, event->y_root,
                               event->time);
    }
  else if (control == META_FRAME_CONTROL_TITLE &&
           event->button == 1)
    {
      MetaFrameFlags flags;

      meta_core_get (display, frame->xwindow,
                     META_CORE_GET_FRAME_FLAGS, &flags,
                     META_CORE_GET_END);

      if (flags & META_FRAME_ALLOWS_MOVE)
        {
          meta_frames_try_grab_op (frames, frame,
                                   META_GRAB_OP_MOVING,
                                   event->x_root, event->y_root,
                                   event->time);
        }
    }
  else if (control == META_FRAME_CONTROL_TITLE &&
           (event->button == 4 || event->button == 5))
    {
      return meta_frame_scroll_wheel_event (frame, event);
    }
  else if (event->button == 2)
    {
      return meta_frame_middle_click_event (frame, event);
    }
  else if (event->button == 3)
    {
      return meta_frame_right_click_event (frame, event);
    }
  
  return TRUE;
}

static gboolean
meta_frames_button_release_event    (GtkWidget           *widget,
                                     GdkEventButton      *event)
{
  MetaUIFrame *frame;
  MetaFrames *frames;
  MetaGrabOp op;
  Display *display;
  
  frames = META_FRAMES (widget);
  display = frames->xdisplay;
  frames->current_grab_op = META_GRAB_OP_NONE;

  frame = meta_frames_lookup_window (frames, GDK_WINDOW_XID (event->window));
  if (frame == NULL)
    return FALSE;

  /* We only handle the releases we handled the presses for (things
   * involving frame controls). Window ops that don't require a
   * frame are handled in the Xlib part of the code, display.c/window.c
   */
  if (frame->xwindow == frames->grab_xwindow &&
      ((int) event->button) == frame->grab_button &&
      frame->button_state == META_BUTTON_STATE_PRESSED)
    {
      switch (frame->prelit_control)
        {
        case META_FRAME_CONTROL_MINIMIZE:
          meta_core_minimize (display, frame->xwindow);
          break;
        case META_FRAME_CONTROL_MAXIMIZE:
          /* Focus the window on the maximize */
          meta_core_user_focus (display, frame->xwindow, event->time);
          meta_core_maximize (display, frame->xwindow);
          break;
        case META_FRAME_CONTROL_UNMAXIMIZE:
          meta_core_unmaximize (display, frame->xwindow);
          break;
        case META_FRAME_CONTROL_DELETE:
          meta_core_delete (display, frame->xwindow, event->time);
          break;
        case META_FRAME_CONTROL_SHADE:
          meta_core_shade (display, frame->xwindow, event->time);
          break;
        case META_FRAME_CONTROL_UNSHADE:
          meta_core_unshade (display, frame->xwindow, event->time);
          break;
        case META_FRAME_CONTROL_ABOVE:
          meta_core_make_above (display, frame->xwindow);
          break;
        case META_FRAME_CONTROL_UNABOVE:
          meta_core_unmake_above (display, frame->xwindow);
          break;
        case META_FRAME_CONTROL_STICK:
          meta_core_stick (display, frame->xwindow);
          break;
        case META_FRAME_CONTROL_UNSTICK:
          meta_core_unstick (display, frame->xwindow);
          break;
        default:
          break;
        }

      /* Update the prelit control regardless of what button the mouse
       * was released over; needed so that the new button can become
       * prelit so to let the user know that it can now be pressed.
       * :)
       */
      MetaFrameControl control = get_control (frames, frame, event->x, event->y);
      meta_frames_update_prelit_control (frames, frame, control);
    }
  
  return TRUE;
}

static void
meta_frames_update_prelit_control (MetaFrames      *frames,
                                   MetaUIFrame     *frame,
                                   MetaFrameControl control)
{
  MetaFrameControl old_control;
  MetaCursor cursor;


  meta_verbose ("Updating prelit control from %u to %u\n",
                frame->prelit_control, control);
  
  cursor = META_CURSOR_DEFAULT;
  
  switch (control)
    {
    case META_FRAME_CONTROL_CLIENT_AREA:
      break;
    case META_FRAME_CONTROL_NONE:
      break;
    case META_FRAME_CONTROL_TITLE:
      break;
    case META_FRAME_CONTROL_DELETE:
      break;
    case META_FRAME_CONTROL_MENU:
      break;
    case META_FRAME_CONTROL_MINIMIZE:
      break;
    case META_FRAME_CONTROL_MAXIMIZE:
      break;
    case META_FRAME_CONTROL_UNMAXIMIZE:
      break;
    case META_FRAME_CONTROL_SHADE:
      break;
    case META_FRAME_CONTROL_UNSHADE:
      break;
    case META_FRAME_CONTROL_ABOVE:
      break;
    case META_FRAME_CONTROL_UNABOVE:
      break;
    case META_FRAME_CONTROL_STICK:
      break;
    case META_FRAME_CONTROL_UNSTICK:
      break;
    case META_FRAME_CONTROL_RESIZE_SE:
      cursor = META_CURSOR_SE_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_S:
      cursor = META_CURSOR_SOUTH_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_SW:
      cursor = META_CURSOR_SW_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_N:
      cursor = META_CURSOR_NORTH_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_NE:
      cursor = META_CURSOR_NE_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_NW:
      cursor = META_CURSOR_NW_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_W:
      cursor = META_CURSOR_WEST_RESIZE;
      break;
    case META_FRAME_CONTROL_RESIZE_E:
      cursor = META_CURSOR_EAST_RESIZE;
      break;
    }        

  /* set/unset the prelight cursor */
  meta_frame_set_screen_cursor (frame->meta_window->frame, cursor);

  switch (control)
    {
    case META_FRAME_CONTROL_MENU:
    case META_FRAME_CONTROL_MINIMIZE:
    case META_FRAME_CONTROL_MAXIMIZE:
    case META_FRAME_CONTROL_DELETE:
    case META_FRAME_CONTROL_SHADE:
    case META_FRAME_CONTROL_UNSHADE:
    case META_FRAME_CONTROL_ABOVE:
    case META_FRAME_CONTROL_UNABOVE:
    case META_FRAME_CONTROL_STICK:
    case META_FRAME_CONTROL_UNSTICK:
    case META_FRAME_CONTROL_UNMAXIMIZE:
      /* leave control set */
      break;
    default:
      /* Only prelight buttons */
      control = META_FRAME_CONTROL_NONE;
      break;
    }      

  if (control == frame->prelit_control &&
      frame->button_state == META_BUTTON_STATE_PRELIGHT)
    return;

  /* Save the old control so we can unprelight it */
  old_control = frame->prelit_control;

  frame->button_state = META_BUTTON_STATE_PRELIGHT;
  frame->prelit_control = control;

  redraw_control (frames, frame, old_control);
  redraw_control (frames, frame, control);
}

static gboolean
meta_frames_motion_notify_event     (GtkWidget           *widget,
                                     GdkEventMotion      *event)
{
  MetaUIFrame *frame;
  MetaFrames *frames;
  MetaFrameControl control;
  Window xwindow;
  int x, y;

  frames = META_FRAMES (widget);

  if (!frames->entered)
    return FALSE;

  xwindow = GDK_WINDOW_XID (event->window);

  frame = meta_frames_lookup_window (frames, xwindow);
  if (frame == NULL)
    return FALSE;

  x = event->x;
  y = event->y;

  if (frames->last_cursor_x == x && frames->last_cursor_y == y)
    return FALSE;

  frames->last_cursor_x = x;
  frames->last_cursor_y = y;

  control = get_control (frames, frame, x, y);

  if (frame->button_state == META_BUTTON_STATE_PRESSED)
    {
      /* If the user leaves the frame button, set the state
       * back to normal and redraw. */
      if (frame->prelit_control != control)
        {
          frame->button_state = META_BUTTON_STATE_NORMAL;
          redraw_control (frames, frame, frame->prelit_control);
        }
    }
  else if (control != META_FRAME_CONTROL_CLIENT_AREA && control != frames->last_control)
    {
      /* Update prelit control and cursor */
      meta_frames_update_prelit_control (frames, frame, control);
    }

  frames->last_control = control;

  if ((event->state & GDK_BUTTON1_MASK) &&
      frames->current_grab_op != META_GRAB_OP_NONE)
    meta_frames_retry_grab_op (frames, event->time);

  return TRUE;
}

static gboolean
meta_frames_destroy_event           (GtkWidget           *widget,
                                     GdkEventAny         *event)
{
  MetaUIFrame *frame;
  MetaFrames *frames;

  frames = META_FRAMES (widget);

  frame = meta_frames_lookup_window (frames, GDK_WINDOW_XID (event->window));
  if (frame == NULL)
    return FALSE;
  
  return TRUE;
}


static void
setup_bg_cr (cairo_t *cr, GdkWindow *window, int x_offset, int y_offset)
{
  GdkWindow *parent = gdk_window_get_parent (window);
  cairo_pattern_t *bg_pattern;

  bg_pattern = gdk_window_get_background_pattern (window);
  if (bg_pattern == NULL && parent)
    {
      gint window_x, window_y;

      gdk_window_get_position (window, &window_x, &window_y);
      setup_bg_cr (cr, parent, x_offset + window_x, y_offset + window_y);
    }
  else if (bg_pattern)
    {
      cairo_translate (cr, - x_offset, - y_offset);
      cairo_set_source (cr, bg_pattern);
      cairo_translate (cr, x_offset, y_offset);
    }
}

/* Returns a pixmap with a piece of the windows frame painted on it.
*/

static cairo_surface_t *
generate_pixmap (MetaFrames            *frames,
                 MetaUIFrame           *frame,
                 cairo_rectangle_int_t *rect)
{
  cairo_surface_t *result;
  cairo_t *cr;

  /* do not create a pixmap for nonexisting areas */
  if (rect->width <= 0 || rect->height <= 0)
    return NULL;

  result = gdk_window_create_similar_surface (frame->window,
                                              CAIRO_CONTENT_COLOR,
                                              rect->width, rect->height);
  
  cr = cairo_create (result);
  cairo_translate (cr, -rect->x, -rect->y);

  setup_bg_cr (cr, frame->window, 0, 0);
  cairo_paint (cr);

  meta_frames_paint (frames, frame, cr);

  cairo_destroy (cr);

  return result;
}


static void
populate_cache (MetaFrames *frames,
                MetaUIFrame *frame)
{
  MetaFrameBorders borders;
  int width, height;
  int frame_width, frame_height, screen_width, screen_height;
  CachedPixels *pixels;
  MetaFrameType frame_type;
  MetaFrameFlags frame_flags;
  int i;

  meta_core_get (frames->xdisplay,
                 frame->xwindow,
                 META_CORE_GET_FRAME_WIDTH, &frame_width,
                 META_CORE_GET_FRAME_HEIGHT, &frame_height,
                 META_CORE_GET_SCREEN_WIDTH, &screen_width,
                 META_CORE_GET_SCREEN_HEIGHT, &screen_height,
                 META_CORE_GET_CLIENT_WIDTH, &width,
                 META_CORE_GET_CLIENT_HEIGHT, &height,
                 META_CORE_GET_FRAME_TYPE, &frame_type,
                 META_CORE_GET_FRAME_FLAGS, &frame_flags,
                 META_CORE_GET_END);

  /* don't cache extremely large windows */
  if (frame_width > 2 * screen_width ||
      frame_height > 2 * screen_height)
    {
      return;
    }
  
  meta_theme_get_frame_borders (meta_theme_get_current (),
                                frame_type,
                                frame->text_height,
                                frame_flags,
                                &borders);

  pixels = get_cache (frames, frame);

  /* Setup the rectangles for the four visible frame borders. First top, then
   * left, right and bottom. Top and bottom extend to the invisible borders
   * while left and right snugly fit in between:
   *   -----
   *   |   |
   *   -----
   */

  /* width and height refer to the client window's
   * size without any border added. */

  /* top */
  pixels->piece[0].rect.x = borders.invisible.left;
  pixels->piece[0].rect.y = borders.invisible.top;
  pixels->piece[0].rect.width = width + borders.visible.left + borders.visible.right;
  pixels->piece[0].rect.height = borders.visible.top;

  /* left */
  pixels->piece[1].rect.x = borders.invisible.left;
  pixels->piece[1].rect.y = borders.total.top;
  pixels->piece[1].rect.height = height;
  pixels->piece[1].rect.width = borders.visible.left;

  /* right */
  pixels->piece[2].rect.x = borders.total.left + width;
  pixels->piece[2].rect.y = borders.total.top;
  pixels->piece[2].rect.width = borders.visible.right;
  pixels->piece[2].rect.height = height;

  /* bottom */
  pixels->piece[3].rect.x = borders.invisible.left;
  pixels->piece[3].rect.y = borders.total.top + height;
  pixels->piece[3].rect.width = width + borders.visible.left + borders.visible.right;
  pixels->piece[3].rect.height = borders.visible.bottom;

  for (i = 0; i < 4; i++)
    {
      CachedFramePiece *piece = &pixels->piece[i];
      /* generate_pixmap() returns NULL for 0 width/height pieces, but
       * does so cheaply so we don't need to cache the NULL return */
      if (!piece->pixmap)
        piece->pixmap = generate_pixmap (frames, frame, &piece->rect);
    }
  
  if (frames->invalidate_cache_timeout_id) {
    g_source_remove (frames->invalidate_cache_timeout_id);
    frames->invalidate_cache_timeout_id = 0;
  }
  
  frames->invalidate_cache_timeout_id = g_timeout_add (1000, invalidate_cache_timeout, frames);

  if (!g_list_find (frames->invalidate_frames, frame))
    frames->invalidate_frames =
      g_list_prepend (frames->invalidate_frames, frame);
}

static void
clip_to_screen (cairo_region_t *region,
                MetaUIFrame    *frame)
{
  cairo_rectangle_int_t frame_area;
  cairo_rectangle_int_t screen_area = { 0, 0, 0, 0 };
  cairo_region_t *tmp_region;
  
  /* Chop off stuff outside the screen; this optimization
   * is crucial to handle huge client windows,
   * like "xterm -geometry 1000x1000"
   */
  meta_core_get (meta_display_get_xdisplay (meta_get_display ()),
                 frame->xwindow,
                 META_CORE_GET_FRAME_X, &frame_area.x,
                 META_CORE_GET_FRAME_Y, &frame_area.y,
                 META_CORE_GET_FRAME_WIDTH, &frame_area.width,
                 META_CORE_GET_FRAME_HEIGHT, &frame_area.height,
                 META_CORE_GET_SCREEN_WIDTH, &screen_area.width,
                 META_CORE_GET_SCREEN_HEIGHT, &screen_area.height,
                 META_CORE_GET_END);

  cairo_region_translate (region, frame_area.x, frame_area.y);

  tmp_region = cairo_region_create_rectangle (&frame_area);
  cairo_region_intersect (region, tmp_region);
  cairo_region_destroy (tmp_region);

  tmp_region = cairo_region_create_rectangle (&screen_area);
  cairo_region_intersect (region, tmp_region);
  cairo_region_destroy (tmp_region);

  cairo_region_translate (region, - frame_area.x, - frame_area.y);
}

static void
subtract_client_area (cairo_region_t *region,
                      MetaUIFrame    *frame)
{
  cairo_rectangle_int_t area;
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaFrameBorders borders;
  cairo_region_t *tmp_region;
  Display *display;

  display = meta_display_get_xdisplay (meta_get_display ());

  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_CLIENT_WIDTH, &area.width,
                 META_CORE_GET_CLIENT_HEIGHT, &area.height,
                 META_CORE_GET_END);
  meta_theme_get_frame_borders (meta_theme_get_current (),
                                type, frame->text_height, flags, 
                                &borders);

  area.x = borders.total.left;
  area.y = borders.total.top;

  tmp_region = cairo_region_create_rectangle (&area);
  cairo_region_subtract (region, tmp_region);
  cairo_region_destroy (tmp_region);
}

static void
cached_pixels_draw (CachedPixels   *pixels,
                    cairo_t        *cr,
                    cairo_region_t *region)
{
  cairo_region_t *region_piece;
  int i;

  for (i = 0; i < 4; i++)
    {
      CachedFramePiece *piece;
      piece = &pixels->piece[i];
      
      if (piece->pixmap)
        {
          cairo_set_source_surface (cr, piece->pixmap,
                                    piece->rect.x, piece->rect.y);
          cairo_paint (cr);
          
          region_piece = cairo_region_create_rectangle (&piece->rect);
          cairo_region_subtract (region, region_piece);
          cairo_region_destroy (region_piece);
        }
    }
}

static gboolean
meta_frames_draw (GtkWidget *widget,
                  cairo_t   *cr)
{
  MetaUIFrame *frame;
  MetaFrames *frames;
  CachedPixels *pixels;
  cairo_region_t *region;
  cairo_rectangle_int_t clip;
  int i, n_areas;
  cairo_surface_t *target;

  frames = META_FRAMES (widget);
  target = cairo_get_target (cr);
  gdk_cairo_get_clip_rectangle (cr, &clip);

  g_assert (cairo_surface_get_type (target) == CAIRO_SURFACE_TYPE_XLIB);
  frame = meta_frames_lookup_window (frames, cairo_xlib_surface_get_drawable (target));
  if (frame == NULL)
    return FALSE;

  populate_cache (frames, frame);

  region = cairo_region_create_rectangle (&clip);
  
  pixels = get_cache (frames, frame);

  cached_pixels_draw (pixels, cr, region);
  
  clip_to_screen (region, frame);
  subtract_client_area (region, frame);

  n_areas = cairo_region_num_rectangles (region);

  for (i = 0; i < n_areas; i++)
    {
      cairo_rectangle_int_t area;

      cairo_region_get_rectangle (region, i, &area);

      cairo_save (cr);

      cairo_rectangle (cr, area.x, area.y, area.width, area.height);
      cairo_clip (cr);

      cairo_push_group (cr);

      meta_frames_paint (frames, frame, cr);

      cairo_pop_group_to_source (cr);
      cairo_paint (cr);

      cairo_restore (cr);
    }

  cairo_region_destroy (region);
  
  return TRUE;
}

static void
meta_frames_paint (MetaFrames   *frames,
                   MetaUIFrame  *frame,
                   cairo_t      *cr)
{
  GtkWidget *widget;
  MetaFrameFlags flags;
  MetaFrameType type;
  int w, h;
  MetaButtonState button_states[META_BUTTON_TYPE_LAST];
  int i;
  int button_type = -1;
  MetaButtonLayout button_layout;
  Display *display;
  
  widget = GTK_WIDGET (frames);
  display = frame->meta_window->display->xdisplay;

  for (i = 0; i < META_BUTTON_TYPE_LAST; i++)
    button_states[i] = META_BUTTON_STATE_NORMAL;

  /* Set prelight state */
  switch (frame->prelit_control)
    {
    case META_FRAME_CONTROL_MENU:
      button_type = META_BUTTON_TYPE_MENU;
      break;
    case META_FRAME_CONTROL_MINIMIZE:
      button_type = META_BUTTON_TYPE_MINIMIZE;
      break;
    case META_FRAME_CONTROL_MAXIMIZE:
      button_type = META_BUTTON_TYPE_MAXIMIZE;
      break;
    case META_FRAME_CONTROL_UNMAXIMIZE:
      button_type = META_BUTTON_TYPE_MAXIMIZE;
      break;
    case META_FRAME_CONTROL_SHADE:
      button_type = META_BUTTON_TYPE_SHADE;
      break;
    case META_FRAME_CONTROL_UNSHADE:
      button_type = META_BUTTON_TYPE_UNSHADE;
      break;
    case META_FRAME_CONTROL_ABOVE:
      button_type = META_BUTTON_TYPE_ABOVE;
      break;
    case META_FRAME_CONTROL_UNABOVE:
      button_type = META_BUTTON_TYPE_UNABOVE;
      break;
    case META_FRAME_CONTROL_STICK:
      button_type = META_BUTTON_TYPE_STICK;
      break;
    case META_FRAME_CONTROL_UNSTICK:
      button_type = META_BUTTON_TYPE_UNSTICK;
      break;
    case META_FRAME_CONTROL_DELETE:
      button_type = META_BUTTON_TYPE_CLOSE;
      break;
    default:
      break;
    }

  if (button_type > -1)
    button_states[button_type] = frame->button_state;

  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_CLIENT_WIDTH, &w,
                 META_CORE_GET_CLIENT_HEIGHT, &h,
                 META_CORE_GET_END);

  meta_frames_ensure_layout (frames, frame);

  meta_prefs_get_button_layout (&button_layout);

  meta_theme_draw_frame_with_style (meta_theme_get_current (),
                                    frame->style,
                                    widget,
                                    cr,
                                    type,
                                    flags,
                                    w, h,
                                    frame->layout,
                                    frame->text_height,
                                    &button_layout,
                                    button_states);
}

static gboolean
meta_frames_enter_notify_event      (GtkWidget           *widget,
                                     GdkEventCrossing    *event)
{
  MetaUIFrame *frame;
  MetaFrames *frames;
  MetaFrameControl control;
  
  frames = META_FRAMES (widget);

  frames->entered = TRUE;

  frame = meta_frames_lookup_window (frames, GDK_WINDOW_XID (event->window));
  if (frame == NULL)
    return FALSE;

  control = get_control (frames, frame, event->x, event->y);
  meta_frames_update_prelit_control (frames, frame, control);
  
  return TRUE;
}

static gboolean
meta_frames_leave_notify_event      (GtkWidget           *widget,
                                     GdkEventCrossing    *event)
{
  MetaUIFrame *frame;
  MetaFrames *frames;

  frames = META_FRAMES (widget);

  frames->entered = FALSE;

  frame = meta_frames_lookup_window (frames, GDK_WINDOW_XID (event->window));
  if (frame == NULL)
    return FALSE;

  meta_frames_update_prelit_control (frames, frame, META_FRAME_CONTROL_NONE);
  
  return TRUE;
}

static GdkRectangle*
control_rect (MetaFrameControl control,
              MetaFrameGeometry *fgeom)
{
  GdkRectangle *rect;
  
  rect = NULL;
  switch (control)
    {
    case META_FRAME_CONTROL_TITLE:
      rect = &fgeom->title_rect;
      break;
    case META_FRAME_CONTROL_DELETE:
      rect = &fgeom->close_rect.visible;
      break;
    case META_FRAME_CONTROL_MENU:
      rect = &fgeom->menu_rect.visible;
      break;
    case META_FRAME_CONTROL_MINIMIZE:
      rect = &fgeom->min_rect.visible;
      break;
    case META_FRAME_CONTROL_MAXIMIZE:
    case META_FRAME_CONTROL_UNMAXIMIZE:
      rect = &fgeom->max_rect.visible;
      break;
    case META_FRAME_CONTROL_SHADE:
      rect = &fgeom->shade_rect.visible;
      break;
    case META_FRAME_CONTROL_UNSHADE:
      rect = &fgeom->unshade_rect.visible;
      break;
    case META_FRAME_CONTROL_ABOVE:
      rect = &fgeom->above_rect.visible;
      break;
    case META_FRAME_CONTROL_UNABOVE:
      rect = &fgeom->unabove_rect.visible;
      break;
    case META_FRAME_CONTROL_STICK:
      rect = &fgeom->stick_rect.visible;
      break;
    case META_FRAME_CONTROL_UNSTICK:
      rect = &fgeom->unstick_rect.visible;
      break;
    case META_FRAME_CONTROL_RESIZE_SE:
      break;
    case META_FRAME_CONTROL_RESIZE_S:
      break;
    case META_FRAME_CONTROL_RESIZE_SW:
      break;
    case META_FRAME_CONTROL_RESIZE_N:
      break;
    case META_FRAME_CONTROL_RESIZE_NE:
      break;
    case META_FRAME_CONTROL_RESIZE_NW:
      break;
    case META_FRAME_CONTROL_RESIZE_W:
      break;
    case META_FRAME_CONTROL_RESIZE_E:
      break;
    case META_FRAME_CONTROL_NONE:
      break;
    case META_FRAME_CONTROL_CLIENT_AREA:
      break;
    }

  return rect;
}

#define TOP_RESIZE_HEIGHT 4
#define CORNER_SIZE_MULT 2

static MetaFrameControl
get_control (MetaFrames *frames,
             MetaUIFrame *frame,
             int x, int y)
{
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaWindow *window;
  gboolean has_vert, has_horiz, has_left, has_right, has_bottom, has_top;
  gboolean has_north_resize;
  cairo_rectangle_int_t client;

  window = frame->meta_window;

  client = window->client_area;

  if (POINT_IN_RECT (x, y, client))
    return META_FRAME_CONTROL_CLIENT_AREA;

  if (POINT_IN_RECT (x, y, frame->fgeom.close_rect.clickable))
    return META_FRAME_CONTROL_DELETE;

  if (POINT_IN_RECT (x, y, frame->fgeom.min_rect.clickable))
    return META_FRAME_CONTROL_MINIMIZE;

  if (POINT_IN_RECT (x, y, frame->fgeom.menu_rect.clickable))
    return META_FRAME_CONTROL_MENU;

  flags = meta_frame_get_flags (window->frame);
  type = meta_window_get_frame_type (window);

  has_north_resize = (type != META_FRAME_TYPE_ATTACHED);
  has_vert = (flags & META_FRAME_ALLOWS_VERTICAL_RESIZE) != 0;
  has_horiz = (flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE) != 0;
  has_top = (flags & META_FRAME_ALLOWS_TOP_RESIZE) != 0;
  has_left = (flags & META_FRAME_ALLOWS_LEFT_RESIZE) != 0;
  has_right = (flags & META_FRAME_ALLOWS_RIGHT_RESIZE) != 0;
  has_bottom = (flags & META_FRAME_ALLOWS_BOTTOM_RESIZE) != 0;

  if (POINT_IN_RECT (x, y, frame->fgeom.title_rect))
    {
      if (has_vert && y <= TOP_RESIZE_HEIGHT && has_north_resize)
        return META_FRAME_CONTROL_RESIZE_N;
      else
        return META_FRAME_CONTROL_TITLE;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.max_rect.clickable))
    {
      if (flags & META_FRAME_MAXIMIZED &&
          (META_WINDOW_TILED_TOP (window) ||
           META_WINDOW_TILED_BOTTOM (window)))
        return META_FRAME_CONTROL_MAXIMIZE;

      if (flags & META_FRAME_MAXIMIZED)
        return META_FRAME_CONTROL_UNMAXIMIZE;
      else
        return META_FRAME_CONTROL_MAXIMIZE;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.shade_rect.clickable))
    {
      return META_FRAME_CONTROL_SHADE;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.unshade_rect.clickable))
    {
      return META_FRAME_CONTROL_UNSHADE;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.above_rect.clickable))
    {
      return META_FRAME_CONTROL_ABOVE;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.unabove_rect.clickable))
    {
      return META_FRAME_CONTROL_UNABOVE;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.stick_rect.clickable))
    {
      return META_FRAME_CONTROL_STICK;
    }

  if (POINT_IN_RECT (x, y, frame->fgeom.unstick_rect.clickable))
    {
      return META_FRAME_CONTROL_UNSTICK;
    }

  /* South resize always has priority over north resize,
   * in case of overlap.
   */

  if (y >= (frame->fgeom.height - frame->fgeom.borders.total.bottom * CORNER_SIZE_MULT) &&
      x >= (frame->fgeom.width - frame->fgeom.borders.total.right * CORNER_SIZE_MULT))
    {
      if ((has_vert && has_horiz) || (has_bottom && has_right))
        return META_FRAME_CONTROL_RESIZE_SE;
      else if (has_bottom)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_right)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y >= (frame->fgeom.height - frame->fgeom.borders.total.bottom * CORNER_SIZE_MULT) &&
           x <= frame->fgeom.borders.total.left * CORNER_SIZE_MULT)
    {
      if ((has_vert && has_horiz) || (has_bottom && has_left))
        return META_FRAME_CONTROL_RESIZE_SW;
      else if (has_bottom)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_left)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (frame->fgeom.borders.invisible.top * CORNER_SIZE_MULT) &&
           (x <= frame->fgeom.borders.total.left * CORNER_SIZE_MULT) && has_north_resize)
    {
      if ((has_vert && has_horiz) || (has_top && has_left))
        return META_FRAME_CONTROL_RESIZE_NW;
      else if (has_top)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_left)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (frame->fgeom.borders.invisible.top * CORNER_SIZE_MULT) &&
           x >= (frame->fgeom.width - frame->fgeom.borders.total.right  * CORNER_SIZE_MULT) && has_north_resize)
    {
      if ((has_vert && has_horiz) || (has_top && has_right))
        return META_FRAME_CONTROL_RESIZE_NE;
      else if (has_top)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_right)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y < (frame->fgeom.borders.invisible.top + TOP_RESIZE_HEIGHT))
    {
      if (has_top && has_north_resize)
        return META_FRAME_CONTROL_RESIZE_N;
    }
  else if (y >= (frame->fgeom.height - frame->fgeom.borders.total.bottom))
    {
      if (has_bottom)
        return META_FRAME_CONTROL_RESIZE_S;
    }
  else if (x <= frame->fgeom.borders.total.left)
    {
      if (has_left)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (x >= (frame->fgeom.width - frame->fgeom.borders.total.right))
    {
      if (has_right)
        return META_FRAME_CONTROL_RESIZE_E;
    }

  if (y >= frame->fgeom.borders.total.top)
    return META_FRAME_CONTROL_NONE;
  else
    return META_FRAME_CONTROL_TITLE;
}

static void
invalidate_whole_window (MetaFrames *frames,
                         MetaUIFrame *frame)
{
  gdk_window_invalidate_rect (frame->window, NULL, FALSE);
  invalidate_cache (frames, frame);
}
