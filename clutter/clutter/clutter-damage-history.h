/*
 * Copyright (C) 2007,2008,2009,2010,2011  Intel Corporation.
 * Copyright (C) 2020 Red Hat Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CLUTTER_DAMAGE_HISTORY_H
#define CLUTTER_DAMAGE_HISTORY_H

#include <cairo.h>
#include <glib.h>

typedef struct _ClutterDamageHistory ClutterDamageHistory;

ClutterDamageHistory * clutter_damage_history_new (void);

void clutter_damage_history_free (ClutterDamageHistory *history);

gboolean clutter_damage_history_is_age_valid (ClutterDamageHistory *history,
                                              int                   age);

void clutter_damage_history_record (ClutterDamageHistory *history,
                                    const cairo_region_t *damage);

void clutter_damage_history_step (ClutterDamageHistory *history);

const cairo_region_t * clutter_damage_history_lookup (ClutterDamageHistory *history,
                                                      int                   age);

#endif /* CLUTTER_DAMAGE_HISTORY_H */
