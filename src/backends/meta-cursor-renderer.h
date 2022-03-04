/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef META_CURSOR_RENDERER_H
#define META_CURSOR_RENDERER_H

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-cursor.h"

#define META_TYPE_HW_CURSOR_INHIBITOR (meta_hw_cursor_inhibitor_get_type ())
G_DECLARE_INTERFACE (MetaHwCursorInhibitor, meta_hw_cursor_inhibitor,
                     META, HW_CURSOR_INHIBITOR, GObject)

struct _MetaHwCursorInhibitorInterface
{
  GTypeInterface parent_iface;

  gboolean (* is_cursor_sprite_inhibited) (MetaHwCursorInhibitor *inhibitor,
                                           MetaCursorSprite      *cursor_sprite);
};

#define META_TYPE_CURSOR_RENDERER (meta_cursor_renderer_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaCursorRenderer, meta_cursor_renderer,
                          META, CURSOR_RENDERER, GObject);

struct _MetaCursorRendererClass
{
  GObjectClass parent_class;

  gboolean (* update_cursor) (MetaCursorRenderer *renderer,
                              MetaCursorSprite   *cursor_sprite);
};

MetaCursorRenderer * meta_cursor_renderer_new (void);

void meta_cursor_renderer_set_cursor (MetaCursorRenderer *renderer,
                                      MetaCursorSprite   *cursor_sprite);

void meta_cursor_renderer_set_position (MetaCursorRenderer *renderer,
                                        float               x,
                                        float               y);
graphene_point_t meta_cursor_renderer_get_position (MetaCursorRenderer *renderer);
void meta_cursor_renderer_force_update (MetaCursorRenderer *renderer);

MetaCursorSprite * meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer);

void meta_cursor_renderer_add_hw_cursor_inhibitor (MetaCursorRenderer    *renderer,
                                                   MetaHwCursorInhibitor *inhibitor);

void meta_cursor_renderer_remove_hw_cursor_inhibitor (MetaCursorRenderer    *renderer,
                                                      MetaHwCursorInhibitor *inhibitor);

gboolean meta_cursor_renderer_is_hw_cursors_inhibited (MetaCursorRenderer *renderer,
                                                       MetaCursorSprite   *cursor_sprite);

graphene_rect_t meta_cursor_renderer_calculate_rect (MetaCursorRenderer *renderer,
                                                     MetaCursorSprite   *cursor_sprite);

void meta_cursor_renderer_emit_painted (MetaCursorRenderer *renderer,
                                        MetaCursorSprite   *cursor_sprite);

#endif /* META_CURSOR_RENDERER_H */
