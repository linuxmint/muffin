/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 * Written by:
 *     Owen Taylor <otaylor@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "compositor/meta-surface-actor-x11.h"

#include <X11/extensions/Xcomposite.h>

#include "cogl/winsys/cogl-texture-pixmap-x11.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

struct _MetaSurfaceActorX11
{
  MetaSurfaceActor parent;

  MetaWindow *window;

  MetaDisplay *display;

  CoglTexture *texture;
  Pixmap pixmap;
  Damage damage;

  int last_width;
  int last_height;

  /* This is used to detect fullscreen windows that need to be unredirected */
  guint full_damage_frames_count;
  guint does_full_damage  : 1;

  /* Other state... */
  guint received_damage : 1;
  guint size_changed : 1;

  guint unredirected   : 1;
};

G_DEFINE_TYPE (MetaSurfaceActorX11,
               meta_surface_actor_x11,
               META_TYPE_SURFACE_ACTOR)

static void
free_damage (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay;

  if (self->damage == None)
    return;

  xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  meta_x11_error_trap_push (display->x11_display);
  XDamageDestroy (xdisplay, self->damage);
  self->damage = None;
  meta_x11_error_trap_pop (display->x11_display);
}

static void
detach_pixmap (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));
  Display *xdisplay;

  if (self->pixmap == None)
    return;

  xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  /* Get rid of all references to the pixmap before freeing it; it's unclear whether
   * you are supposed to be able to free a GLXPixmap after freeing the underlying
   * pixmap, but it certainly doesn't work with current DRI/Mesa
   */
  meta_shaped_texture_set_texture (stex, NULL);
  cogl_flush ();

  meta_x11_error_trap_push (display->x11_display);
  XFreePixmap (xdisplay, self->pixmap);
  self->pixmap = None;
  meta_x11_error_trap_pop (display->x11_display);

  g_clear_pointer (&self->texture, cogl_object_unref);
}

static void
set_pixmap (MetaSurfaceActorX11 *self,
            Pixmap               pixmap)
{
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));
  GError *error = NULL;
  CoglTexture *texture;

  g_assert (self->pixmap == None);
  self->pixmap = pixmap;

  texture = COGL_TEXTURE (cogl_texture_pixmap_x11_new (ctx, self->pixmap, FALSE, &error));

  if (error != NULL)
    {
      g_warning ("Failed to allocate stex texture: %s", error->message);
      g_error_free (error);
    }
  else if (G_UNLIKELY (!cogl_texture_pixmap_x11_is_using_tfp_extension (COGL_TEXTURE_PIXMAP_X11 (texture))))
    g_warning ("NOTE: Not using GLX TFP!\n");

  self->texture = texture;
  meta_shaped_texture_set_texture (stex, texture);
}

static void
update_pixmap (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (self->size_changed)
    {
      detach_pixmap (self);
      self->size_changed = FALSE;
    }

  if (self->pixmap == None)
    {
      Pixmap new_pixmap;
      Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

      meta_x11_error_trap_push (display->x11_display);
      new_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (meta_x11_error_trap_pop_with_return (display->x11_display) != Success)
        {
          /* Probably a BadMatch if the window isn't viewable; we could
           * GrabServer/GetWindowAttributes/NameWindowPixmap/UngrabServer/Sync
           * to avoid this, but there's no reason to take two round trips
           * when one will do. (We need that Sync if we want to handle failures
           * for any reason other than !viewable. That's unlikely, but maybe
           * we'll BadAlloc or something.)
           */
          new_pixmap = None;
        }

      if (new_pixmap == None)
        {
          meta_verbose ("Unable to get named pixmap for %s\n",
                        meta_window_get_description (self->window));
          return;
        }

      set_pixmap (self, new_pixmap);
    }
}

gboolean
meta_surface_actor_x11_is_visible (MetaSurfaceActorX11 *self)
{
  return (self->pixmap != None) && !self->unredirected;
}

static void
meta_surface_actor_x11_process_damage (MetaSurfaceActor *actor,
                                       int x, int y, int width, int height)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);

  self->received_damage = TRUE;

  if (meta_window_is_fullscreen (self->window) && !self->unredirected && !self->does_full_damage)
    {
      MetaRectangle window_rect;
      meta_window_get_frame_rect (self->window, &window_rect);

      if (x == 0 &&
          y == 0 &&
          window_rect.width == width &&
          window_rect.height == height)
        self->full_damage_frames_count++;
      else
        self->full_damage_frames_count = 0;

      if (self->full_damage_frames_count >= 100)
        self->does_full_damage = TRUE;
    }

  if (!meta_surface_actor_x11_is_visible (self))
    return;

  cogl_texture_pixmap_x11_update_area (COGL_TEXTURE_PIXMAP_X11 (self->texture),
                                       x, y, width, height);
  meta_surface_actor_update_area (actor, x, y, width, height);
}

