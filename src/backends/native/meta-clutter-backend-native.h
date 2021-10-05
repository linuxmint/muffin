/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_CLUTTER_BACKEND_NATIVE_H
#define META_CLUTTER_BACKEND_NATIVE_H

#include <glib-object.h>

#include "backends/native/meta-stage-native.h"
#include "clutter/clutter.h"
#include "clutter/egl/clutter-backend-eglnative.h"

#define META_TYPE_CLUTTER_BACKEND_NATIVE (meta_clutter_backend_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaClutterBackendNative, meta_clutter_backend_native,
                      META, CLUTTER_BACKEND_NATIVE,
                      ClutterBackendEglNative)

MetaStageNative * meta_clutter_backend_native_get_stage_native (ClutterBackend *backend);

void meta_clutter_backend_native_set_seat_id (const gchar *seat_id);

#endif /* META_CLUTTER_BACKEND_NATIVE_H */
