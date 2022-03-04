/*
 * Wayland Support
 *
 * Copyright (C) 2014 Red Hat
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

#include <glib.h>
#include <string.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-private.h"

#ifdef HAVE_NATIVE_BACKEND
#include <libinput.h>
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-event-native.h"
#include "backends/native/meta-seat-native.h"
#endif

G_DEFINE_TYPE (MetaWaylandTouch, meta_wayland_touch,
               META_TYPE_WAYLAND_INPUT_DEVICE)

struct _MetaWaylandTouchSurface
{
  MetaWaylandSurface *surface;
  MetaWaylandTouch *touch;
  struct wl_listener surface_destroy_listener;
  struct wl_list resource_list;
  gint touch_count;
};

struct _MetaWaylandTouchInfo
{
  MetaWaylandTouchSurface *touch_surface;
  guint32 slot_serial;
  gint32 slot;
  gfloat start_x;
  gfloat start_y;
  gfloat x;
  gfloat y;
  guint updated : 1;
  guint begin_delivered : 1;
};

#ifdef HAVE_NATIVE_BACKEND
static void
move_resources (struct wl_list *destination, struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list   *destination,
			   struct wl_list   *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;
  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

static void
touch_surface_free (gpointer data)
{
  MetaWaylandTouchSurface *touch_surface = data;
  MetaWaylandTouch *touch = touch_surface->touch;

  move_resources (&touch->resource_list,
                  &touch_surface->resource_list);
  wl_list_remove (&touch_surface->surface_destroy_listener.link);
  g_free (touch_surface);
}

static MetaWaylandTouchSurface *
touch_surface_increment_touch (MetaWaylandTouchSurface *surface)
{
  surface->touch_count++;
  return surface;
}

static void
touch_surface_decrement_touch (MetaWaylandTouchSurface *touch_surface)
{
  touch_surface->touch_count--;

  if (touch_surface->touch_count == 0)
    {
      /* Now that there are no touches on the surface, free the
       * MetaWaylandTouchSurface, the memory is actually owned by
       * the touch_surface->touch_surfaces hashtable, so remove the
       * item from there.
       */
      MetaWaylandTouch *touch = touch_surface->touch;
      g_hash_table_remove (touch->touch_surfaces, touch_surface->surface);
    }
}

static void
touch_handle_surface_destroy (struct wl_listener *listener, void *data)
{
  MetaWaylandTouchSurface *touch_surface = wl_container_of (listener, touch_surface, surface_destroy_listener);
  MetaWaylandSurface *surface = touch_surface->surface;
  MetaWaylandTouch *touch = touch_surface->touch;
  MetaWaylandTouchInfo *touch_info;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, touch->touches);

  /* Destroy all touches on the surface, this indirectly drops touch_count
   * on the touch_surface to 0, also freeing touch_surface and removing
   * from the touch_surfaces hashtable.
   */
  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &touch_info))
    {
      if (touch_info->touch_surface == touch_surface)
        g_hash_table_iter_remove (&iter);
    }

  /* Ensure the surface no longer exists */
  g_assert (g_hash_table_remove (touch->touch_surfaces, surface) == FALSE);
}

static MetaWaylandTouchSurface *
touch_surface_get (MetaWaylandTouch   *touch,
                   MetaWaylandSurface *surface)
{
  MetaWaylandTouchSurface *touch_surface;

  touch_surface = g_hash_table_lookup (touch->touch_surfaces, surface);

  if (touch_surface)
    return touch_surface_increment_touch (touch_surface);

  /* Create a new one for this surface */
  touch_surface = g_new0 (MetaWaylandTouchSurface, 1);
  touch_surface->touch = touch;
  touch_surface->surface = surface;
  touch_surface->touch_count = 1;
  touch_surface->surface_destroy_listener.notify = touch_handle_surface_destroy;
  wl_resource_add_destroy_listener (touch_surface->surface->resource,
                                    &touch_surface->surface_destroy_listener);

  wl_list_init (&touch_surface->resource_list);
  move_resources_for_client (&touch_surface->resource_list,
                             &touch->resource_list,
                             wl_resource_get_client (touch_surface->surface->resource));

  g_hash_table_insert (touch->touch_surfaces, surface, touch_surface);

  return touch_surface;
}