static void
meta_surface_actor_x11_pre_paint (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (self->received_damage)
    {
      meta_x11_error_trap_push (display->x11_display);
      XDamageSubtract (xdisplay, self->damage, None, None);
      meta_x11_error_trap_pop (display->x11_display);

      self->received_damage = FALSE;
    }

  update_pixmap (self);
}

static gboolean
meta_surface_actor_x11_is_opaque (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  MetaShapedTexture *stex = meta_surface_actor_get_texture (actor);

  if (meta_surface_actor_x11_is_unredirected (self))
    return TRUE;

  return meta_shaped_texture_is_opaque (stex);
}

gboolean
meta_surface_actor_x11_should_unredirect (MetaSurfaceActorX11 *self)
{
  if (!meta_surface_actor_x11_is_opaque (META_SURFACE_ACTOR (self)))
    return FALSE;

  if (!self->does_full_damage &&
      !meta_window_is_override_redirect (self->window))
    return FALSE;
  return TRUE;
}

static void
sync_unredirected (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

  meta_x11_error_trap_push (display->x11_display);

  if (self->unredirected)
    {
      XCompositeUnredirectWindow (xdisplay, xwindow, CompositeRedirectManual);
      XSync (xdisplay, False);
      detach_pixmap (self);
    }
  else
    {
      XCompositeRedirectWindow (xdisplay, xwindow, CompositeRedirectManual);
      XSync (xdisplay, False);
      clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    }

  meta_x11_error_trap_pop (display->x11_display);
}

void
meta_surface_actor_x11_set_unredirected (MetaSurfaceActorX11 *self,
                                         gboolean             unredirected)
{
  if (self->unredirected == unredirected)
    return;

  self->unredirected = unredirected;
  sync_unredirected (self);
}

gboolean
meta_surface_actor_x11_is_unredirected (MetaSurfaceActorX11 *self)
{
  return self->unredirected;
}

static void
release_x11_resources (MetaSurfaceActorX11 *self)
{
  detach_pixmap (self);
  free_damage (self);
}

static void
meta_surface_actor_x11_dispose (GObject *object)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (object);

  release_x11_resources (self);

  G_OBJECT_CLASS (meta_surface_actor_x11_parent_class)->dispose (object);
}

static void
meta_surface_actor_x11_class_init (MetaSurfaceActorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaSurfaceActorClass *surface_actor_class = META_SURFACE_ACTOR_CLASS (klass);

  object_class->dispose = meta_surface_actor_x11_dispose;

  surface_actor_class->process_damage = meta_surface_actor_x11_process_damage;
  surface_actor_class->pre_paint = meta_surface_actor_x11_pre_paint;
  surface_actor_class->is_opaque = meta_surface_actor_x11_is_opaque;
}

static void
meta_surface_actor_x11_init (MetaSurfaceActorX11 *self)
{
  self->last_width = -1;
  self->last_height = -1;
}

static void
create_damage (MetaSurfaceActorX11 *self)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (self->display->x11_display);
  Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

  self->damage = XDamageCreate (xdisplay, xwindow, XDamageReportBoundingBox);
}

static void
window_decorated_notify (MetaWindow *window,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (user_data);

  release_x11_resources (self);
  create_damage (self);
}

static void
reset_texture (MetaSurfaceActorX11 *self)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));

  if (!self->texture)
    return;

  /* Setting the texture to NULL will cause all the FBO's cached by the
   * shaped texture's MetaTextureTower to be discarded and recreated.
   */
  meta_shaped_texture_set_texture (stex, NULL);
  meta_shaped_texture_set_texture (stex, self->texture);
}

MetaSurfaceActor *
meta_surface_actor_x11_new (MetaWindow *window)
{
  MetaSurfaceActorX11 *self = g_object_new (META_TYPE_SURFACE_ACTOR_X11, NULL);
  MetaDisplay *display = meta_window_get_display (window);

  g_assert (!meta_is_wayland_compositor ());

  self->window = window;
  self->display = display;

  g_signal_connect_object (self->display, "gl-video-memory-purged",
                           G_CALLBACK (reset_texture), self, G_CONNECT_SWAPPED);

  create_damage (self);
  g_signal_connect_object (self->window, "notify::decorated",
                           G_CALLBACK (window_decorated_notify), self, 0);

  g_signal_connect_object (meta_window_actor_from_window (window), "destroy",
                           G_CALLBACK (release_x11_resources), self,
                           G_CONNECT_SWAPPED);

  self->unredirected = FALSE;
  sync_unredirected (self);

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  return META_SURFACE_ACTOR (self);
}

void
meta_surface_actor_x11_set_size (MetaSurfaceActorX11 *self,
                                 int width, int height)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));

  if (self->last_width == width &&
      self->last_height == height)
    return;

  self->size_changed = TRUE;
  self->last_width = width;
  self->last_height = height;
  meta_shaped_texture_set_fallback_size (stex, width, height);
}
