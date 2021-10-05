/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2018 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_INPUT_MAPPER_H
#define META_INPUT_MAPPER_H

#include <clutter/clutter.h>
#include "meta-monitor-manager-private.h"

#define META_TYPE_INPUT_MAPPER (meta_input_mapper_get_type ())

G_DECLARE_FINAL_TYPE (MetaInputMapper, meta_input_mapper,
		      META, INPUT_MAPPER, GObject)

MetaInputMapper * meta_input_mapper_new      (void);

void meta_input_mapper_add_device    (MetaInputMapper    *mapper,
				      ClutterInputDevice *device,
                                      gboolean            builtin);
void meta_input_mapper_remove_device (MetaInputMapper    *mapper,
				      ClutterInputDevice *device);

ClutterInputDevice *
meta_input_mapper_get_logical_monitor_device (MetaInputMapper        *mapper,
                                              MetaLogicalMonitor     *logical_monitor,
                                              ClutterInputDeviceType  device_type);
MetaLogicalMonitor *
meta_input_mapper_get_device_logical_monitor (MetaInputMapper *mapper,
                                              ClutterInputDevice *device);

#endif /* META_INPUT_MAPPER_H */
