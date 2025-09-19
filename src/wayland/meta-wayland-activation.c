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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "wayland/meta-wayland-activation.h"

#include <glib.h>
#include <wayland-server.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "xdg-activation-v1-server-protocol.h"

typedef struct _MetaXdgActivationToken MetaXdgActivationToken;

struct _MetaWaylandActivation
{
    MetaWaylandCompositor *compositor;
    struct wl_list resource_list;
    struct wl_list token_list;
    GHashTable *tokens;
};

struct _MetaXdgActivationToken
{
    MetaWaylandSurface *surface;
    MetaWaylandSeat *seat;
    MetaWaylandActivation *activation;
    MetaStartupSequence *sequence;
    struct wl_listener surface_listener;
    char *app_id;
    char *token;
    uint32_t serial;
    gulong sequence_complete_id;
    gulong sequence_timeout_id;
    gboolean committed;
};

static void
unbind_resource (struct wl_resource *resource)
{
    wl_list_remove (wl_resource_get_link (resource));
}

static void
token_set_serial (struct wl_client   *client,
                  struct wl_resource *resource,
                  uint32_t            serial,
                  struct wl_resource *seat_resource)
{
    MetaXdgActivationToken *token = wl_resource_get_user_data (resource);
    MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);

    token->serial = serial;
    token->seat = seat;
}

static void
token_set_app_id (struct wl_client   *client,
                  struct wl_resource *resource,
                  const char         *app_id)
{
    MetaXdgActivationToken *token = wl_resource_get_user_data (resource);

    g_clear_pointer (&token->app_id, g_free);
    token->app_id = g_strdup (app_id);
}

static void
token_set_surface (struct wl_client   *client,
                   struct wl_resource *resource,
                   struct wl_resource *surface_resource)
{
    MetaXdgActivationToken *token = wl_resource_get_user_data (resource);
    MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

    token->surface = surface;
    wl_resource_add_destroy_listener (surface_resource,
                                      &token->surface_listener);
}

static void
sequence_complete_cb (MetaStartupSequence    *sequence,
                      MetaXdgActivationToken *token)
{
    MetaWaylandActivation *activation = token->activation;
    MetaDisplay *display = meta_get_display ();

    if (!g_hash_table_contains (activation->tokens, token->token))
        return;

    meta_startup_notification_remove_sequence (display->startup_notification,
                                               sequence);
    g_hash_table_remove (activation->tokens, token->token);
}

static void
sequence_timeout_cb (MetaStartupSequence    *sequence,
                     MetaXdgActivationToken *token)
{
    MetaWaylandActivation *activation = token->activation;

    g_hash_table_remove (activation->tokens, token->token);
}

static char *
create_startup_token (MetaWaylandActivation *activation,
                      MetaDisplay           *display)
{
    g_autofree char *uuid = NULL, *token = NULL;

    do
    {
        g_clear_pointer (&uuid, g_free);
        g_clear_pointer (&token, g_free);
        uuid = g_uuid_string_random ();
        token = g_strdup_printf ("%s_TIME%d", uuid,
                                 meta_display_get_current_time (display));
    }
    while (g_hash_table_contains (activation->tokens, token));

    return g_steal_pointer (&token);
}

static void
token_commit (struct wl_client   *client,
              struct wl_resource *resource)
{
    MetaXdgActivationToken *token = wl_resource_get_user_data (resource);
    MetaWaylandActivation *activation = token->activation;
    MetaDisplay *display = meta_get_display ();
    uint64_t timestamp;

    if (token->committed)
    {
        wl_resource_post_error (resource,
                                XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED,
                                "Activation token was already used");
        return;
    }

    timestamp = meta_display_get_current_time_roundtrip (display);

    token->committed = TRUE;
    token->token = create_startup_token (activation, display);
    token->sequence = g_object_new (META_TYPE_STARTUP_SEQUENCE,
                                    "id", token->token,
                                    "application-id", token->app_id,
                                    "timestamp", timestamp,
                                    NULL);

    token->sequence_complete_id =
    g_signal_connect (token->sequence,
                      "complete",
                      G_CALLBACK (sequence_complete_cb),
                      token);

    token->sequence_timeout_id =
      g_signal_connect (token->sequence,
                        "timeout",
                        G_CALLBACK (sequence_timeout_cb),
                        token);

    meta_startup_notification_add_sequence (display->startup_notification,
                                            token->sequence);

    xdg_activation_token_v1_send_done (resource, token->token);
    g_hash_table_insert (activation->tokens, token->token, token);
}

static void
token_destroy (struct wl_client   *client,
               struct wl_resource *resource)
{
    wl_resource_destroy (resource);
}

static const struct xdg_activation_token_v1_interface token_interface = {
    token_set_serial,
    token_set_app_id,
    token_set_surface,
    token_commit,
    token_destroy,
};

static void
meta_xdg_activation_token_free (MetaXdgActivationToken *token)
{
    if (token->sequence)
    {
        g_clear_signal_handler (&token->sequence_complete_id,
                                token->sequence);
        g_clear_signal_handler (&token->sequence_timeout_id,
                                token->sequence);
        g_clear_object (&token->sequence);
    }

    if (token->surface)
        wl_list_remove (&token->surface_listener.link);

    g_free (token->app_id);
    g_free (token->token);
    g_free (token);
}

