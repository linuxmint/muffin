/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Edge resistance for move/resize operations */

/* 
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

#ifndef META_EDGE_RESISTANCE_H
#define META_EDGE_RESISTANCE_H

#include "window-private.h"

void        meta_window_edge_resistance_for_move   (MetaWindow  *window,
                                                    int          old_x,
                                                    int          old_y,
                                                    int         *new_x,
                                                    int         *new_y,
                                                    GSourceFunc  timeout_func,
                                                    gboolean     snap,
                                                    gboolean     is_keyboard_op);
void        meta_window_edge_resistance_for_resize (MetaWindow  *window,
                                                    int          old_width,
                                                    int          old_height,
                                                    int         *new_width,
                                                    int         *new_height,
                                                    int          gravity,
                                                    GSourceFunc  timeout_func,
                                                    gboolean     snap,
                                                    gboolean     is_keyboard_op);

#endif /* META_EDGE_RESISTANCE_H */

