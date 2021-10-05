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

#ifndef META_WAYLAND_DATA_OFFER_H
#define META_WAYLAND_DATA_OFFER_H

#include "meta/meta-selection.h"
#include "wayland/meta-wayland-data-source.h"

struct _MetaWaylandDataOffer
{
  struct wl_resource *resource;
  MetaWaylandDataSource *source;
  struct wl_listener source_destroy_listener;
  gboolean accepted;
  gboolean action_sent;
  uint32_t dnd_actions;
  enum wl_data_device_manager_dnd_action preferred_dnd_action;
  MetaSelectionType selection_type;
};

MetaWaylandDataOffer * meta_wayland_data_offer_new (MetaSelectionType      selection_type,
                                                    MetaWaylandDataSource *source,
                                                    struct wl_resource    *resource);

void meta_wayland_data_offer_update_action (MetaWaylandDataOffer *offer);

struct wl_resource *    meta_wayland_data_offer_get_resource (MetaWaylandDataOffer *offer);
MetaWaylandDataSource * meta_wayland_data_offer_get_source   (MetaWaylandDataOffer *offer);

#endif /* META_WAYLAND_DATA_OFFER_H */
