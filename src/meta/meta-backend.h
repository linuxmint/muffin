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

#ifndef META_BACKEND_H
#define META_BACKEND_H

#include <glib-object.h>

#include "clutter/clutter.h"
#include "meta/meta-dnd.h"
#include "meta/meta-remote-access-controller.h"

#define META_TYPE_BACKEND (meta_backend_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaBackend, meta_backend, META, BACKEND, GObject)

META_EXPORT
MetaBackend * meta_get_backend (void);

META_EXPORT
void meta_backend_set_keymap (MetaBackend *backend,
                              const char  *layouts,
                              const char  *variants,
                              const char  *options);

META_EXPORT
void meta_backend_lock_layout_group (MetaBackend *backend,
                                     guint        idx);

META_EXPORT
void meta_backend_set_numlock (MetaBackend *backend,
                               gboolean     numlock_state);

META_EXPORT
ClutterActor *meta_backend_get_stage (MetaBackend *backend);

META_EXPORT
MetaDnd      *meta_backend_get_dnd   (MetaBackend *backend);

META_EXPORT
MetaSettings *meta_backend_get_settings (MetaBackend *backend);

META_EXPORT
MetaRemoteAccessController * meta_backend_get_remote_access_controller (MetaBackend *backend);

META_EXPORT
gboolean meta_backend_is_rendering_hardware_accelerated (MetaBackend *backend);

META_EXPORT
void meta_clutter_init (void);

#endif /* META_BACKEND_H */
