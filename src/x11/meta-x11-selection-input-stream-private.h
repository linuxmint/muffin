/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Benjamin Otte <otte@gnome.org>
 *         Christian Kellner <gicmo@gnome.org>
 */

#ifndef META_X11_SELECTION_INPUT_STREAM_H
#define META_X11_SELECTION_INPUT_STREAM_H

#include <gio/gio.h>

#include "x11/meta-x11-display-private.h"

#define META_TYPE_X11_SELECTION_INPUT_STREAM (meta_x11_selection_input_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaX11SelectionInputStream,
                      meta_x11_selection_input_stream,
                      META, X11_SELECTION_INPUT_STREAM,
                      GInputStream)

void           meta_x11_selection_input_stream_new_async     (MetaX11Display             *x11_display,
                                                              Window                      window,
                                                              const char                 *selection,
                                                              const char                 *target,
                                                              guint32                     timestamp,
                                                              int                         io_priority,
                                                              GCancellable               *cancellable,
                                                              GAsyncReadyCallback         callback,
                                                              gpointer                    user_data);
GInputStream * meta_x11_selection_input_stream_new_finish    (GAsyncResult               *result,
                                                              const char                **type,
                                                              int                        *format,
                                                              GError                    **error);

gboolean       meta_x11_selection_input_stream_xevent        (MetaX11SelectionInputStream *stream,
                                                              const XEvent                *xevent);

G_END_DECLS

#endif /* META_X11_SELECTION_INPUT_STREAM_H */
