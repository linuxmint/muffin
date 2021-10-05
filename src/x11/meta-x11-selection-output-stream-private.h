/* GIO - GLib Output, Output and Streaming Library
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

#ifndef META_X11_SELECTION_OUTPUT_STREAM_H
#define META_X11_SELECTION_OUTPUT_STREAM_H

#include <gio/gio.h>

#include "x11/meta-x11-display-private.h"

#define META_TYPE_X11_SELECTION_OUTPUT_STREAM (meta_x11_selection_output_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaX11SelectionOutputStream,
                      meta_x11_selection_output_stream,
                      META, X11_SELECTION_OUTPUT_STREAM,
                      GOutputStream)

GOutputStream * meta_x11_selection_output_stream_new         (MetaX11Display                *x11_display,
                                                              Window                         window,
                                                              const char                    *selection,
                                                              const char                    *target,
                                                              const char                    *property,
                                                              const char                    *type,
                                                              int                            format,
                                                              gulong                         timestamp);

gboolean        meta_x11_selection_output_stream_xevent        (MetaX11SelectionOutputStream *stream,
                                                                const XEvent                 *xevent);

#endif /* META_X11_SELECTION_OUTPUT_STREAM_H */
