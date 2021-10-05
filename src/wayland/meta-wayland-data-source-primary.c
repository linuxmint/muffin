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

#include "primary-selection-unstable-v1-server-protocol.h"
#include "wayland/meta-wayland-data-source-primary.h"

typedef struct _MetaWaylandDataSourcePrimary
{
  MetaWaylandDataSource parent;
} MetaWaylandDataSourcePrimary;

G_DEFINE_TYPE (MetaWaylandDataSourcePrimary, meta_wayland_data_source_primary,
               META_TYPE_WAYLAND_DATA_SOURCE);

static void
primary_source_offer (struct wl_client   *client,
                      struct wl_resource *resource,
                      const char         *type)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);

  if (!meta_wayland_data_source_add_mime_type (source, type))
    wl_resource_post_no_memory (resource);
}

static void
primary_source_destroy (struct wl_client   *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static struct zwp_primary_selection_source_v1_interface primary_source_interface = {
  primary_source_offer,
  primary_source_destroy,
};

static void
destroy_primary_source (struct wl_resource *resource)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);

  meta_wayland_data_source_set_resource (source, NULL);
  g_object_unref (source);
}

static void
meta_wayland_data_source_primary_send (MetaWaylandDataSource *source,
                                       const gchar           *mime_type,
                                       gint                   fd)
{
  struct wl_resource *resource = meta_wayland_data_source_get_resource (source);

  zwp_primary_selection_source_v1_send_send (resource, mime_type, fd);
  close (fd);
}

static void
meta_wayland_data_source_primary_cancel (MetaWaylandDataSource *source)
{
  struct wl_resource *resource = meta_wayland_data_source_get_resource (source);

  if (resource)
    zwp_primary_selection_source_v1_send_cancelled (resource);
}

static void
meta_wayland_data_source_primary_init (MetaWaylandDataSourcePrimary *source_primary)
{
}

static void
meta_wayland_data_source_primary_class_init (MetaWaylandDataSourcePrimaryClass *klass)
{
  MetaWaylandDataSourceClass *data_source_class =
    META_WAYLAND_DATA_SOURCE_CLASS (klass);

  data_source_class->send = meta_wayland_data_source_primary_send;
  data_source_class->cancel = meta_wayland_data_source_primary_cancel;
}

MetaWaylandDataSource *
meta_wayland_data_source_primary_new (struct wl_resource *resource)
{
  MetaWaylandDataSource *source_primary =
    g_object_new (META_TYPE_WAYLAND_DATA_SOURCE_PRIMARY, NULL);

  meta_wayland_data_source_set_resource (source_primary, resource);
  wl_resource_set_implementation (resource, &primary_source_interface,
                                  source_primary, destroy_primary_source);

  return source_primary;
}
