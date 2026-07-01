/*
 * Copyright (C) 2024 Linux Mint
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
 */

#include "config.h"

#include "meta/meta-wayland-background-actor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "meta/compositor-muffin.h"
#include "wayland/meta-wayland-layer-shell.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-private.h"

struct _MetaWaylandBackgroundActor
{
    ClutterActor parent_instance;

    MetaDisplay *display;
    int monitor_index;
    float dim_factor;

    ClutterActor *clone;
    MetaWaylandLayerSurface *tracked_surface;
    MetaWaylandLayerShell *layer_shell;
    gulong mapped_handler_id;
    gulong unmapped_handler_id;
};

enum
{
    PROP_0,
    PROP_DIM_FACTOR,
    PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (MetaWaylandBackgroundActor, meta_wayland_background_actor, CLUTTER_TYPE_ACTOR)

static void
meta_wayland_background_actor_remove_clone (MetaWaylandBackgroundActor *self)
{
    if (self->clone)
    {
        clutter_actor_remove_child (CLUTTER_ACTOR (self), self->clone);
        self->clone = NULL;
    }

    self->tracked_surface = NULL;
}

static void
meta_wayland_background_actor_attach_surface (MetaWaylandBackgroundActor *self,
                                               MetaWaylandLayerSurface    *layer_surface)
{
    MetaWaylandActorSurface *actor_surface;
    MetaSurfaceActor *surface_actor;

    meta_wayland_background_actor_remove_clone (self);

    actor_surface = META_WAYLAND_ACTOR_SURFACE (layer_surface);
    surface_actor = meta_wayland_actor_surface_get_actor (actor_surface);

    if (!surface_actor)
        return;

    self->tracked_surface = layer_surface;
    self->clone = clutter_clone_new (CLUTTER_ACTOR (surface_actor));
    clutter_actor_add_child (CLUTTER_ACTOR (self), self->clone);
}

static MetaWaylandOutput *
get_output_for_monitor (MetaWaylandBackgroundActor *self)
{
    MetaWaylandCompositor *compositor;

    compositor = meta_wayland_compositor_get_default ();
    return meta_wayland_compositor_get_output_for_monitor (compositor,
                                                            self->monitor_index);
}

static void
on_layer_surface_mapped (MetaWaylandLayerShell   *layer_shell,
                         MetaWaylandLayerSurface *layer_surface,
                         gpointer                 user_data)
{
    MetaWaylandBackgroundActor *self = user_data;
    MetaWaylandOutput *output;

    if (self->tracked_surface != NULL)
        return;

    if (meta_wayland_layer_surface_get_layer (layer_surface) !=
        META_LAYER_SHELL_LAYER_BACKGROUND)
        return;

    output = get_output_for_monitor (self);
    if (output && meta_wayland_layer_surface_get_output (layer_surface) != output)
        return;

    meta_wayland_background_actor_attach_surface (self, layer_surface);
}

static void
on_layer_surface_unmapped (MetaWaylandLayerShell   *layer_shell,
                           MetaWaylandLayerSurface *layer_surface,
                           gpointer                 user_data)
{
    MetaWaylandBackgroundActor *self = user_data;

    if (self->tracked_surface != layer_surface)
        return;

    meta_wayland_background_actor_remove_clone (self);
}

static void
meta_wayland_background_actor_disconnect_signals (MetaWaylandBackgroundActor *self)
{
    if (self->layer_shell)
    {
        if (self->mapped_handler_id != 0)
        {
            g_signal_handler_disconnect (self->layer_shell, self->mapped_handler_id);
            self->mapped_handler_id = 0;
        }

        if (self->unmapped_handler_id != 0)
        {
            g_signal_handler_disconnect (self->layer_shell, self->unmapped_handler_id);
            self->unmapped_handler_id = 0;
        }

        self->layer_shell = NULL;
    }
}

static void
meta_wayland_background_actor_dispose (GObject *object)
{
    MetaWaylandBackgroundActor *self = META_WAYLAND_BACKGROUND_ACTOR (object);

    meta_wayland_background_actor_disconnect_signals (self);
    meta_wayland_background_actor_remove_clone (self);

    G_OBJECT_CLASS (meta_wayland_background_actor_parent_class)->dispose (object);
}

static gboolean
get_monitor_rect (MetaWaylandBackgroundActor *self,
                  MetaRectangle              *rect)
{
    MetaBackend *backend = meta_get_backend ();
    MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
    MetaLogicalMonitor *logical_monitor;

    logical_monitor = meta_monitor_manager_get_logical_monitor_from_number (
        monitor_manager, self->monitor_index);

    if (!logical_monitor)
        return FALSE;

    *rect = logical_monitor->rect;
    return TRUE;
}

static void
meta_wayland_background_actor_get_preferred_width (ClutterActor *actor,
                                                    gfloat        for_height,
                                                    gfloat       *min_width_p,
                                                    gfloat       *natural_width_p)
{
    MetaRectangle rect;
    float width = 0;

    if (get_monitor_rect (META_WAYLAND_BACKGROUND_ACTOR (actor), &rect))
        width = rect.width;

    if (min_width_p)
        *min_width_p = width;
    if (natural_width_p)
        *natural_width_p = width;
}

static void
meta_wayland_background_actor_get_preferred_height (ClutterActor *actor,
                                                     gfloat        for_width,
                                                     gfloat       *min_height_p,
                                                     gfloat       *natural_height_p)
{
    MetaRectangle rect;
    float height = 0;

    if (get_monitor_rect (META_WAYLAND_BACKGROUND_ACTOR (actor), &rect))
        height = rect.height;

    if (min_height_p)
        *min_height_p = height;
    if (natural_height_p)
        *natural_height_p = height;
}

static gboolean
meta_wayland_background_actor_get_paint_volume (ClutterActor       *actor,
                                                 ClutterPaintVolume *volume)
{
    MetaRectangle rect;

    if (!get_monitor_rect (META_WAYLAND_BACKGROUND_ACTOR (actor), &rect))
        return FALSE;

    clutter_paint_volume_set_width (volume, rect.width);
    clutter_paint_volume_set_height (volume, rect.height);

    return TRUE;
}

static void
meta_wayland_background_actor_set_dim_factor (MetaWaylandBackgroundActor *self,
                                               float                       dim_factor)
{
    if (self->dim_factor == dim_factor)
        return;

    self->dim_factor = dim_factor;
    clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DIM_FACTOR]);
}

