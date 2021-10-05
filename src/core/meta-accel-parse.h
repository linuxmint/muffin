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

#ifndef META_ACCEL_PARSE_H
#define META_ACCEL_PARSE_H

#include <glib.h>

#include "meta/common.h"

typedef struct _MetaKeyCombo MetaKeyCombo;

/* Not a real key symbol but means "key above the tab key"; this is
 * used as the default keybinding for cycle_group.
 * 0x2xxxxxxx is a range not used by GDK or X. the remaining digits are
 * randomly chosen */
#define META_KEY_ABOVE_TAB 0x2f7259c9

gboolean meta_parse_accelerator (const char   *accel,
                                 MetaKeyCombo *combo);
gboolean meta_parse_modifier    (const char          *accel,
                                 MetaVirtualModifier *mask);

#endif /* META_ACCEL_PARSE_H */
