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

#ifndef META_WAYLAND_DATA_SOURCE_PRIMARY_H
#define META_WAYLAND_DATA_SOURCE_PRIMARY_H

#include "meta-wayland-data-source.h"

#define META_TYPE_WAYLAND_DATA_SOURCE_PRIMARY (meta_wayland_data_source_primary_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandDataSourcePrimary,
                      meta_wayland_data_source_primary,
                      META, WAYLAND_DATA_SOURCE_PRIMARY,
                      MetaWaylandDataSource);

MetaWaylandDataSource * meta_wayland_data_source_primary_new (struct wl_resource *resource);

#endif /* META_WAYLAND_DATA_SOURCE_PRIMARY_H */
