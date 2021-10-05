/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_SELECTION_SOURCE_H
#define META_SELECTION_SOURCE_H

#include <gio/gio.h>

#include <meta/common.h>

typedef enum
{
  META_SELECTION_PRIMARY,
  META_SELECTION_CLIPBOARD,
  META_SELECTION_DND,
  META_N_SELECTION_TYPES,
} MetaSelectionType;

typedef struct _MetaSelectionSourceClass MetaSelectionSourceClass;
typedef struct _MetaSelectionSource MetaSelectionSource;

#define META_TYPE_SELECTION_SOURCE (meta_selection_source_get_type ())

META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaSelectionSource,
                          meta_selection_source,
                          META, SELECTION_SOURCE,
                          GObject)

struct _MetaSelectionSourceClass
{
  GObjectClass parent_class;

  void           (* activated)     (MetaSelectionSource *source);
  void           (* deactivated)   (MetaSelectionSource *source);

  GList *        (* get_mimetypes) (MetaSelectionSource  *source);

  void           (* read_async)    (MetaSelectionSource  *source,
                                    const gchar          *mimetype,
                                    GCancellable         *cancellable,
                                    GAsyncReadyCallback   callback,
                                    gpointer              user_data);
  GInputStream * (* read_finish)   (MetaSelectionSource  *source,
                                    GAsyncResult         *result,
                                    GError              **error);
};

META_EXPORT
void           meta_selection_source_read_async  (MetaSelectionSource  *source,
                                                  const gchar          *mimetype,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data);

META_EXPORT
GInputStream * meta_selection_source_read_finish (MetaSelectionSource  *source,
                                                  GAsyncResult         *result,
                                                  GError              **error);

META_EXPORT
GList *  meta_selection_source_get_mimetypes     (MetaSelectionSource  *source);

META_EXPORT
gboolean meta_selection_source_is_active         (MetaSelectionSource  *source);

#endif /* META_SELECTION_SOURCE_H */
