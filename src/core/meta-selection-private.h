/*
 * Copyright (C) 2020 Red Hat
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
 *     Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_SELECTION_PRIVATE_H
#define META_SELECTION_PRIVATE_H

#include "meta/meta-selection.h"

MetaSelectionSource *
  meta_selection_get_current_owner (MetaSelection     *selection,
                                    MetaSelectionType  selection_type);

#endif /* META_SELECTION_PRIVATE_H */
