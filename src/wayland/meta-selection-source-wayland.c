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

#include "config.h"

#include <glib-unix.h>
#include <gio/gunixinputstream.h>

#include "wayland/meta-selection-source-wayland-private.h"

struct _MetaSelectionSourceWayland
{
  MetaSelectionSource parent_instance;
  MetaWaylandDataSource *data_source;
  GList *mimetypes;
};

G_DEFINE_TYPE (MetaSelectionSourceWayland, meta_selection_source_wayland,
               META_TYPE_SELECTION_SOURCE)

static void
meta_selection_source_wayland_finalize (GObject *object)
{
  MetaSelectionSourceWayland *source_wayland = META_SELECTION_SOURCE_WAYLAND (object);

  g_list_free_full (source_wayland->mimetypes, g_free);

  G_OBJECT_CLASS (meta_selection_source_wayland_parent_class)->finalize (object);
}

static void
meta_selection_source_wayland_read_async (MetaSelectionSource *source,
                                          const gchar         *mimetype,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  MetaSelectionSourceWayland *source_wayland = META_SELECTION_SOURCE_WAYLAND (source);
  GInputStream *stream;
  GTask *task;
  int pipe_fds[2];

  if (!g_unix_open_pipe (pipe_fds, FD_CLOEXEC, NULL))
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_wayland_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Could not open pipe to read wayland selection");
      return;
    }

  if (!g_unix_set_fd_nonblocking (pipe_fds[0], TRUE, NULL) ||
      !g_unix_set_fd_nonblocking (pipe_fds[1], TRUE, NULL))
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_wayland_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Could not make pipe nonblocking");
      close (pipe_fds[0]);
      close (pipe_fds[1]);
      return;
    }

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_source_wayland_read_async);

  stream = g_unix_input_stream_new (pipe_fds[0], TRUE);
  meta_wayland_data_source_send (source_wayland->data_source,
                                 mimetype, pipe_fds[1]);
  close (pipe_fds[1]);

  g_task_return_pointer (task, stream, g_object_unref);
  g_object_unref (task);
}

static GInputStream *
meta_selection_source_wayland_read_finish (MetaSelectionSource  *source,
                                           GAsyncResult         *result,
                                           GError              **error)
{
  g_return_val_if_fail (g_task_is_valid (result, source), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_selection_source_wayland_read_async, FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static GList *
meta_selection_source_wayland_get_mimetypes (MetaSelectionSource  *source)
{
  MetaSelectionSourceWayland *source_wayland = META_SELECTION_SOURCE_WAYLAND (source);

  return g_list_copy_deep (source_wayland->mimetypes,
                           (GCopyFunc) g_strdup, NULL);
}

static void
meta_selection_source_wayland_deactivated (MetaSelectionSource *source)
{
  MetaSelectionSourceWayland *source_wayland =
    META_SELECTION_SOURCE_WAYLAND (source);

  meta_wayland_data_source_cancel (source_wayland->data_source);
  META_SELECTION_SOURCE_CLASS (meta_selection_source_wayland_parent_class)->deactivated (source);
}

static void
meta_selection_source_wayland_class_init (MetaSelectionSourceWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

  object_class->finalize = meta_selection_source_wayland_finalize;

  source_class->deactivated = meta_selection_source_wayland_deactivated;

  source_class->read_async = meta_selection_source_wayland_read_async;
  source_class->read_finish = meta_selection_source_wayland_read_finish;
  source_class->get_mimetypes = meta_selection_source_wayland_get_mimetypes;
}

static void
meta_selection_source_wayland_init (MetaSelectionSourceWayland *source)
{
}

static GList *
copy_string_array_to_list (struct wl_array *array)
{
  GList *l = NULL;
  char **p;

  wl_array_for_each (p, array)
    l = g_list_prepend (l, g_strdup (*p));

  return l;
}

MetaSelectionSource *
meta_selection_source_wayland_new (MetaWaylandDataSource *data_source)
{
  MetaSelectionSourceWayland *source_wayland;
  struct wl_array *mimetypes;

  source_wayland = g_object_new (META_TYPE_SELECTION_SOURCE_WAYLAND, NULL);
  source_wayland->data_source = data_source;

  mimetypes = meta_wayland_data_source_get_mime_types (data_source);
  source_wayland->mimetypes = copy_string_array_to_list (mimetypes);

  return META_SELECTION_SOURCE (source_wayland);
}
