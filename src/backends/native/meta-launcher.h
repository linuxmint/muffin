/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#ifndef META_LAUNCHER_H
#define META_LAUNCHER_H

#include <glib-object.h>

typedef struct _MetaLauncher MetaLauncher;

MetaLauncher     *meta_launcher_new                     (GError       **error);
void              meta_launcher_free                    (MetaLauncher  *self);

gboolean          meta_launcher_activate_session        (MetaLauncher  *self,
                                                         GError       **error);

gboolean          meta_launcher_activate_vt             (MetaLauncher  *self,
                                                         signed char    vt,
                                                         GError       **error);

const char *      meta_launcher_get_seat_id             (MetaLauncher *launcher);

int               meta_launcher_open_restricted         (MetaLauncher *launcher,
                                                         const char   *path,
                                                         GError      **error);

void              meta_launcher_close_restricted        (MetaLauncher *launcher,
                                                         int           fd);


#endif /* META_LAUNCHER_H */
