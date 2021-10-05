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

#include <unistd.h>

#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-private.h"

#define ALL_ACTIONS (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
                     WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
                     WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

typedef struct _MetaWaylandDataSourcePrivate
{
  struct wl_resource *resource;
  MetaWaylandDataOffer *offer;
  struct wl_array mime_types;
  gboolean has_target;
  uint32_t dnd_actions;
  enum wl_data_device_manager_dnd_action user_dnd_action;
  enum wl_data_device_manager_dnd_action current_dnd_action;
  MetaWaylandSeat *seat;
  guint actions_set : 1;
  guint in_ask : 1;
  guint drop_performed : 1;
} MetaWaylandDataSourcePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaWaylandDataSource, meta_wayland_data_source,
                            G_TYPE_OBJECT);

static void
meta_wayland_data_source_real_send (MetaWaylandDataSource *source,
				    const gchar           *mime_type,
				    gint                   fd)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  wl_data_source_send_send (priv->resource, mime_type, fd);
  close (fd);
}

static void
meta_wayland_data_source_real_target (MetaWaylandDataSource *source,
				      const gchar           *mime_type)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  wl_data_source_send_target (priv->resource, mime_type);
}

static void
meta_wayland_data_source_real_cancel (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (!priv->resource)
    return;

  wl_data_source_send_cancelled (priv->resource);
}

static void
meta_wayland_data_source_real_action (MetaWaylandDataSource                  *source,
				      enum wl_data_device_manager_dnd_action  action)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (wl_resource_get_version (priv->resource) >=
      WL_DATA_SOURCE_ACTION_SINCE_VERSION)
    wl_data_source_send_action (priv->resource, action);
}

static void
meta_wayland_data_source_real_drop_performed (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (wl_resource_get_version (priv->resource) >=
      WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
    {
      priv->drop_performed = TRUE;
      wl_data_source_send_dnd_drop_performed (priv->resource);
    }
}

static void
meta_wayland_data_source_real_drag_finished (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);
  enum wl_data_device_manager_dnd_action action;

  if (meta_wayland_data_source_get_in_ask (source))
    {
      action = meta_wayland_data_source_get_current_action (source);
      meta_wayland_data_source_real_action (source, action);
    }

  if (wl_resource_get_version (priv->resource) >=
      WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION)
    wl_data_source_send_dnd_finished (priv->resource);
}

static void
meta_wayland_data_source_finalize (GObject *object)
{
  MetaWaylandDataSource *source = META_WAYLAND_DATA_SOURCE (object);
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);
  char **pos;

  wl_array_for_each (pos, &priv->mime_types)
    g_free (*pos);
  wl_array_release (&priv->mime_types);

  G_OBJECT_CLASS (meta_wayland_data_source_parent_class)->finalize (object);
}

static void
meta_wayland_data_source_init (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  wl_array_init (&priv->mime_types);
  priv->current_dnd_action = -1;
  priv->drop_performed = FALSE;
}

static void
meta_wayland_data_source_class_init (MetaWaylandDataSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_data_source_finalize;

  klass->send = meta_wayland_data_source_real_send;
  klass->target = meta_wayland_data_source_real_target;
  klass->cancel = meta_wayland_data_source_real_cancel;
  klass->action = meta_wayland_data_source_real_action;
  klass->drop_performed = meta_wayland_data_source_real_drop_performed;
  klass->drag_finished = meta_wayland_data_source_real_drag_finished;
}


static void
data_source_offer (struct wl_client *client,
                   struct wl_resource *resource, const char *type)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);

  if (!meta_wayland_data_source_add_mime_type (source, type))
    wl_resource_post_no_memory (resource);
}

static void
data_source_destroy (struct wl_client   *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
data_source_set_actions (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            dnd_actions)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (priv->actions_set)
    {
      wl_resource_post_error (priv->resource,
                              WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                              "cannot set actions more than once");
      return;
    }

  if (dnd_actions & ~(ALL_ACTIONS))
    {
      wl_resource_post_error (priv->resource,
                              WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                              "invalid actions mask %x", dnd_actions);
      return;
    }

  if (meta_wayland_data_source_get_seat (source))
    {
      wl_resource_post_error (priv->resource,
                              WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                              "invalid action change after "
                              "wl_data_device.start_drag");
      return;
    }

  meta_wayland_data_source_set_actions (source, dnd_actions);
}

static struct wl_data_source_interface data_source_interface = {
  data_source_offer,
  data_source_destroy,
  data_source_set_actions
};

static void
destroy_data_source (struct wl_resource *resource)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);

  meta_wayland_data_source_set_resource (source, NULL);
  g_object_unref (source);
}

