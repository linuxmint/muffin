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

    MetaWaylandLayerShell *layer_shell;
    gulong mapped_handler_id;
    gulong unmapped_handler_id;
    gulong monitors_changed_handler_id;
};

enum
{
    PROP_0,
    PROP_DIM_FACTOR,
    PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (MetaWaylandBackgroundActor, meta_wayland_background_actor, CLUTTER_TYPE_ACTOR)

static gboolean get_monitor_rect (MetaWaylandBackgroundActor *self,
                                  MetaRectangle              *rect);
static MetaLogicalMonitor * get_logical_monitor (MetaWaylandBackgroundActor *self);

static void
meta_wayland_background_actor_add_clone (MetaWaylandBackgroundActor *self,
                                          MetaWaylandLayerSurface    *layer_surface,
                                          MetaRectangle              *rect)
{
    MetaWaylandActorSurface *actor_surface;
    MetaSurfaceActor *surface_actor;
    ClutterActor *clone;
    double scale_x, scale_y;

    actor_surface = META_WAYLAND_ACTOR_SURFACE (layer_surface);
    surface_actor = meta_wayland_actor_surface_get_actor (actor_surface);

    if (!surface_actor)
        return;

    /* Clones all keep the default z_position, so each add_child () lands its
     * child last - see meta_wayland_layer_shell_find_surfaces (). */
    clone = clutter_clone_new (CLUTTER_ACTOR (surface_actor));
    clutter_actor_add_child (CLUTTER_ACTOR (self), clone);

    /* A clone paints the source's contents at its own allocation and does not
     * inherit the source's scale transform, so the geometry scale the layer
     * surface applied to itself has to be copied across by hand. */
    clutter_actor_get_scale (CLUTTER_ACTOR (surface_actor), &scale_x, &scale_y);
    clutter_actor_set_scale (clone, scale_x, scale_y);

    /* The source sits at absolute stage coordinates; this actor is anchored at
     * the monitor origin, so the offset cancels the monitor's own position and
     * leaves the clone local to it. */
    clutter_actor_add_constraint (clone,
        clutter_bind_constraint_new (CLUTTER_ACTOR (surface_actor),
                                     CLUTTER_BIND_X, -rect->x));
    clutter_actor_add_constraint (clone,
        clutter_bind_constraint_new (CLUTTER_ACTOR (surface_actor),
                                     CLUTTER_BIND_Y, -rect->y));
}

static void
meta_wayland_background_actor_sync_clones (MetaWaylandBackgroundActor *self)
{
    GList *surfaces, *l;
    MetaRectangle rect;

    clutter_actor_destroy_all_children (CLUTTER_ACTOR (self));

    if (!self->layer_shell || !get_monitor_rect (self, &rect))
        return;

    surfaces = meta_wayland_layer_shell_find_surfaces (self->layer_shell,
                                                        META_LAYER_SHELL_LAYER_BACKGROUND,
                                                        get_logical_monitor (self));

    for (l = surfaces; l; l = l->next)
        meta_wayland_background_actor_add_clone (self, l->data, &rect);

    g_list_free (surfaces);
}

static void
on_layer_surface_mapped (MetaWaylandLayerShell   *layer_shell,
                         MetaWaylandLayerSurface *layer_surface,
                         gpointer                 user_data)
{
    MetaWaylandBackgroundActor *self = user_data;

    if (meta_wayland_layer_surface_get_layer (layer_surface) !=
        META_LAYER_SHELL_LAYER_BACKGROUND)
        return;

    meta_wayland_background_actor_sync_clones (self);
}

static void
on_layer_surface_unmapped (MetaWaylandLayerShell   *layer_shell,
                           MetaWaylandLayerSurface *layer_surface,
                           gpointer                 user_data)
{
    MetaWaylandBackgroundActor *self = user_data;

    if (meta_wayland_layer_surface_get_layer (layer_surface) !=
        META_LAYER_SHELL_LAYER_BACKGROUND)
        return;

    meta_wayland_background_actor_sync_clones (self);
}

static void
on_monitors_changed (MetaMonitorManager         *monitor_manager,
                     MetaWaylandBackgroundActor *self)
{
    meta_wayland_background_actor_sync_clones (self);
}

static void
meta_wayland_background_actor_disconnect_signals (MetaWaylandBackgroundActor *self)
{
    MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (meta_get_backend ());

    g_clear_signal_handler (&self->monitors_changed_handler_id, monitor_manager);

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
    clutter_actor_destroy_all_children (CLUTTER_ACTOR (self));

    G_OBJECT_CLASS (meta_wayland_background_actor_parent_class)->dispose (object);
}

static MetaLogicalMonitor *
get_logical_monitor (MetaWaylandBackgroundActor *self)
{
    MetaBackend *backend = meta_get_backend ();
    MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);

    return meta_monitor_manager_get_logical_monitor_from_number (
        monitor_manager, self->monitor_index);
}

static gboolean
get_monitor_rect (MetaWaylandBackgroundActor *self,
                  MetaRectangle              *rect)
{
    MetaLogicalMonitor *logical_monitor = get_logical_monitor (self);

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
    ClutterColor color = { 0x00, 0x00, 0x00, 0xff };

    self->dim_factor = 1.0;

    /* This actor paints nothing itself - it only hosts clones of whatever
     * surfaces are mapped in the BACKGROUND layer on its monitor. With no
     * wallpaper client running there are no clones and the actor is fully
     * transparent, so keep an opaque fill underneath. It also covers the
     * remainder when a background surface is smaller than its monitor.
     *
     * Black to match the stage backdrop Cinnamon paints behind its uiGroup
     * (js/ui/main.js), so a missing wallpaper looks the same either way. */
    clutter_actor_set_background_color (CLUTTER_ACTOR (self), &color);
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

    g_return_val_if_fail (META_IS_DISPLAY (display), NULL);
    g_return_val_if_fail (meta_is_wayland_compositor (), NULL);

    self = g_object_ref_sink (g_object_new (META_TYPE_WAYLAND_BACKGROUND_ACTOR, NULL));
    self->display = display;
    self->monitor_index = monitor;

    self->monitors_changed_handler_id =
        g_signal_connect (meta_backend_get_monitor_manager (meta_get_backend ()),
                          "monitors-changed-internal",
                          G_CALLBACK (on_monitors_changed), self);

    compositor = meta_wayland_compositor_get_default ();
    layer_shell = meta_wayland_compositor_get_layer_shell (compositor);

    if (!layer_shell)
        return CLUTTER_ACTOR (self);

    self->layer_shell = layer_shell;

    meta_wayland_background_actor_sync_clones (self);

    self->mapped_handler_id =
        g_signal_connect (layer_shell, "layer-surface-mapped",
                          G_CALLBACK (on_layer_surface_mapped), self);

    self->unmapped_handler_id =
        g_signal_connect (layer_shell, "layer-surface-unmapped",
                          G_CALLBACK (on_layer_surface_unmapped), self);

    return CLUTTER_ACTOR (self);
}
