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

#ifndef META_WAYLAND_DATA_SOURCE_H
#define META_WAYLAND_DATA_SOURCE_H

#include <glib-object.h>
#include <wayland-server.h>

#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_DATA_SOURCE (meta_wayland_data_source_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaWaylandDataSource,
                          meta_wayland_data_source,
                          META, WAYLAND_DATA_SOURCE,
                          GObject)

typedef struct _MetaWaylandDataSourceClass MetaWaylandDataSourceClass;

struct _MetaWaylandDataSourceClass
{
  GObjectClass parent_class;

  void (* send)    (MetaWaylandDataSource *source,
                    const gchar           *mime_type,
                    gint                   fd);
  void (* target)  (MetaWaylandDataSource *source,
                    const gchar           *mime_type);
  void (* cancel)  (MetaWaylandDataSource *source);

  void (* action)         (MetaWaylandDataSource *source,
                           uint32_t               action);
  void (* drop_performed) (MetaWaylandDataSource *source);
  void (* drag_finished)  (MetaWaylandDataSource *source);
};

MetaWaylandDataSource * meta_wayland_data_source_new (struct wl_resource *resource);

struct wl_resource * meta_wayland_data_source_get_resource (MetaWaylandDataSource *source);
void                 meta_wayland_data_source_set_resource (MetaWaylandDataSource *source,
                                                            struct wl_resource    *resource);

gboolean meta_wayland_data_source_get_in_ask    (MetaWaylandDataSource *source);
void     meta_wayland_data_source_update_in_ask (MetaWaylandDataSource *source);

void meta_wayland_data_source_target (MetaWaylandDataSource *source,
                                      const char            *mime_type);
void meta_wayland_data_source_send   (MetaWaylandDataSource *source,
                                      const char            *mime_type,
                                      int                    fd);
void meta_wayland_data_source_cancel (MetaWaylandDataSource *source);

gboolean meta_wayland_data_source_has_target (MetaWaylandDataSource *source);

void              meta_wayland_data_source_set_seat (MetaWaylandDataSource *source,
                                                     MetaWaylandSeat       *seat);
MetaWaylandSeat * meta_wayland_data_source_get_seat (MetaWaylandDataSource *source);

void meta_wayland_data_source_set_has_target (MetaWaylandDataSource *source,
                                              gboolean               has_target);
struct wl_array * meta_wayland_data_source_get_mime_types (MetaWaylandDataSource *source);
gboolean meta_wayland_data_source_add_mime_type (MetaWaylandDataSource *source,
                                                 const gchar           *mime_type);
gboolean meta_wayland_data_source_has_mime_type (MetaWaylandDataSource *source,
                                                 const char            *mime_type);

gboolean meta_wayland_data_source_get_actions (MetaWaylandDataSource *source,
                                               uint32_t              *dnd_actions);
void     meta_wayland_data_source_set_actions (MetaWaylandDataSource *source,
                                               uint32_t               dnd_actions);

enum wl_data_device_manager_dnd_action
     meta_wayland_data_source_get_current_action (MetaWaylandDataSource *source);
void meta_wayland_data_source_set_current_action (MetaWaylandDataSource                  *source,
                                                  enum wl_data_device_manager_dnd_action  action);

enum wl_data_device_manager_dnd_action
     meta_wayland_data_source_get_user_action (MetaWaylandDataSource *source);
void meta_wayland_data_source_set_user_action (MetaWaylandDataSource *source,
                                               uint32_t               action);

MetaWaylandDataOffer *
     meta_wayland_data_source_get_current_offer (MetaWaylandDataSource *source);
void meta_wayland_data_source_set_current_offer (MetaWaylandDataSource *source,
                                                 MetaWaylandDataOffer  *offer);

gboolean meta_wayland_data_source_get_drop_performed (MetaWaylandDataSource *source);

void meta_wayland_data_source_notify_drop_performed (MetaWaylandDataSource *source);
void meta_wayland_data_source_notify_finish (MetaWaylandDataSource *source);

#endif /* META_WAYLAND_DATA_SOURCE_H */