static MetaWaylandTouchInfo *
touch_get_info (MetaWaylandTouch     *touch,
                ClutterEventSequence *sequence,
                gboolean              create)
{
  MetaWaylandTouchInfo *touch_info;

  touch_info = g_hash_table_lookup (touch->touches, sequence);

  if (!touch_info && create)
    {
      touch_info = g_new0 (MetaWaylandTouchInfo, 1);
      touch_info->slot = meta_event_native_sequence_get_slot (sequence);
      g_hash_table_insert (touch->touches, sequence, touch_info);
    }

  return touch_info;
}

static void
touch_get_relative_coordinates (MetaWaylandTouch   *touch,
                                MetaWaylandSurface *surface,
                                const ClutterEvent *event,
                                gfloat             *x,
                                gfloat             *y)
{
  gfloat event_x, event_y;

  clutter_event_get_coords (event, &event_x, &event_y);

  return meta_wayland_surface_get_relative_coordinates (surface,
                                                        event_x, event_y,
                                                        x, y);
}
#endif /* HAVE_NATIVE_BACKEND */

void
meta_wayland_touch_update (MetaWaylandTouch   *touch,
                           const ClutterEvent *event)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaWaylandTouchInfo *touch_info;
  ClutterEventSequence *sequence;

  sequence = clutter_event_get_event_sequence (event);

  if (event->type == CLUTTER_TOUCH_BEGIN)
    {
      MetaWaylandSurface *surface = NULL;
      ClutterActor *actor;

      actor = clutter_event_get_source (event);

      if (META_IS_SURFACE_ACTOR_WAYLAND (actor))
        surface = meta_surface_actor_wayland_get_surface (META_SURFACE_ACTOR_WAYLAND (actor));

      if (!surface)
        return;

      touch_info = touch_get_info (touch, sequence, TRUE);
      touch_info->touch_surface = touch_surface_get (touch, surface);
      clutter_event_get_coords (event, &touch_info->start_x, &touch_info->start_y);
    }
  else
    touch_info = touch_get_info (touch, sequence, FALSE);

  if (!touch_info)
    return;

  if (event->type != CLUTTER_TOUCH_BEGIN &&
      !touch_info->begin_delivered)
    {
      g_hash_table_remove (touch->touches, sequence);
      return;
    }

  if (event->type == CLUTTER_TOUCH_BEGIN ||
      event->type == CLUTTER_TOUCH_END)
    {
      MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (touch);

      touch_info->slot_serial =
        meta_wayland_input_device_next_serial (input_device);
    }

  touch_get_relative_coordinates (touch, touch_info->touch_surface->surface,
                                  event, &touch_info->x, &touch_info->y);
  touch_info->updated = TRUE;
#endif /* HAVE_NATIVE_BACKEND */
}

static void
handle_touch_begin (MetaWaylandTouch   *touch,
                    const ClutterEvent *event)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaWaylandTouchInfo *touch_info;
  ClutterEventSequence *sequence;
  struct wl_resource *resource;
  struct wl_list *l;

  sequence = clutter_event_get_event_sequence (event);
  touch_info = touch_get_info (touch, sequence, FALSE);

  if (!touch_info)
    return;

  l = &touch_info->touch_surface->resource_list;
  wl_resource_for_each(resource, l)
    {
      wl_touch_send_down (resource, touch_info->slot_serial,
                          clutter_event_get_time (event),
                          touch_info->touch_surface->surface->resource,
                          touch_info->slot,
                          wl_fixed_from_double (touch_info->x),
                          wl_fixed_from_double (touch_info->y));
    }

  touch_info->begin_delivered = TRUE;
#endif /* HAVE_NATIVE_BACKEND */
}

static void
handle_touch_update (MetaWaylandTouch   *touch,
                     const ClutterEvent *event)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaWaylandTouchInfo *touch_info;
  ClutterEventSequence *sequence;
  struct wl_resource *resource;
  struct wl_list *l;

  sequence = clutter_event_get_event_sequence (event);
  touch_info = touch_get_info (touch, sequence, FALSE);

  if (!touch_info)
    return;

  l = &touch_info->touch_surface->resource_list;
  wl_resource_for_each(resource, l)
    {
      wl_touch_send_motion (resource,
                            clutter_event_get_time (event),
                            touch_info->slot,
                            wl_fixed_from_double (touch_info->x),
                            wl_fixed_from_double (touch_info->y));
    }
#endif /* HAVE_NATIVE_BACKEND */
}

static void
handle_touch_end (MetaWaylandTouch   *touch,
                  const ClutterEvent *event)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaWaylandTouchInfo *touch_info;
  ClutterEventSequence *sequence;
  struct wl_resource *resource;
  struct wl_list *l;

  sequence = clutter_event_get_event_sequence (event);
  touch_info = touch_get_info (touch, sequence, FALSE);

  if (!touch_info)
    return;

  l = &touch_info->touch_surface->resource_list;
  wl_resource_for_each (resource, l)
    {
      wl_touch_send_up (resource, touch_info->slot_serial,
                        clutter_event_get_time (event),
                        touch_info->slot);
    }

  g_hash_table_remove (touch->touches, sequence);
