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

#include <gio/gunixoutputstream.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "meta/meta-selection.h"
#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-wayland-private.h"

#include "meta-wayland-data-offer.h"

#define ALL_ACTIONS (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
                     WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
                     WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

static void
data_offer_accept (struct wl_client *client,
                   struct wl_resource *resource,
                   guint32 serial,
                   const char *mime_type)
{
  MetaWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  /* FIXME: Check that client is currently focused by the input
   * device that is currently dragging this data source.  Should
   * this be a wl_data_device request? */

  if (offer->source)
    {
      meta_wayland_data_source_target (offer->source, mime_type);
      meta_wayland_data_source_set_has_target (offer->source,
                                               mime_type != NULL);
    }

  offer->accepted = mime_type != NULL;
}

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
data_offer_receive (struct wl_client *client, struct wl_resource *resource,
                    const char *mime_type, int32_t fd)
{
  MetaWaylandDataOffer *offer = wl_resource_get_user_data (resource);
  MetaDisplay *display = meta_get_display ();
  MetaSelectionType selection_type;
  GList *mime_types;
  gboolean found;

  selection_type = offer->selection_type;
  mime_types = meta_selection_get_mimetypes (meta_display_get_selection (display),
                                             selection_type);
  found = g_list_find_custom (mime_types, mime_type, (GCompareFunc) g_strcmp0) != NULL;
  g_list_free_full (mime_types, g_free);

  if (found)
    {
      GOutputStream *stream;

      stream = g_unix_output_stream_new (fd, TRUE);
      meta_selection_transfer_async (meta_display_get_selection (display),
                                     selection_type,
                                     mime_type,
                                     -1,
                                     stream,
                                     NULL,
                                     (GAsyncReadyCallback) transfer_cb,
                                     stream);
    }
  else
    {
      close (fd);
    }
}

static void
data_offer_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
data_offer_finish (struct wl_client   *client,
		   struct wl_resource *resource)
{
  MetaWaylandDataOffer *offer = wl_resource_get_user_data (resource);
  enum wl_data_device_manager_dnd_action current_action;

  if (!offer->source ||
      offer != meta_wayland_data_source_get_current_offer (offer->source))
    return;

  if (!offer->accepted || !offer->action_sent)
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_FINISH,
                              "premature finish request");
      return;
    }

  current_action = meta_wayland_data_source_get_current_action (offer->source);

  if (current_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE ||
      current_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_OFFER,
                              "offer finished with an invalid action");
      return;
    }

  meta_wayland_data_source_notify_finish (offer->source);
}

static void
data_offer_set_actions (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            dnd_actions,
                        uint32_t            preferred_action)
{
  MetaWaylandDataOffer *offer = wl_resource_get_user_data (resource);

  if (dnd_actions & ~(ALL_ACTIONS))
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
                              "invalid actions mask %x", dnd_actions);
      return;
    }

  if (preferred_action &&
      (!(preferred_action & dnd_actions) ||
       __builtin_popcount (preferred_action) > 1))
    {
      wl_resource_post_error (offer->resource,
                              WL_DATA_OFFER_ERROR_INVALID_ACTION,
                              "invalid action %x", preferred_action);
      return;
    }

  offer->dnd_actions = dnd_actions;
  offer->preferred_dnd_action = preferred_action;

  meta_wayland_data_offer_update_action (offer);
}

static const struct wl_data_offer_interface data_offer_interface = {
  data_offer_accept,
  data_offer_receive,
  data_offer_destroy,
  data_offer_finish,
  data_offer_set_actions,
};

static void
destroy_data_offer (struct wl_resource *resource)
{
  MetaWaylandDataOffer *offer = wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat;

  if (offer->source)
    {
      seat = meta_wayland_data_source_get_seat (offer->source);

      if (offer == meta_wayland_data_source_get_current_offer (offer->source))
        {
          if (seat->data_device.dnd_data_source == offer->source)
            {
              if (wl_resource_get_version (offer->resource) <
                  WL_DATA_OFFER_ACTION_SINCE_VERSION)
                meta_wayland_data_source_notify_finish (offer->source);
              else if (meta_wayland_data_source_get_drop_performed (offer->source))
                meta_wayland_data_source_cancel (offer->source);
            }
          else
            {
              meta_wayland_data_source_set_current_offer (offer->source, NULL);
              meta_wayland_data_source_set_has_target (offer->source, FALSE);
            }
        }

      g_object_remove_weak_pointer (G_OBJECT (offer->source),
                                    (gpointer *)&offer->source);
      offer->source = NULL;
    }

  meta_display_sync_wayland_input_focus (meta_get_display ());
  g_slice_free (MetaWaylandDataOffer, offer);
}

MetaWaylandDataOffer *
meta_wayland_data_offer_new (MetaSelectionType      selection_type,
                             MetaWaylandDataSource *source,
                             struct wl_resource    *target)
{
  MetaWaylandDataOffer *offer;

  offer = g_slice_new0 (MetaWaylandDataOffer);
  offer->selection_type = selection_type;
  offer->resource = wl_resource_create (wl_resource_get_client (target),
                                        &wl_data_offer_interface,
                                        wl_resource_get_version (target), 0);
  wl_resource_set_implementation (offer->resource,
                                  &data_offer_interface,
                                  offer,
                                  destroy_data_offer);
  if (source)
    {
      offer->source = source;
      g_object_add_weak_pointer (G_OBJECT (source), (gpointer *)&offer->source);
    }

  return offer;
}

static enum wl_data_device_manager_dnd_action
data_offer_choose_action (MetaWaylandDataOffer *offer)
{
  MetaWaylandDataSource *source = offer->source;
  uint32_t actions, user_action, available_actions;

  if (wl_resource_get_version (offer->resource) <
      WL_DATA_OFFER_ACTION_SINCE_VERSION)
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;

  meta_wayland_data_source_get_actions (source, &actions);
  user_action = meta_wayland_data_source_get_user_action (source);

  available_actions = actions & offer->dnd_actions;

  if (!available_actions)
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;

  /* If the user is forcing an action, go for it */
  if ((user_action & available_actions) != 0)
    return user_action;

  /* If the dest side has a preferred DnD action, use it */
  if ((offer->preferred_dnd_action & available_actions) != 0)
    return offer->preferred_dnd_action;

  /* Use the first found action, in bit order */
  return 1 << (ffs (available_actions) - 1);
}

void
meta_wayland_data_offer_update_action (MetaWaylandDataOffer *offer)
{
  enum wl_data_device_manager_dnd_action current_action, action;
  MetaWaylandDataSource *source;

  if (!offer->source)
    return;

  source = offer->source;
  current_action = meta_wayland_data_source_get_current_action (source);
  action = data_offer_choose_action (offer);

  if (current_action == action)
    return;

  meta_wayland_data_source_set_current_action (source, action);

  if (!meta_wayland_data_source_get_in_ask (source) &&
      wl_resource_get_version (offer->resource) >=
      WL_DATA_OFFER_ACTION_SINCE_VERSION)
    {
      wl_data_offer_send_action (offer->resource, action);
      offer->action_sent = TRUE;
    }
}

struct wl_resource *
meta_wayland_data_offer_get_resource (MetaWaylandDataOffer *offer)
{
  return offer->resource;
}

MetaWaylandDataSource *
meta_wayland_data_offer_get_source (MetaWaylandDataOffer *offer)
{
  return offer->source;
}
