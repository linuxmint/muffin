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

#include "core/meta-clipboard-manager.h"
#include "meta/meta-selection-source-memory.h"

#define MAX_TEXT_SIZE (4 * 1024 * 1024) /* 4MB */
#define MAX_IMAGE_SIZE (200 * 1024 * 1024) /* 200MB */

/* Supported mimetype globs, from least to most preferred */
static struct {
  const char *mimetype_glob;
  ssize_t max_transfer_size;
} supported_mimetypes[] = {
  { "image/tiff",               MAX_IMAGE_SIZE },
  { "image/bmp",                MAX_IMAGE_SIZE },
  { "image/gif",                MAX_IMAGE_SIZE },
  { "image/jpeg",               MAX_IMAGE_SIZE },
  { "image/webp",               MAX_IMAGE_SIZE },
  { "image/png",                MAX_IMAGE_SIZE },
  { "image/svg+xml",            MAX_IMAGE_SIZE },
  { "text/plain",               MAX_TEXT_SIZE },
  { "text/plain;charset=utf-8", MAX_TEXT_SIZE },
};

static gboolean
mimetype_match (const char *mimetype,
                int        *idx,
                gssize     *max_transfer_size)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (supported_mimetypes); i++)
    {
      if (g_pattern_match_simple (supported_mimetypes[i].mimetype_glob, mimetype))
        {
          *max_transfer_size = supported_mimetypes[i].max_transfer_size;
          *idx = i;
          return TRUE;
        }
    }

  return FALSE;
}

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *result,
             GOutputStream *output)
{
  MetaDisplay *display = meta_get_display ();
  GError *error = NULL;

  if (!meta_selection_transfer_finish (selection, result, &error))
    {
      g_warning ("Failed to store clipboard: %s", error->message);
      g_error_free (error);
      g_object_unref (output);
      return;
    }

  g_output_stream_close (output, NULL, NULL);
  display->saved_clipboard =
    g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
  g_object_unref (output);
}

static void
owner_changed_cb (MetaSelection       *selection,
                  MetaSelectionType    selection_type,
                  MetaSelectionSource *new_owner,
                  MetaDisplay         *display)
{
  if (selection_type != META_SELECTION_CLIPBOARD)
    return;

  if (new_owner && new_owner != display->selection_source)
    {
      GOutputStream *output;
      GList *mimetypes, *l;
      int best_idx = -1;
      const char *best = NULL;
      ssize_t transfer_size = -1;

      /* New selection source, find the best mimetype in order to
       * keep a copy of it.
       */
      g_clear_object (&display->selection_source);
      g_clear_pointer (&display->saved_clipboard_mimetype, g_free);
      g_clear_pointer (&display->saved_clipboard, g_bytes_unref);

      mimetypes = meta_selection_get_mimetypes (selection, selection_type);

      for (l = mimetypes; l; l = l->next)
        {
          gssize max_transfer_size;
          int idx;

          if (!mimetype_match (l->data, &idx, &max_transfer_size))
            continue;

          if (best_idx < idx)
            {
              best_idx = idx;
              best = l->data;
              transfer_size = max_transfer_size;
            }
        }

      if (best_idx < 0)
        {
          g_list_free_full (mimetypes, g_free);
          return;
        }

      display->saved_clipboard_mimetype = g_strdup (best);
      g_list_free_full (mimetypes, g_free);
      output = g_memory_output_stream_new_resizable ();
      meta_selection_transfer_async (selection,
                                     META_SELECTION_CLIPBOARD,
                                     display->saved_clipboard_mimetype,
                                     transfer_size,
                                     output,
                                     NULL,
                                     (GAsyncReadyCallback) transfer_cb,
                                     output);
    }
  else if (!new_owner && display->saved_clipboard)
    {
      /* Old owner is gone, time to take over */
      new_owner = meta_selection_source_memory_new (display->saved_clipboard_mimetype,
                                                    display->saved_clipboard);
      g_set_object (&display->selection_source, new_owner);
      meta_selection_set_owner (selection, selection_type, new_owner);
      g_object_unref (new_owner);
    }
}

void
meta_clipboard_manager_init (MetaDisplay *display)
{
  MetaSelection *selection;

  selection = meta_display_get_selection (display);
  g_signal_connect_after (selection, "owner-changed",
                          G_CALLBACK (owner_changed_cb), display);
}

void
meta_clipboard_manager_shutdown (MetaDisplay *display)
{
  MetaSelection *selection;

  g_clear_object (&display->selection_source);
  g_clear_pointer (&display->saved_clipboard, g_bytes_unref);
  g_clear_pointer (&display->saved_clipboard_mimetype, g_free);
  selection = meta_display_get_selection (display);
  g_signal_handlers_disconnect_by_func (selection, owner_changed_cb, display);
}
