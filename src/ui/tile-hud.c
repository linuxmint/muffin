/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin tile-hud marks the area a window will *ehm* snap to */

/*
 * Copyright (C) 2010 Florian Müllner
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

#include <gtk/gtk.h>
#include <cairo.h>

#include "tile-hud.h"
#include "display-private.h"
#include "window-private.h"
#include "boxes-private.h"
#include <math.h>

#include "core.h"

struct _MetaTileHUD {
  GtkWidget     *hud_window;
  gulong         create_serial;

  gboolean       snap_state;

  MetaRectangle  abs_work_area;
  MetaRectangle  relative_work_area;

  GdkRGBA       *tile_color;
  GdkRGBA       *snap_color;
  GdkRGBA       *border_color;
  GdkRGBA       *current_color;

  GdkRGBA       invis;

  float opacity;

  guint  current_proximity_zone;
  guint  current_tile_zone;

  HUDTileRestrictions restrictions;
};

enum {
    TOP    = 1 << 0,
    BOTTOM = 1 << 1,
    LEFT   = 1 << 2,
    RIGHT  = 1 << 3
};

#define _270_DEG 270.0 * (M_PI/180.0)
#define _180_DEG 180.0 * (M_PI/180.0)
#define  _90_DEG  90.0 * (M_PI/180.0)

// TODO: These should be set in the GTK theme, along with colors
#define R 4
#define OFFSET 0

static void
update_color (MetaTileHUD *hud, cairo_pattern_t *pat)
{
    hud->snap_color->alpha = hud->opacity;
    hud->tile_color->alpha = hud->opacity;
    hud->border_color->alpha = hud->opacity;
    hud->current_color = hud->snap_state ? hud->snap_color :
                                           hud->tile_color;
    cairo_pattern_add_color_stop_rgba(pat, 0.0, hud->current_color->red,
                                                hud->current_color->green,
                                                hud->current_color->blue,
                                                hud->opacity * 0.6);
    cairo_pattern_add_color_stop_rgba(pat, 0.4, hud->current_color->red,
                                                hud->current_color->green,
                                                hud->current_color->blue,
                                                hud->opacity * 0.4);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, hud->current_color->red,
                                                hud->current_color->green,
                                                hud->current_color->blue,
                                                hud->opacity * 0.2);
}

static void
do_side_box (MetaTileHUD *hud, cairo_t *cr, gint side, MetaRectangle rect)
{
  cairo_pattern_t *pat;
  cairo_save (cr);
  switch (side) {
    case LEFT:
        pat = cairo_pattern_create_linear(BOX_LEFT (rect), BOX_TOP (rect), BOX_RIGHT (rect), BOX_TOP (rect));
        update_color (hud, pat);
        cairo_move_to (cr, rect.x, rect.y + OFFSET);

        cairo_line_to (cr, BOX_RIGHT (rect) - R, BOX_TOP (rect) + OFFSET);
        cairo_arc (cr, BOX_RIGHT (rect) - R, BOX_TOP (rect) + R + OFFSET, R, _270_DEG, 0);
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_BOTTOM (rect) - R - OFFSET);
        cairo_arc (cr, BOX_RIGHT (rect) - R, BOX_BOTTOM (rect) - R - OFFSET, R, 0, _90_DEG);
        cairo_line_to (cr, BOX_LEFT (rect), BOX_BOTTOM (rect) - OFFSET);
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
    case RIGHT:
        pat = cairo_pattern_create_linear(BOX_RIGHT (rect), BOX_TOP (rect), BOX_LEFT (rect), BOX_TOP (rect));
        update_color (hud, pat);
        cairo_move_to (cr, BOX_RIGHT (rect), rect.y + OFFSET);
        cairo_line_to (cr, BOX_LEFT (rect) + R, BOX_TOP (rect) + OFFSET);
        cairo_arc_negative (cr, BOX_LEFT (rect) + R, BOX_TOP (rect) + R + OFFSET, R, _270_DEG, _180_DEG);
        cairo_line_to (cr, BOX_LEFT (rect), BOX_BOTTOM (rect) - R - OFFSET);
        cairo_arc_negative (cr, BOX_LEFT (rect) + R, BOX_BOTTOM (rect) - R - OFFSET, R, _180_DEG, _90_DEG);
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_BOTTOM (rect) - OFFSET);
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
    case TOP:
        pat = cairo_pattern_create_linear(BOX_LEFT (rect), BOX_TOP (rect), BOX_LEFT (rect), BOX_BOTTOM (rect));
        update_color (hud, pat);
        cairo_move_to (cr, rect.x + OFFSET, rect.y);
        cairo_line_to (cr, BOX_LEFT (rect) + OFFSET, BOX_BOTTOM (rect) - R);
        cairo_arc_negative (cr, BOX_LEFT (rect) + OFFSET + R, BOX_BOTTOM (rect) - R, R, _180_DEG, _90_DEG);
        cairo_line_to (cr, BOX_RIGHT (rect) - OFFSET - R, BOX_BOTTOM (rect));
        cairo_arc_negative (cr, BOX_RIGHT (rect) - OFFSET - R, BOX_BOTTOM (rect) - R, R, _90_DEG, 0);
        cairo_line_to (cr, BOX_RIGHT (rect) - OFFSET, BOX_TOP (rect));
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
    case BOTTOM:
        pat = cairo_pattern_create_linear(BOX_LEFT (rect), BOX_BOTTOM (rect), BOX_LEFT (rect), BOX_TOP (rect));
        update_color (hud, pat);
        cairo_move_to (cr, rect.x + OFFSET, BOX_BOTTOM (rect));
        cairo_line_to (cr, BOX_LEFT (rect) + OFFSET, BOX_TOP (rect) + R);
        cairo_arc (cr, BOX_LEFT (rect) + OFFSET + R, BOX_TOP (rect) + R, R, _180_DEG, _270_DEG);
        cairo_line_to (cr, BOX_RIGHT (rect) - OFFSET - R, BOX_TOP (rect));
        cairo_arc (cr, BOX_RIGHT (rect) - OFFSET - R, BOX_TOP (rect) + R, R, _270_DEG, 0);
        cairo_line_to (cr, BOX_RIGHT (rect) - OFFSET, BOX_BOTTOM (rect));
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
  }
  cairo_restore (cr);
}

static void
do_corner_box (MetaTileHUD *hud, cairo_t *cr, gint side, MetaRectangle rect)
{
  cairo_pattern_t *pat;
  cairo_save (cr);
  switch (side) {
    case LEFT | TOP:
        pat = cairo_pattern_create_linear(BOX_LEFT (rect), BOX_TOP (rect), BOX_RIGHT (rect), BOX_BOTTOM (rect));
        update_color (hud, pat);
        cairo_move_to (cr, BOX_LEFT (rect), BOX_TOP (rect));
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_TOP (rect));
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_BOTTOM (rect) - R);
        cairo_arc (cr, BOX_RIGHT (rect) - R, BOX_BOTTOM (rect) - R, R, 0, _90_DEG);
        cairo_line_to (cr, BOX_LEFT (rect), BOX_BOTTOM (rect));
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
    case RIGHT | TOP:
        pat = cairo_pattern_create_linear(BOX_RIGHT (rect), BOX_TOP (rect), BOX_LEFT (rect), BOX_BOTTOM (rect));
        update_color (hud, pat);
        cairo_move_to (cr, BOX_RIGHT (rect), BOX_TOP (rect));
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_BOTTOM (rect));
        cairo_line_to (cr, BOX_LEFT (rect) + R, BOX_BOTTOM (rect));
        cairo_arc (cr, BOX_LEFT (rect) + R, BOX_BOTTOM (rect) - R, R, _90_DEG, _180_DEG);
        cairo_line_to (cr, BOX_LEFT (rect), BOX_TOP (rect));
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
    case RIGHT | BOTTOM:
        pat = cairo_pattern_create_linear(BOX_RIGHT (rect), BOX_BOTTOM (rect), BOX_LEFT (rect), BOX_TOP (rect));
        update_color (hud, pat);
        cairo_move_to (cr, BOX_RIGHT (rect), BOX_BOTTOM (rect));
        cairo_line_to (cr, BOX_LEFT (rect), BOX_BOTTOM (rect));
        cairo_line_to (cr, BOX_LEFT (rect), BOX_TOP (rect) + R);
        cairo_arc (cr, BOX_LEFT (rect) + R, BOX_TOP (rect) + R, R, _180_DEG, _270_DEG);
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_TOP (rect));
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
    case LEFT | BOTTOM:
        pat = cairo_pattern_create_linear(BOX_LEFT (rect), BOX_BOTTOM (rect), BOX_RIGHT (rect), BOX_TOP (rect));
        update_color (hud, pat);
        cairo_move_to (cr, BOX_LEFT (rect), BOX_BOTTOM (rect));
        cairo_line_to (cr, BOX_LEFT (rect), BOX_TOP (rect));
        cairo_line_to (cr, BOX_RIGHT (rect) - R, BOX_TOP (rect));
        cairo_arc (cr, BOX_RIGHT (rect) - R, BOX_TOP (rect) + R, R, _270_DEG, 0);
        cairo_line_to (cr, BOX_RIGHT (rect), BOX_BOTTOM (rect));
        cairo_close_path (cr);
        cairo_clip_preserve (cr);
        cairo_set_source (cr, pat);
        cairo_fill_preserve (cr);
        gdk_cairo_set_source_rgba (cr, hud->border_color);
        cairo_stroke (cr);
        cairo_pattern_destroy(pat);
        break;
  }
  cairo_restore (cr);
}

static gboolean
meta_tile_hud_draw (GtkWidget *widget,
                        cairo_t   *cr,
                        gpointer   user_data)
{
  MetaTileHUD *hud = user_data;
  cairo_set_line_width (cr, 1.0);

  // /* Fill the hud area with a transparent color */
  gdk_cairo_set_source_rgba (cr, &hud->invis);

  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  MetaRectangle tile_area;

  if (hud->restrictions & HUD_CAN_TILE_SIDE_BY_SIDE)
      {
      // LEFT
      if (hud->current_proximity_zone == ZONE_2) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_LEFT, &tile_area, HUD_WIDTH);
          tile_area.y += OFFSET;
          tile_area.height -= 2 * OFFSET;
          do_side_box (hud, cr, LEFT, tile_area);
      }
      // RIGHT
      if (hud->current_proximity_zone == ZONE_3) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_RIGHT, &tile_area, HUD_WIDTH);
          tile_area.y += OFFSET;
          tile_area.height -= 2 * OFFSET;
          do_side_box (hud, cr, RIGHT, tile_area);
      }
  }

  if (hud->restrictions & HUD_CAN_TILE_CORNER) {
      // ULC
      if (hud->current_proximity_zone == ZONE_4) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_ULC, &tile_area, HUD_WIDTH);
          do_corner_box (hud, cr, LEFT | TOP, tile_area);
      }
      // LLC
      if (hud->current_proximity_zone == ZONE_7) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_LLC, &tile_area, HUD_WIDTH);
          do_corner_box (hud, cr, LEFT | BOTTOM, tile_area);
      }
      // URC
      if (hud->current_proximity_zone == ZONE_5) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_URC, &tile_area, HUD_WIDTH);
          do_corner_box (hud, cr, RIGHT | TOP, tile_area);
      }
      // LRC
      if (hud->current_proximity_zone == ZONE_6) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_LRC, &tile_area, HUD_WIDTH);
          do_corner_box (hud, cr, RIGHT | BOTTOM, tile_area);
      }
  }
  if (hud->restrictions & HUD_CAN_TILE_TOP_BOTTOM) {
      // TOP
      if (hud->current_proximity_zone == ZONE_0) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_TOP, &tile_area, HUD_WIDTH);
          tile_area.x += OFFSET;
          tile_area.width -= 2 * OFFSET;
          do_side_box (hud, cr, TOP, tile_area);
      }
      // BOTTOM
      if (hud->current_proximity_zone == ZONE_1) {
          meta_window_get_tile_threshold_area_for_mode (NULL, hud->relative_work_area, META_TILE_BOTTOM, &tile_area, HUD_WIDTH);
          tile_area.x += OFFSET;
          tile_area.width -= 2 * OFFSET;
          do_side_box (hud, cr, BOTTOM, tile_area);
      }
  }
  return FALSE;
}