static void
meta_wayland_background_actor_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
    MetaWaylandBackgroundActor *self = META_WAYLAND_BACKGROUND_ACTOR (object);

    switch (prop_id)
    {
    case PROP_DIM_FACTOR:
        g_value_set_float (value, self->dim_factor);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
meta_wayland_background_actor_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
    MetaWaylandBackgroundActor *self = META_WAYLAND_BACKGROUND_ACTOR (object);

    switch (prop_id)
    {
    case PROP_DIM_FACTOR:
        meta_wayland_background_actor_set_dim_factor (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
meta_wayland_background_actor_class_init (MetaWaylandBackgroundActorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

    object_class->dispose = meta_wayland_background_actor_dispose;
    object_class->get_property = meta_wayland_background_actor_get_property;
    object_class->set_property = meta_wayland_background_actor_set_property;

    actor_class->get_preferred_width = meta_wayland_background_actor_get_preferred_width;
    actor_class->get_preferred_height = meta_wayland_background_actor_get_preferred_height;
    actor_class->get_paint_volume = meta_wayland_background_actor_get_paint_volume;

    obj_props[PROP_DIM_FACTOR] =
        g_param_spec_float ("dim-factor",
                            "Dim factor",
                            "Factor to dim the background by",
                            0.0, 1.0, 1.0,
                            G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

static void
meta_wayland_background_actor_init (MetaWaylandBackgroundActor *self)
{
    self->dim_factor = 1.0;
}

/**
 * meta_wayland_background_actor_new_for_monitor:
 * @display: a #MetaDisplay
 * @monitor: the monitor index
 *
 * Creates a new actor that clones the layer-shell background surface
 * for the given monitor. The background is provided by cinnamon-settings-daemon
 * as a layer-shell surface on the BACKGROUND layer with namespace "desktop".
 *
 * Return value: (transfer full): the newly created background actor
 */
ClutterActor *
meta_wayland_background_actor_new_for_monitor (MetaDisplay *display,
                                                int          monitor)
{
    MetaWaylandBackgroundActor *self;
    MetaWaylandCompositor *compositor;
    MetaWaylandLayerShell *layer_shell;
    MetaWaylandOutput *output;
    MetaWaylandLayerSurface *surface;

    g_return_val_if_fail (META_IS_DISPLAY (display), NULL);
    g_return_val_if_fail (meta_is_wayland_compositor (), NULL);

    self = g_object_ref_sink (g_object_new (META_TYPE_WAYLAND_BACKGROUND_ACTOR, NULL));
    self->display = display;
    self->monitor_index = monitor;

    compositor = meta_wayland_compositor_get_default ();
    layer_shell = meta_wayland_compositor_get_layer_shell (compositor);

    if (!layer_shell)
        return CLUTTER_ACTOR (self);

    self->layer_shell = layer_shell;

    output = meta_wayland_compositor_get_output_for_monitor (compositor, monitor);

    surface = meta_wayland_layer_shell_find_surface (layer_shell,
                                                      META_LAYER_SHELL_LAYER_BACKGROUND,
                                                      NULL,
                                                      output);

    if (surface)
        meta_wayland_background_actor_attach_surface (self, surface);

    self->mapped_handler_id =
        g_signal_connect (layer_shell, "layer-surface-mapped",
                          G_CALLBACK (on_layer_surface_mapped), self);

    self->unmapped_handler_id =
        g_signal_connect (layer_shell, "layer-surface-unmapped",
                          G_CALLBACK (on_layer_surface_unmapped), self);

    return CLUTTER_ACTOR (self);
}