#endif /* HAVE_NATIVE_BACKEND */
}

static GList *
touch_get_surfaces (MetaWaylandTouch *touch,
                    gboolean          only_updated)
{
  MetaWaylandTouchInfo *touch_info;
  GList *surfaces = NULL;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, touch->touches);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &touch_info))
    {
      if (only_updated && !touch_info->updated)
        continue;
      if (g_list_find (surfaces, touch_info->touch_surface))
        continue;

      surfaces = g_list_prepend (surfaces, touch_info->touch_surface);
      touch_info->updated = FALSE;
    }

  return g_list_reverse (surfaces);
}

static void
touch_send_frame_event (MetaWaylandTouch *touch)
{
  GList *surfaces, *s;

  surfaces = touch_get_surfaces (touch, TRUE);

  for (s = surfaces; s; s = s->next)
    {
      MetaWaylandTouchSurface *touch_surface = s->data;
      struct wl_resource *resource;
      struct wl_list *l;

      l = &touch_surface->resource_list;
      wl_resource_for_each(resource, l)
        {
          wl_touch_send_frame (resource);
        }
    }

  g_list_free (surfaces);
}

static void
check_send_frame_event (MetaWaylandTouch   *touch,
                        const ClutterEvent *event)
{
  gboolean send_frame_event;
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
  ClutterEventSequence *sequence;
  gint32 slot;

  if (META_IS_BACKEND_NATIVE (backend))
    {
      sequence = clutter_event_get_event_sequence (event);
      slot = meta_event_native_sequence_get_slot (sequence);
      touch->frame_slots &= ~(1 << slot);

      if (touch->frame_slots == 0)
        send_frame_event = TRUE;
      else
        send_frame_event = FALSE;
    }
  else
#endif /* HAVE_NATIVE_BACKEND */
    {
      send_frame_event = TRUE;
    }

  if (send_frame_event)
    touch_send_frame_event (touch);
}

gboolean
meta_wayland_touch_handle_event (MetaWaylandTouch   *touch,
                                 const ClutterEvent *event)
{
  switch (event->type)
    {
    case CLUTTER_TOUCH_BEGIN:
      handle_touch_begin (touch, event);
      break;

    case CLUTTER_TOUCH_UPDATE:
      handle_touch_update (touch, event);
      break;

    case CLUTTER_TOUCH_END:
      handle_touch_end (touch, event);
      break;

    default:
      return FALSE;
    }

  check_send_frame_event (touch, event);
  return FALSE;
}

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
touch_release (struct wl_client   *client,
               struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_touch_interface touch_interface = {
  touch_release,
};

#ifdef HAVE_NATIVE_BACKEND
static void
touch_info_free (MetaWaylandTouchInfo *touch_info)
{
  touch_surface_decrement_touch (touch_info->touch_surface);
  g_free (touch_info);
}
#endif /* HAVE_NATIVE_BACKEND */

void
meta_wayland_touch_cancel (MetaWaylandTouch *touch)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (touch);
  MetaWaylandSeat *seat = meta_wayland_input_device_get_seat (input_device);
  GList *surfaces, *s;

  if (!meta_wayland_seat_has_touch (seat))
    return;

  surfaces = s = touch_get_surfaces (touch, FALSE);

  for (s = surfaces; s; s = s->next)
    {
      MetaWaylandTouchSurface *touch_surface = s->data;
      struct wl_resource *resource;
      struct wl_list *l;

      l = &touch_surface->resource_list;
      wl_resource_for_each(resource, l)
        wl_touch_send_cancel (resource);
    }

  g_hash_table_remove_all (touch->touches);
  g_list_free (surfaces);
}

#ifdef HAVE_NATIVE_BACKEND
static gboolean
evdev_filter_func (struct libinput_event *event,
                   gpointer               data)
{
  MetaWaylandTouch *touch = data;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_TOUCH_DOWN:
    case LIBINPUT_EVENT_TOUCH_UP:
    case LIBINPUT_EVENT_TOUCH_MOTION: {
      struct libinput_event_touch *touch_event;
      int32_t slot;

      touch_event = libinput_event_get_touch_event (event);
      slot = libinput_event_touch_get_slot (touch_event);

      /* XXX: Could theoretically overflow, 64 slots should be
       * enough for most hw/usecases though.
       */
      touch->frame_slots |= (1 << slot);
      break;
    }
    case LIBINPUT_EVENT_TOUCH_CANCEL:
      /* Clutter translates this into individual CLUTTER_TOUCH_CANCEL events,
       * which are not so useful when sending a global signal as the protocol
       * requires.
       */
      meta_wayland_touch_cancel (touch);
      break;
    default:
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
}
#endif