MetaWaylandDataSource *
meta_wayland_data_source_new (struct wl_resource *resource)
{
  MetaWaylandDataSource *source =
    g_object_new (META_TYPE_WAYLAND_DATA_SOURCE, NULL);
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  meta_wayland_data_source_set_resource (source, resource);
  wl_resource_set_implementation (resource, &data_source_interface,
                                  source, destroy_data_source);

  if (wl_resource_get_version (resource) < WL_DATA_SOURCE_ACTION_SINCE_VERSION)
    {
      priv->dnd_actions = priv->user_dnd_action =
        WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    }

  return source;
}

struct wl_resource *
meta_wayland_data_source_get_resource (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->resource;
}

void
meta_wayland_data_source_set_resource (MetaWaylandDataSource *source,
                                       struct wl_resource    *resource)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  priv->resource = resource;
}

gboolean
meta_wayland_data_source_get_in_ask (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->in_ask;
}

void
meta_wayland_data_source_update_in_ask (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  priv->in_ask =
    priv->current_dnd_action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
}

void
meta_wayland_data_source_target (MetaWaylandDataSource *source,
                                 const char            *mime_type)
{
  if (META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->target)
    META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->target (source, mime_type);
}

void
meta_wayland_data_source_send (MetaWaylandDataSource *source,
                               const char            *mime_type,
                               int                    fd)
{
  META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->send (source, mime_type, fd);
}

gboolean
meta_wayland_data_source_has_target (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->has_target;
}

void
meta_wayland_data_source_set_seat (MetaWaylandDataSource *source,
                                   MetaWaylandSeat       *seat)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  priv->seat = seat;
}

MetaWaylandSeat *
meta_wayland_data_source_get_seat (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->seat;
}

void
meta_wayland_data_source_set_has_target (MetaWaylandDataSource *source,
                                         gboolean               has_target)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  priv->has_target = has_target;
}

struct wl_array *
meta_wayland_data_source_get_mime_types (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private ((MetaWaylandDataSource *)source);

  return &priv->mime_types;
}

void
meta_wayland_data_source_cancel (MetaWaylandDataSource *source)
{
  META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->cancel (source);
}

gboolean
meta_wayland_data_source_get_actions (MetaWaylandDataSource *source,
                                      uint32_t              *dnd_actions)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (dnd_actions)
    *dnd_actions = priv->dnd_actions;

  return priv->actions_set;
}

enum wl_data_device_manager_dnd_action
meta_wayland_data_source_get_user_action (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (!priv->seat)
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;

  return priv->user_dnd_action;
}

enum wl_data_device_manager_dnd_action
meta_wayland_data_source_get_current_action (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->current_dnd_action;
}

void
meta_wayland_data_source_set_current_offer (MetaWaylandDataSource *source,
                                            MetaWaylandDataOffer  *offer)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  priv->offer = offer;
}

MetaWaylandDataOffer *
meta_wayland_data_source_get_current_offer (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->offer;
}

void
meta_wayland_data_source_set_current_action (MetaWaylandDataSource                  *source,
                                             enum wl_data_device_manager_dnd_action  action)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  if (priv->current_dnd_action == action)
    return;

  priv->current_dnd_action = action;

  if (!meta_wayland_data_source_get_in_ask (source))
    META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->action (source, action);
}

void
meta_wayland_data_source_set_actions (MetaWaylandDataSource *source,
                                      uint32_t               dnd_actions)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  priv->dnd_actions = dnd_actions;
  priv->actions_set = TRUE;
}

void
meta_wayland_data_source_set_user_action (MetaWaylandDataSource *source,
                                          uint32_t               action)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);
  MetaWaylandDataOffer *offer;

  if (priv->user_dnd_action == action)
    return;

  priv->user_dnd_action = action;
  offer = meta_wayland_data_source_get_current_offer (source);

  if (offer)
    meta_wayland_data_offer_update_action (offer);
}

gboolean
meta_wayland_data_source_get_drop_performed (MetaWaylandDataSource *source)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);

  return priv->drop_performed;
}

void
meta_wayland_data_source_notify_drop_performed (MetaWaylandDataSource *source)
{
  META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->drop_performed (source);
}

void
meta_wayland_data_source_notify_finish (MetaWaylandDataSource *source)
{
  META_WAYLAND_DATA_SOURCE_GET_CLASS (source)->drag_finished (source);
}

gboolean
meta_wayland_data_source_add_mime_type (MetaWaylandDataSource *source,
                                        const char            *mime_type)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);
  char **pos;

  pos = wl_array_add (&priv->mime_types, sizeof (*pos));

  if (pos)
    {
      *pos = g_strdup (mime_type);
      return *pos != NULL;
    }

  return FALSE;
}

gboolean
meta_wayland_data_source_has_mime_type (MetaWaylandDataSource *source,
                                        const char            *mime_type)
{
  MetaWaylandDataSourcePrivate *priv =
    meta_wayland_data_source_get_instance_private (source);
  char **p;

  wl_array_for_each (p, &priv->mime_types)
    {
      if (g_strcmp0 (mime_type, *p) == 0)
        return TRUE;
    }

  return FALSE;
}
