/*
 * Copyright (C) 2021 Red Hat, Inc.
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
 */

#ifndef META_WAYLAND_TRANSACTION_H
#define META_WAYLAND_TRANSACTION_H

#include "wayland/meta-wayland-types.h"
#include "wayland/meta-wayland-subsurface.h"

void meta_wayland_transaction_commit (MetaWaylandTransaction *transaction);

MetaWaylandTransactionEntry *meta_wayland_transaction_ensure_entry (MetaWaylandTransaction *transaction,
                                                                    MetaWaylandSurface     *surface);

void meta_wayland_transaction_add_placement_op (MetaWaylandTransaction           *transaction,
                                                MetaWaylandSurface               *surface,
                                                MetaWaylandSubsurfacePlacementOp *op);

void meta_wayland_transaction_add_subsurface_position (MetaWaylandTransaction *transaction,
                                                       MetaWaylandSurface     *surface,
                                                       int                     x,
                                                       int                     y);

void meta_wayland_transaction_add_xdg_popup_reposition (MetaWaylandTransaction *transaction,
                                                        MetaWaylandSurface     *surface,
                                                        void                   *xdg_positioner,
                                                        uint32_t               token);

void meta_wayland_transaction_merge_into (MetaWaylandTransaction *from,
                                          MetaWaylandTransaction *to);

void meta_wayland_transaction_merge_pending_state (MetaWaylandTransaction *transaction,
                                                   MetaWaylandSurface *surface);

MetaWaylandTransaction *meta_wayland_transaction_new (MetaWaylandCompositor *compositor);

void meta_wayland_transaction_free (MetaWaylandTransaction *transaction);

void meta_wayland_transaction_finalize (MetaWaylandCompositor *compositor);

void meta_wayland_transaction_init (MetaWaylandCompositor *compositor);

#endif