void
meta_wayland_touch_enable (MetaWaylandTouch *touch)
{
#ifdef HAVE_NATIVE_BACKEND
  touch->touch_surfaces = g_hash_table_new_full (NULL, NULL, NULL,
                                                 (GDestroyNotify) touch_surface_free);
  touch->touches = g_hash_table_new_full (NULL, NULL, NULL,
                                          (GDestroyNotify) touch_info_free);
#endif /* HAVE_NATIVE_BACKEND */

  wl_list_init (&touch->resource_list);

#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
  if (META_IS_BACKEND_NATIVE (backend))
    {
      ClutterBackend *backend = clutter_get_default_backend ();
      ClutterSeat *seat = clutter_backend_get_default_seat (backend);

      meta_seat_native_add_filter (META_SEAT_NATIVE (seat),
                                   evdev_filter_func, touch, NULL);
    }
#endif
}

void
meta_wayland_touch_disable (MetaWaylandTouch *touch)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
  if (META_IS_BACKEND_NATIVE (backend))
    {
      ClutterBackend *backend = clutter_get_default_backend ();
      ClutterSeat *seat = clutter_backend_get_default_seat (backend);

      meta_seat_native_remove_filter (META_SEAT_NATIVE (seat),
                                      evdev_filter_func, touch);
    }
#endif

  meta_wayland_touch_cancel (touch);

  g_clear_pointer (&touch->touch_surfaces, g_hash_table_unref);
  g_clear_pointer (&touch->touches, g_hash_table_unref);
}

void
meta_wayland_touch_create_new_resource (MetaWaylandTouch   *touch,
                                        struct wl_client   *client,
                                        struct wl_resource *seat_resource,
                                        uint32_t            id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  struct wl_resource *cr;

  if (!meta_wayland_seat_has_touch (seat))
    {
      wl_resource_post_error (seat_resource, WL_DISPLAY_ERROR_INVALID_METHOD,
                              "Cannot retrieve touch interface without touch capability");
      return;
    }

  cr = wl_resource_create (client, &wl_touch_interface, wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (cr, &touch_interface, touch, unbind_resource);
  wl_list_insert (&touch->resource_list, wl_resource_get_link (cr));
}

gboolean
meta_wayland_touch_can_popup (MetaWaylandTouch *touch,
                              uint32_t          serial)
{
  MetaWaylandTouchInfo *touch_info;
  GHashTableIter iter;

  if (!touch->touches)
    return FALSE;

  g_hash_table_iter_init (&iter, touch->touches);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &touch_info))
    {
      if (touch_info->slot_serial == serial)
        return TRUE;
    }
  return FALSE;
}

ClutterEventSequence *
meta_wayland_touch_find_grab_sequence (MetaWaylandTouch   *touch,
                                       MetaWaylandSurface *surface,
                                       uint32_t            serial)
{
  MetaWaylandTouchInfo *touch_info;
  ClutterEventSequence *sequence;
  GHashTableIter iter;

  if (!touch->touches)
    return NULL;

  g_hash_table_iter_init (&iter, touch->touches);

  while (g_hash_table_iter_next (&iter, (gpointer*) &sequence,
                                 (gpointer*) &touch_info))
    {
      if (touch_info->slot_serial == serial &&
	  touch_info->touch_surface->surface == surface)
        return sequence;
    }

  return NULL;
}

gboolean
meta_wayland_touch_get_press_coords (MetaWaylandTouch     *touch,
                                     ClutterEventSequence *sequence,
                                     gfloat               *x,
                                     gfloat               *y)
{
  MetaWaylandTouchInfo *touch_info;

  if (!touch->touches)
    return FALSE;

  touch_info = g_hash_table_lookup (touch->touches, sequence);

  if (!touch_info)
    return FALSE;

  if (x)
    *x = touch_info->start_x;
  if (y)
    *y = touch_info->start_y;

  return TRUE;
}

static void
meta_wayland_touch_init (MetaWaylandTouch *touch)
{
}

static void
meta_wayland_touch_class_init (MetaWaylandTouchClass *klass)
{
}
