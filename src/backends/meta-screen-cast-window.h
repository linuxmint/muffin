/*
 * Copyright (C) 2018 Red Hat Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#ifndef META_SCREEN_CAST_WINDOW_H
#define META_SCREEN_CAST_WINDOW_H

#include <stdint.h>
#include <glib-object.h>

#include "backends/meta-cursor.h"
#include "meta/boxes.h"

G_BEGIN_DECLS

#define META_TYPE_SCREEN_CAST_WINDOW (meta_screen_cast_window_get_type ())
G_DECLARE_INTERFACE (MetaScreenCastWindow, meta_screen_cast_window,
                     META, SCREEN_CAST_WINDOW, GObject)

struct _MetaScreenCastWindowInterface
{
  GTypeInterface parent_iface;

  void (*get_buffer_bounds) (MetaScreenCastWindow *screen_cast_window,
                             MetaRectangle        *bounds);

  void (*transform_relative_position) (MetaScreenCastWindow *screen_cast_window,
                                       double                x,
                                       double                y,
                                       double               *x_out,
                                       double               *y_out);

  gboolean (*transform_cursor_position) (MetaScreenCastWindow *screen_cast_window,
                                         MetaCursorSprite     *cursor_sprite,
                                         graphene_point_t     *cursor_position,
                                         float                *out_cursor_scale,
                                         graphene_point_t     *out_relative_cursor_position);

  void (*capture_into) (MetaScreenCastWindow *screen_cast_window,
                        MetaRectangle        *bounds,
                        uint8_t              *data);

  gboolean (*blit_to_framebuffer) (MetaScreenCastWindow *screen_cast_window,
                                   MetaRectangle        *bounds,
                                   CoglFramebuffer      *framebuffer);

  gboolean (*has_damage) (MetaScreenCastWindow *screen_cast_window);
};

void meta_screen_cast_window_get_buffer_bounds (MetaScreenCastWindow *screen_cast_window,
                                                MetaRectangle        *bounds);

void meta_screen_cast_window_transform_relative_position (MetaScreenCastWindow *screen_cast_window,
                                                          double                x,
                                                          double                y,
                                                          double               *x_out,
                                                          double               *y_out);

gboolean meta_screen_cast_window_transform_cursor_position (MetaScreenCastWindow *screen_cast_window,
                                                            MetaCursorSprite     *cursor_sprite,
                                                            graphene_point_t     *cursor_position,
                                                            float                *out_cursor_scale,
                                                            graphene_point_t     *out_relative_cursor_position);

void meta_screen_cast_window_capture_into (MetaScreenCastWindow *screen_cast_window,
                                           MetaRectangle        *bounds,
                                           uint8_t              *data);

gboolean meta_screen_cast_window_blit_to_framebuffer (MetaScreenCastWindow *screen_cast_window,
                                                      MetaRectangle        *bounds,
                                                      CoglFramebuffer      *framebuffer);

gboolean meta_screen_cast_window_has_damage (MetaScreenCastWindow *screen_cast_window);

G_END_DECLS

#endif /* META_SCREEN_CAST_WINDOW_H */
