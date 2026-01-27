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

#include "clutter-build-config.h"

#include "clutter-damage-history.h"

#define DAMAGE_HISTORY_LENGTH 0x10

struct _ClutterDamageHistory
{
    cairo_region_t *damages[DAMAGE_HISTORY_LENGTH];
    int index;
};

ClutterDamageHistory *
clutter_damage_history_new (void)
{
    ClutterDamageHistory *history;

    history = g_new0 (ClutterDamageHistory, 1);

    return history;
}

void
clutter_damage_history_free (ClutterDamageHistory *history)
{
    int i;

    for (i = 0; i < G_N_ELEMENTS (history->damages); i++)
        g_clear_pointer (&history->damages[i], cairo_region_destroy);

    g_free (history);
}

gboolean
clutter_damage_history_is_age_valid (ClutterDamageHistory *history,
                                     int                   age)
{
    if (age >= DAMAGE_HISTORY_LENGTH ||
        age < 1)
        return FALSE;

    if (!clutter_damage_history_lookup (history, age))
        return FALSE;

    return TRUE;
}

void
clutter_damage_history_record (ClutterDamageHistory *history,
                               const cairo_region_t *damage)
{
    g_clear_pointer (&history->damages[history->index], cairo_region_destroy);
    history->damages[history->index] = cairo_region_copy (damage);
}

static inline int
step_damage_index (int current,
                   int diff)
{
    return (current + diff) & (DAMAGE_HISTORY_LENGTH - 1);
}

void
clutter_damage_history_step (ClutterDamageHistory *history)
{
    history->index = step_damage_index (history->index, 1);
}

const cairo_region_t *
clutter_damage_history_lookup (ClutterDamageHistory *history,
                               int                   age)
{
    return history->damages[step_damage_index (history->index, -age)];
}