static void
token_handle_surface_destroy (struct wl_listener *listener,
                              void               *data)
{
    MetaXdgActivationToken *token = wl_container_of (listener, token,
                                                     surface_listener);

    token->surface = NULL;
    wl_list_remove (&token->surface_listener.link);
}

static void
meta_wayland_activation_token_create_new_resource (MetaWaylandActivation *activation,
                                                   struct wl_client      *client,
                                                   struct wl_resource    *activation_resource,
                                                   uint32_t               id)
{
    MetaXdgActivationToken *token;
    struct wl_resource *token_resource;

    token = g_new0 (MetaXdgActivationToken, 1);
    token->activation = activation;

    token_resource =
    wl_resource_create (client, &xdg_activation_token_v1_interface,
                        wl_resource_get_version (activation_resource),
                        id);
    wl_resource_set_implementation (token_resource, &token_interface,
                                    token, unbind_resource);
    wl_resource_set_user_data (token_resource, token);
    wl_list_insert (&activation->token_list,
                    wl_resource_get_link (token_resource));
    token->surface_listener.notify = token_handle_surface_destroy;
}

static void
activation_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
    wl_resource_destroy (resource);
}

static void
activation_get_activation_token (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            id)
{
    MetaWaylandActivation *activation = wl_resource_get_user_data (resource);

    meta_wayland_activation_token_create_new_resource (activation,
                                                       client,
                                                       resource,
                                                       id);
}

static gboolean
token_can_activate (MetaXdgActivationToken *token)
{
    MetaWaylandSeat *seat;

    if (!token->seat)
        return FALSE;
    if (!token->surface)
        return FALSE;

    seat = token->seat;

    if (seat->keyboard &&
        meta_wayland_keyboard_can_grab_surface (seat->keyboard,
                                                token->surface,
                                                token->serial))
      return TRUE;

    return meta_wayland_seat_get_grab_info (seat,
                                            token->surface,
                                            token->serial,
                                            FALSE, NULL, NULL);
}

static gboolean
startup_sequence_is_recent (MetaDisplay         *display,
                            MetaStartupSequence *sequence)
{
    uint32_t seq_timestamp_ms, last_user_time_ms;

    seq_timestamp_ms = meta_startup_sequence_get_timestamp (sequence);
    last_user_time_ms = meta_display_get_last_user_time (display);

    return seq_timestamp_ms >= last_user_time_ms;
}

static void
activation_activate (struct wl_client   *client,
                     struct wl_resource *resource,
                     const char         *token_str,
                     struct wl_resource *surface_resource)
{
    MetaWaylandActivation *activation = wl_resource_get_user_data (resource);
    MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
    MetaDisplay *display = meta_get_display ();
    MetaXdgActivationToken *token;
    MetaStartupSequence *sequence;
    MetaWindow *window;

    window = meta_wayland_surface_get_window (surface);
    if (!window)
        return;

    token = g_hash_table_lookup (activation->tokens, token_str);
    if (token)
      {
        sequence = token->sequence;
      }
    else
      {
          sequence = meta_startup_notification_lookup_sequence (display->startup_notification,
                                                                   token_str);
      }

    if (!sequence)
        return;

    if ((token && token_can_activate (token)) ||
        (!token && startup_sequence_is_recent (display, sequence)))
    {
        uint32_t timestamp;
        int32_t workspace_idx;

        workspace_idx = meta_startup_sequence_get_workspace (sequence);
        timestamp = meta_startup_sequence_get_timestamp (sequence);

        if (workspace_idx >= 0)
            meta_window_change_workspace_by_index (window, workspace_idx, TRUE);

        meta_window_activate_full (window, timestamp,
                                   META_CLIENT_TYPE_APPLICATION, NULL);
    }
    else
    {
        meta_window_set_demands_attention (window);
    }

    meta_startup_sequence_complete (sequence);
}

static const struct xdg_activation_v1_interface activation_interface = {
    activation_destroy,
    activation_get_activation_token,
    activation_activate,
};

static void
bind_activation (struct wl_client *client,
                 void             *data,
                 uint32_t          version,
                 uint32_t          id)
{
    MetaWaylandCompositor *compositor = data;
    MetaWaylandActivation *activation = compositor->activation;
    struct wl_resource *resource;

    resource = wl_resource_create (client, &xdg_activation_v1_interface,
                                   MIN (version, META_XDG_ACTIVATION_V1_VERSION),
                                   id);
    wl_resource_set_implementation (resource, &activation_interface,
                                    activation, unbind_resource);
    wl_resource_set_user_data (resource, activation);
    wl_list_insert (&activation->resource_list,
                    wl_resource_get_link (resource));
}

void
meta_wayland_activation_finalize (MetaWaylandCompositor *compositor)
{
    g_hash_table_destroy (compositor->activation->tokens);
    g_clear_pointer (&compositor->activation, g_free);
}

void
meta_wayland_activation_init (MetaWaylandCompositor *compositor)
{
    MetaWaylandActivation *activation;

    activation = g_new0 (MetaWaylandActivation, 1);
    activation->compositor = compositor;
    wl_list_init (&activation->resource_list);
    wl_list_init (&activation->token_list);

    activation->tokens =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           NULL,
                           (GDestroyNotify) meta_xdg_activation_token_free);

    wl_global_create (compositor->wayland_display,
                      &xdg_activation_v1_interface,
                      META_XDG_ACTIVATION_V1_VERSION,
                      compositor, bind_activation);

    compositor->activation = activation;
}
