/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Linux Mint
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Michael Webster <miketwebster@gmail.com>
 */

#ifndef __CLUTTER_SEAT_WAYLAND_CLIENT_H__
#define __CLUTTER_SEAT_WAYLAND_CLIENT_H__

#include <glib-object.h>
#include <clutter/clutter-seat.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_SEAT_WAYLAND_CLIENT             (clutter_seat_wayland_client_get_type ())
#define CLUTTER_SEAT_WAYLAND_CLIENT(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_SEAT_WAYLAND_CLIENT, ClutterSeatWaylandClient))
#define CLUTTER_IS_SEAT_WAYLAND_CLIENT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_SEAT_WAYLAND_CLIENT))
#define CLUTTER_SEAT_WAYLAND_CLIENT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_SEAT_WAYLAND_CLIENT, ClutterSeatWaylandClientClass))
#define CLUTTER_IS_SEAT_WAYLAND_CLIENT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_SEAT_WAYLAND_CLIENT))
#define CLUTTER_SEAT_WAYLAND_CLIENT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_SEAT_WAYLAND_CLIENT, ClutterSeatWaylandClientClass))

typedef struct _ClutterSeatWaylandClient       ClutterSeatWaylandClient;
typedef struct _ClutterSeatWaylandClientClass  ClutterSeatWaylandClientClass;

struct _ClutterSeatWaylandClient
{
    ClutterSeat parent_instance;

    ClutterKeymap *keymap;
};

struct _ClutterSeatWaylandClientClass
{
    ClutterSeatClass parent_class;
};

GType clutter_seat_wayland_client_get_type (void) G_GNUC_CONST;

ClutterSeat * clutter_seat_wayland_client_new (void);

G_END_DECLS

#endif /* __CLUTTER_SEAT_WAYLAND_CLIENT_H__ */
