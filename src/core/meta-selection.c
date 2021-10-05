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

#include "core/meta-selection-private.h"
#include "meta/meta-selection.h"

typedef struct TransferRequest TransferRequest;

struct _MetaSelection
{
  GObject parent_instance;
  MetaDisplay *display;
  MetaSelectionSource *owners[META_N_SELECTION_TYPES];
};

struct TransferRequest
{
  MetaSelectionType selection_type;
  GInputStream  *istream;
  GOutputStream *ostream;
  gssize len;
};

enum
{
  OWNER_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (MetaSelection, meta_selection, G_TYPE_OBJECT)

static void read_selection_source_async (GTask           *task,
                                         TransferRequest *request);

static void
meta_selection_dispose (GObject *object)
{
  MetaSelection *selection = META_SELECTION (object);
  guint i;

  for (i = 0; i < META_N_SELECTION_TYPES; i++)
    {
      g_clear_object (&selection->owners[i]);
    }

  G_OBJECT_CLASS (meta_selection_parent_class)->dispose (object);
}

static void
meta_selection_class_init (MetaSelectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_selection_dispose;

  signals[OWNER_CHANGED] =
    g_signal_new ("owner-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  META_TYPE_SELECTION_SOURCE);
}

static void
meta_selection_init (MetaSelection *selection)
{
}

MetaSelection *
meta_selection_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_SELECTION,
                       NULL);
}

/**
 * meta_selection_set_owner:
 * @selection: The selection manager
 * @selection_type: Selection type
 * @owner: New selection owner
 *
 * Sets @owner as the owner of the selection given by @selection_type,
 * unsets any previous owner there was.
 **/
void
meta_selection_set_owner (MetaSelection       *selection,
                          MetaSelectionType    selection_type,
                          MetaSelectionSource *owner)
{
  g_return_if_fail (META_IS_SELECTION (selection));
  g_return_if_fail (selection_type < META_N_SELECTION_TYPES);

  if (selection->owners[selection_type] == owner)
    return;

  if (selection->owners[selection_type])
    g_signal_emit_by_name (selection->owners[selection_type], "deactivated");

  g_set_object (&selection->owners[selection_type], owner);
  g_signal_emit_by_name (owner, "activated");
  g_signal_emit (selection, signals[OWNER_CHANGED], 0, selection_type, owner);
}

/**
 * meta_selection_unset_owner:
 * @selection: The selection manager
 * @selection_type: Selection type
 * @owner: Owner to unset
 *
 * Unsets @owner as the owner the selection given by @selection_type. If
 * @owner does not own the selection, nothing is done.
 **/
void
meta_selection_unset_owner (MetaSelection       *selection,
                            MetaSelectionType    selection_type,
                            MetaSelectionSource *owner)
{
  g_return_if_fail (META_IS_SELECTION (selection));
  g_return_if_fail (selection_type < META_N_SELECTION_TYPES);

  if (selection->owners[selection_type] == owner)
    {
      g_signal_emit_by_name (owner, "deactivated");
      g_clear_object (&selection->owners[selection_type]);
      g_signal_emit (selection, signals[OWNER_CHANGED], 0,
                     selection_type, NULL);
    }
}

/**
 * meta_selection_get_mimetypes:
 * @selection: The selection manager
 * @selection_type: Selection to query
 *
 * Returns the list of supported mimetypes for the given selection type.
 *
 * Returns: (element-type utf8) (transfer full): The supported mimetypes
 */
GList *
meta_selection_get_mimetypes (MetaSelection     *selection,
                              MetaSelectionType  selection_type)
{
  g_return_val_if_fail (META_IS_SELECTION (selection), NULL);
  g_return_val_if_fail (selection_type < META_N_SELECTION_TYPES, NULL);

  if (!selection->owners[selection_type])
    return NULL;

  return meta_selection_source_get_mimetypes (selection->owners[selection_type]);
}

static TransferRequest *
transfer_request_new (GOutputStream     *ostream,
                      MetaSelectionType  selection_type,
                      gssize             len)
{
  TransferRequest *request;

  request = g_new0 (TransferRequest, 1);
  request->ostream = g_object_ref (ostream);
  request->selection_type = selection_type;
  request->len = len;
  return request;
}

static void
transfer_request_free (TransferRequest *request)
{
  g_clear_object (&request->istream);
  g_clear_object (&request->ostream);
  g_free (request);
}