LOCAL_SYMBOL MetaTileHUD *
meta_tile_hud_new (int            screen_number)
{
  MetaTileHUD *hud;
  GdkScreen *screen;
  GtkStyleContext *context;
  GtkWidgetPath *path;

  screen = gdk_display_get_screen (gdk_display_get_default (), screen_number);

  hud = g_new (MetaTileHUD, 1);

  hud->hud_window = gtk_window_new (GTK_WINDOW_POPUP);

  hud->snap_state = FALSE;

  hud->restrictions = 0;

  hud->tile_color = NULL;
  hud->snap_color = NULL;
  hud->current_color = NULL;

  hud->current_proximity_zone = ZONE_NONE;

  gtk_window_set_screen (GTK_WINDOW (hud->hud_window), screen);
  gtk_widget_set_app_paintable (hud->hud_window, TRUE);

  hud->relative_work_area.x = hud->relative_work_area.y = 0;
  hud->abs_work_area.x = hud->abs_work_area.y = 0;
  hud->relative_work_area.width = hud->relative_work_area.height = 0;
  hud->abs_work_area.width = hud->abs_work_area.height = 0;

  gtk_widget_set_visual (hud->hud_window,
                         gdk_screen_get_rgba_visual (screen));

  path = gtk_widget_path_new ();
  gtk_widget_path_append_type (path, GTK_TYPE_ICON_VIEW);

  context = gtk_style_context_new ();
  gtk_style_context_set_path (context, path);
  gtk_style_context_add_class (context,
                               GTK_STYLE_CLASS_RUBBERBAND);

  gtk_widget_path_free (path);

  gtk_style_context_get (context, GTK_STATE_FLAG_SELECTED,
                         "background-color", &hud->snap_color,
                         NULL);

  gtk_style_context_add_class (context,
                               "gtkstyle-fallback");

  gtk_style_context_get (context, GTK_STATE_FLAG_NORMAL,
                         "color", &hud->tile_color,
                         NULL);

  gtk_style_context_get (context, GTK_STATE_FLAG_NORMAL,
                         "color", &hud->border_color,
                         NULL);

  hud->border_color->alpha = 1.0;
  hud->snap_color->alpha = 1.0;
  hud->tile_color->alpha = 1.0;

  g_object_unref (context);

  hud->create_serial = XNextRequest (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
  gtk_widget_realize (hud->hud_window);
  g_signal_connect (hud->hud_window, "draw",
                    G_CALLBACK (meta_tile_hud_draw), hud);

  gdk_rgba_parse (&hud->invis, "#000000");
  hud->invis.alpha = 0.0;
  
  hud->opacity = 0.0;
  return hud;
}

LOCAL_SYMBOL void
meta_tile_hud_free (MetaTileHUD *hud)
{
  gtk_widget_destroy (hud->hud_window);

  if (hud->tile_color)
    gdk_rgba_free (hud->tile_color);
  if (hud->snap_color)
    gdk_rgba_free (hud->snap_color);
  if (hud->border_color)
    gdk_rgba_free (hud->border_color);

  g_free (hud);
}

LOCAL_SYMBOL void
meta_tile_hud_show (MetaTileHUD        *hud,
                    MetaRectangle      *rect,
                    float               opacity,
                    gboolean            snap,
                    HUDTileRestrictions restrictions,
                    guint               current_proximity_zone)
{
  GdkRectangle old_rect;
  GdkWindow *window;

  if (hud->snap_state != snap) {
        hud->snap_state = snap;
        hud->current_color = snap ? hud->snap_color :
                                    hud->tile_color;
  }

  hud->current_proximity_zone = current_proximity_zone;
  hud->restrictions = restrictions;

  gtk_widget_show (hud->hud_window);
  window = gtk_widget_get_window (hud->hud_window);

  meta_core_lower_beneath_grab_window (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                       GDK_WINDOW_XID (window),
                                       gtk_get_current_event_time ());

  hud->opacity = opacity;

  old_rect.x = hud->abs_work_area.x;
  old_rect.y = 0;
  old_rect.width = hud->abs_work_area.width;
  old_rect.height = hud->abs_work_area.height;

  gdk_window_invalidate_rect (window, &old_rect, FALSE);

  hud->abs_work_area = *rect;
  hud->relative_work_area.x = hud->relative_work_area.y = 0;
  hud->relative_work_area.height = hud->abs_work_area.height;
  hud->relative_work_area.width = hud->abs_work_area.width;

  gdk_window_move_resize (window,
                          hud->abs_work_area.x, hud->abs_work_area.y,
                          hud->abs_work_area.width, hud->abs_work_area.height);
}

LOCAL_SYMBOL void
meta_tile_hud_hide (MetaTileHUD *hud)
{
  gtk_widget_hide (hud->hud_window);
}

LOCAL_SYMBOL void
meta_tile_hud_fade_out (MetaTileHUD *hud, float opacity, gboolean snap)
{
  hud->opacity = opacity;

  GdkRectangle old_rect;
  GdkWindow *window;

  window = gtk_widget_get_window (hud->hud_window);

  old_rect.x = hud->abs_work_area.x;
  old_rect.y = 0;
  old_rect.width = hud->abs_work_area.width;
  old_rect.height = hud->abs_work_area.height;

  gdk_window_invalidate_rect (window, &old_rect, FALSE);

  gdk_window_move_resize (window,
                          hud->abs_work_area.x, hud->abs_work_area.y,
                          hud->abs_work_area.width, hud->abs_work_area.height);

  if (opacity <= 0.0)
    meta_tile_hud_hide (hud);
}

LOCAL_SYMBOL gboolean
meta_tile_hud_get_visible (MetaTileHUD *hud)
{
    return gtk_widget_get_visible (hud->hud_window);
}

LOCAL_SYMBOL Window
meta_tile_hud_get_xwindow (MetaTileHUD *hud,
                               gulong          *create_serial)
{
  GdkWindow *window = gtk_widget_get_window (hud->hud_window);

  if (create_serial)
    *create_serial = hud->create_serial;

  return GDK_WINDOW_XID (window);
}
