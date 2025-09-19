/*
 * Wayland Support
 *
 * Copyright (C) 2024 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "wayland/meta-wayland-xdg-dialog.h"

#include "core/window-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-xdg-shell.h"
#include "wayland/meta-wayland-versions.h"

#include "xdg-dialog-v1-server-protocol.h"

static GQuark quark_xdg_dialog_data = 0;

typedef struct _MetaWaylandXdgDialog
{
    struct wl_resource *resource;
    MetaWaylandXdgSurface *toplevel;
    gboolean is_modal;
} MetaWaylandXdgDialog;

struct _MetaWaylandXdgWmDialog
{
    GObject parent;
    struct wl_list resources;
};

#define META_TYPE_WAYLAND_XDG_WM_DIALOG (meta_wayland_xdg_wm_dialog_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandXdgWmDialog, meta_wayland_xdg_wm_dialog,
                      META, WAYLAND_XDG_WM_DIALOG, GObject)

G_DEFINE_TYPE (MetaWaylandXdgWmDialog, meta_wayland_xdg_wm_dialog, G_TYPE_OBJECT)

static void
xdg_dialog_destructor (struct wl_resource *resource)
{
    MetaWaylandXdgDialog *xdg_dialog = wl_resource_get_user_data (resource);

    if (xdg_dialog->toplevel)
    {
        g_object_steal_qdata (G_OBJECT (xdg_dialog->toplevel),
                              quark_xdg_dialog_data);
    }

    g_free (xdg_dialog);
}

static void
xdg_dialog_set_modal (struct wl_client   *client,
                      struct wl_resource *resource)
{
    MetaWaylandXdgDialog *xdg_dialog = wl_resource_get_user_data (resource);
    MetaWaylandXdgSurface *xdg_surface;

    xdg_surface = xdg_dialog->toplevel;

    if (xdg_surface && !xdg_dialog->is_modal)
    {
        MetaWaylandSurfaceRole *surface_role =
        META_WAYLAND_SURFACE_ROLE (xdg_surface);
        MetaWaylandSurface *surface =
        meta_wayland_surface_role_get_surface (surface_role);
        MetaWindow *window = meta_wayland_surface_get_window (surface);

        xdg_dialog->is_modal = TRUE;
        meta_window_set_type (window, META_WINDOW_MODAL_DIALOG);
    }
}

static void
xdg_dialog_unset_modal (struct wl_client   *client,
                        struct wl_resource *resource)
{
    MetaWaylandXdgDialog *xdg_dialog = wl_resource_get_user_data (resource);
    MetaWaylandXdgSurface *xdg_surface;

    xdg_surface = xdg_dialog->toplevel;

    if (xdg_surface && xdg_dialog->is_modal)
    {
        MetaWaylandSurfaceRole *surface_role =
        META_WAYLAND_SURFACE_ROLE (xdg_surface);
        MetaWaylandSurface *surface =
        meta_wayland_surface_role_get_surface (surface_role);
        MetaWindow *window = meta_wayland_surface_get_window (surface);

        xdg_dialog->is_modal = FALSE;
        meta_window_set_type (window, META_WINDOW_NORMAL);
    }
}

static void
xdg_dialog_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
    wl_resource_destroy (resource);
}

static const struct xdg_dialog_v1_interface meta_wayland_xdg_dialog_interface = {
    xdg_dialog_destroy,
    xdg_dialog_set_modal,
    xdg_dialog_unset_modal,
};

static void
xdg_dialog_toplevel_destroyed (MetaWaylandXdgDialog *xdg_dialog)
{
    xdg_dialog->toplevel = NULL;
}

static void
xdg_wm_dialog_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
    wl_resource_destroy (resource);
}

static void
xdg_wm_dialog_get_xdg_dialog (struct wl_client   *client,
                              struct wl_resource *resource,
                              uint32_t            id,
                              struct wl_resource *toplevel_resource)
{
    MetaWaylandXdgSurface *xdg_surface = wl_resource_get_user_data (toplevel_resource);
    MetaWaylandXdgDialog *xdg_dialog;

    xdg_dialog = g_object_get_qdata (G_OBJECT (xdg_surface), quark_xdg_dialog_data);
    if (xdg_dialog)
    {
        wl_resource_post_error (toplevel_resource,
                                XDG_WM_DIALOG_V1_ERROR_ALREADY_USED,
                                "xdg_wm_dialog_v1::get_xdg_dialog already requested");
        return;
    }

    xdg_dialog = g_new0 (MetaWaylandXdgDialog, 1);
    xdg_dialog->toplevel = xdg_surface;
    xdg_dialog->resource = wl_resource_create (client,
                                               &xdg_dialog_v1_interface,
                                               wl_resource_get_version (resource),
                                               id);
    wl_resource_set_implementation (xdg_dialog->resource,
                                    &meta_wayland_xdg_dialog_interface,
                                    xdg_dialog, xdg_dialog_destructor);

    g_object_set_qdata_full (G_OBJECT (xdg_surface),
                             quark_xdg_dialog_data,
                             xdg_dialog,
                             (GDestroyNotify) xdg_dialog_toplevel_destroyed);
}

static const struct xdg_wm_dialog_v1_interface meta_wayland_xdg_wm_dialog_interface = {
    xdg_wm_dialog_destroy,
    xdg_wm_dialog_get_xdg_dialog,
};

static void
unbind_resource (struct wl_resource *resource)
{
    wl_list_remove (wl_resource_get_link (resource));
}

static void
bind_xdg_wm_dialog (struct wl_client *client,
                    void             *data,
                    uint32_t          version,
                    uint32_t          id)
{
    MetaWaylandXdgWmDialog *xdg_wm_dialog = data;
    struct wl_resource *resource;

    resource = wl_resource_create (client, &xdg_wm_dialog_v1_interface, version, id);
    wl_resource_set_implementation (resource, &meta_wayland_xdg_wm_dialog_interface,
                                    data, unbind_resource);




    wl_list_insert (&xdg_wm_dialog->resources, wl_resource_get_link (resource));
}

static void
meta_wayland_xdg_wm_dialog_init (MetaWaylandXdgWmDialog *xdg_wm_dialog)
{
    wl_list_init (&xdg_wm_dialog->resources);
}

static void
meta_wayland_xdg_wm_dialog_class_init (MetaWaylandXdgWmDialogClass *klass)
{
    quark_xdg_dialog_data =
    g_quark_from_static_string ("-meta-wayland-xdg-wm-dialog-surface-data");
}

static MetaWaylandXdgWmDialog *
meta_wayland_xdg_wm_dialog_new (MetaWaylandCompositor *compositor)
{
    MetaWaylandXdgWmDialog *xdg_wm_dialog;

    xdg_wm_dialog = g_object_new (META_TYPE_WAYLAND_XDG_WM_DIALOG, NULL);

    if (wl_global_create (compositor->wayland_display,
        &xdg_wm_dialog_v1_interface,
        META_XDG_DIALOG_VERSION,
        xdg_wm_dialog, bind_xdg_wm_dialog) == NULL)
        g_error ("Failed to register a global xdg-dialog object");

    return xdg_wm_dialog;
}

void
meta_wayland_init_xdg_wm_dialog (MetaWaylandCompositor *compositor)
{
    g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-xdg-wm-dialog",
                            meta_wayland_xdg_wm_dialog_new (compositor),
                            g_object_unref);
}
