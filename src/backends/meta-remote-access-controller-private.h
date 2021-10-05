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

#ifndef META_REMOTE_ACCESS_CONTROLLER_PRIVATE_H
#define META_REMOTE_ACCESS_CONTROLLER_PRIVATE_H

#include "meta/meta-remote-access-controller.h"

void meta_remote_access_controller_notify_new_handle (MetaRemoteAccessController *controller,
                                                      MetaRemoteAccessHandle     *handle);

void meta_remote_access_handle_notify_stopped (MetaRemoteAccessHandle *handle);

void meta_remote_access_handle_set_disable_animations (MetaRemoteAccessHandle *handle,
                                                       gboolean                disable_animations);

#endif /* META_REMOTE_ACCESS_CONTROLLER_PRIVATE_H */