static void
splice_cb (GOutputStream *stream,
           GAsyncResult  *result,
           GTask         *task)
{
  GError *error = NULL;

  g_output_stream_splice_finish (stream, result, &error);
  if (error)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static void
write_cb (GOutputStream *stream,
          GAsyncResult  *result,
          GTask         *task)
{
  TransferRequest *request;
  GError *error = NULL;

  g_output_stream_write_bytes_finish (stream, result, &error);
  if (error)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  request = g_task_get_task_data (task);

  if (request->len > 0)
    {
      read_selection_source_async (task, request);
    }
  else
    {
      g_task_return_boolean (task, TRUE);
      g_object_unref (task);
    }
}

static void
read_cb (GInputStream *stream,
         GAsyncResult *result,
         GTask        *task)
{
  TransferRequest *request;
  GError *error = NULL;
  GBytes *bytes;

  bytes = g_input_stream_read_bytes_finish (stream, result, &error);
  if (error)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }
  else if (g_bytes_get_size (bytes) == 0)
    {
      g_task_return_boolean (task, TRUE);
      g_object_unref (task);
      return;
    }

  request = g_task_get_task_data (task);

  if (request->len < g_bytes_get_size (bytes))
    {
      GBytes *copy;

      /* Trim content */
      copy = g_bytes_new_from_bytes (bytes, 0, request->len);
      g_bytes_unref (bytes);
      bytes = copy;
    }

  request->len -= g_bytes_get_size (bytes);
  g_output_stream_write_bytes_async (request->ostream,
                                     bytes,
                                     G_PRIORITY_DEFAULT,
                                     g_task_get_cancellable (task),
                                     (GAsyncReadyCallback) write_cb,
                                     task);
  g_bytes_unref (bytes);
}

static void
read_selection_source_async (GTask           *task,
                             TransferRequest *request)
{
  g_input_stream_read_bytes_async (request->istream,
                                   (gsize) request->len,
                                   G_PRIORITY_DEFAULT,
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback) read_cb,
                                   task);
}

static void
source_read_cb (MetaSelectionSource *source,
                GAsyncResult        *result,
                GTask               *task)
{
  TransferRequest *request;
  GInputStream *stream;
  GError *error = NULL;

  stream = meta_selection_source_read_finish (source, result, &error);
  if (!stream)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  request = g_task_get_task_data (task);
  request->istream = stream;

  if (request->len < 0)
    {
      g_output_stream_splice_async (request->ostream,
                                    request->istream,
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                    G_PRIORITY_DEFAULT,
                                    g_task_get_cancellable (task),
                                    (GAsyncReadyCallback) splice_cb,
                                    task);
    }
  else
    {
      read_selection_source_async (task, request);
    }
}

/**
 * meta_selection_transfer_async:
 * @selection: The selection manager
 * @selection_type: Selection type
 * @mimetype: Mimetype to transfer
 * @size: Maximum size to transfer, -1 for unlimited
 * @output: Output stream to write contents to
 * @cancellable: Cancellable
 * @callback: User callback
 * @user_data: User data
 *
 * Requests a transfer of @mimetype on the selection given by
 * @selection_type.
 **/
void
meta_selection_transfer_async (MetaSelection        *selection,
                               MetaSelectionType     selection_type,
                               const char           *mimetype,
                               gssize                size,
                               GOutputStream        *output,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  GTask *task;

  g_return_if_fail (META_IS_SELECTION (selection));
  g_return_if_fail (selection_type < META_N_SELECTION_TYPES);
  g_return_if_fail (G_IS_OUTPUT_STREAM (output));
  g_return_if_fail (mimetype != NULL);

  task = g_task_new (selection, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_transfer_async);

  g_task_set_task_data (task,
                        transfer_request_new (output, selection_type, size),
                        (GDestroyNotify) transfer_request_free);
  meta_selection_source_read_async (selection->owners[selection_type],
                                    mimetype,
                                    cancellable,
                                    (GAsyncReadyCallback) source_read_cb,
                                    task);
}

/**
 * meta_selection_transfer_finish:
 * @selection: The selection manager
 * @result: The async result
 * @error: Location for returned error, or %NULL
 *
 * Finishes the transfer of a queried mimetype.
 *
 * Returns: #TRUE if the transfer was successful.
 **/
gboolean
meta_selection_transfer_finish (MetaSelection  *selection,
                                GAsyncResult   *result,
                                GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, selection), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_selection_transfer_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

MetaSelectionSource *
meta_selection_get_current_owner (MetaSelection     *selection,
                                  MetaSelectionType  selection_type)
{
  g_return_val_if_fail (META_IS_SELECTION (selection), NULL);
  g_return_val_if_fail (selection_type < META_N_SELECTION_TYPES, NULL);

  return selection->owners[selection_type];
}
