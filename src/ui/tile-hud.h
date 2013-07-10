/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Meta tile preview */

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
#ifndef META_TILE_HUD_H
#define META_TILE_HUD_H

#include <meta/boxes.h>

typedef struct _MetaTileHUD MetaTileHUD;

typedef enum {
    HUD_CAN_TILE_SIDE_BY_SIDE    = 1 << 1,
    HUD_CAN_TILE_TOP_BOTTOM      = 1 << 2,
    HUD_CAN_TILE_CORNER          = 1 << 3
} HUDTileRestrictions;

MetaTileHUD       *meta_tile_hud_new         (int                 screen_number);
void               meta_tile_hud_free        (MetaTileHUD        *hud);
void               meta_tile_hud_show        (MetaTileHUD        *hud,
                                              MetaRectangle      *rect,
                                              float               opacity,
                                              gboolean            snap,
                                              HUDTileRestrictions restrictions,
                                              guint               current_proximity_zone);
void               meta_tile_hud_hide        (MetaTileHUD        *hud);
void               meta_tile_hud_fade_out    (MetaTileHUD        *hud,
                                              float               opacity,
                                              gboolean            snap);
Window             meta_tile_hud_get_xwindow (MetaTileHUD        *hud,
                                              gulong             *create_serial);
gboolean           meta_tile_hud_get_visible (MetaTileHUD        *hud);

#endif /* META_TILE_HUD_H */
