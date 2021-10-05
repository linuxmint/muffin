/*
 * Copyright © 2011 Kristian Høgsberg
 *             2020 Red Hat Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "config.h"

#include "meta-wayland-data-offer-primary.h"

#include <gio/gunixoutputstream.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/display-private.h"
#include "primary-selection-unstable-v1-server-protocol.h"
#include "wayland/meta-wayland-data-offer.h"

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *res,
             GOutputStream *stream)
{
  GError *error = NULL;

  if (!meta_selection_transfer_finish (selection, res, &error))
    {
      g_warning ("Could not fetch selection data: %s", error->message);
      g_error_free (error);
    }

  g_output_stream_close (stream, NULL, NULL);
  g_object_unref (stream);
}

static void
primary_offer_receive (struct wl_client   *client,
		       struct wl_resource *resource,
                       const char         *mime_type,
		       int32_t             fd)
{
  MetaDisplay *display = meta_get_display ();
  GOutputStream *stream;
  GList *mime_types;
  gboolean found;

  mime_types = meta_selection_get_mimetypes (meta_display_get_selection (display),
                                             META_SELECTION_PRIMARY);
  found = g_list_find_custom (mime_types, mime_type, (GCompareFunc) g_strcmp0) != NULL;
  g_list_free_full (mime_types, g_free);

  if (!found)
    {
      close (fd);
      return;
    }

  stream = g_unix_output_stream_new (fd, TRUE);
  meta_selection_transfer_async (meta_display_get_selection (display),
                                 META_SELECTION_PRIMARY,
                                 mime_type,
                                 -1,
                                 stream,
                                 NULL,
                                 (GAsyncReadyCallback) transfer_cb,
                                 stream);
}

static void
primary_offer_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_primary_selection_offer_v1_interface primary_offer_interface = {
  primary_offer_receive,
  primary_offer_destroy,
};

static void
destroy_primary_offer (struct wl_resource *resource)
{
  MetaWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  if (offer->source)
    {
      if (offer == meta_wayland_data_source_get_current_offer (offer->source))
        {
          meta_wayland_data_source_cancel (offer->source);
          meta_wayland_data_source_set_current_offer (offer->source, NULL);
        }

      g_object_remove_weak_pointer (G_OBJECT (offer->source),
                                    (gpointer *)&offer->source);
      offer->source = NULL;
    }

  meta_display_sync_wayland_input_focus (meta_get_display ());
  g_slice_free (MetaWaylandDataOffer, offer);
}

MetaWaylandDataOffer *
meta_wayland_data_offer_primary_new (struct wl_resource *target)
{
  MetaWaylandDataOffer *offer;

  offer = g_slice_new0 (MetaWaylandDataOffer);
  offer->selection_type = META_SELECTION_PRIMARY;
  offer->resource = wl_resource_create (wl_resource_get_client (target),
                                        &zwp_primary_selection_offer_v1_interface,
                                        wl_resource_get_version (target), 0);
  wl_resource_set_implementation (offer->resource,
                                  &primary_offer_interface,
                                  offer,
                                  destroy_primary_offer);
  return offer;
}
