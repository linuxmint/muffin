/*
 * Wayland Support
 *
 * Copyright (C) 2026 the Cinnamon team
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

/* The role for fcitx's classicui candidate window on Wayland
 * (zwp_input_popup_surface_v2). The compositor owns the placement: the client
 * surface is put in a MetaFeedbackActor (a top-layer, compositor-positioned
 * actor) anchored just below the text cursor. */

#include "config.h"

#include "wayland/meta-wayland-input-popup-surface.h"

#include "compositor/meta-feedback-actor-private.h"
#include "core/display-private.h"
#include "wayland/meta-wayland-actor-surface.h"

struct _MetaWaylandInputPopupSurface
{
  MetaWaylandActorSurface parent;

  ClutterActor *feedback_actor;
  graphene_rect_t text_input_rect;
  gboolean visible;
};

G_DEFINE_TYPE (MetaWaylandInputPopupSurface,
               meta_wayland_input_popup_surface,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
sync_position (MetaWaylandInputPopupSurface *popup)
{
  MetaSurfaceActor *surface_actor;
  MetaDisplay *display;
  float popup_width = 0.0f, popup_height = 0.0f;
  float x, y;

  if (!popup->feedback_actor)
    return;

  surface_actor =
    meta_wayland_actor_surface_get_actor (META_WAYLAND_ACTOR_SURFACE (popup));
  if (surface_actor)
    clutter_actor_get_size (CLUTTER_ACTOR (surface_actor),
                            &popup_width, &popup_height);

  /* Default placement: directly below the caret. */
  x = popup->text_input_rect.origin.x;
  y = popup->text_input_rect.origin.y + popup->text_input_rect.size.height;

  /* Keep the candidate window within the caret's monitor: flip above the caret
   * when it would overflow the bottom edge, and slide horizontally to stay
   * within the side edges - the same flip/slide an xdg_positioner would request,
   * focused to this popup. */
  display = meta_get_display ();
  if (display)
    {
      MetaRectangle caret, monitor;
      int monitor_index;

      caret.x = (int) popup->text_input_rect.origin.x;
      caret.y = (int) popup->text_input_rect.origin.y;
      caret.width = (int) popup->text_input_rect.size.width;
      caret.height = (int) popup->text_input_rect.size.height;

      monitor_index = meta_display_get_monitor_index_for_rect (display, &caret);
      meta_display_get_monitor_geometry (display, monitor_index, &monitor);

      if (y + popup_height > monitor.y + monitor.height)
        {
          float above = popup->text_input_rect.origin.y - popup_height;

          if (above >= monitor.y)
            y = above;
        }

      if (x + popup_width > monitor.x + monitor.width)
        x = monitor.x + monitor.width - popup_width;
      if (x < monitor.x)
        x = monitor.x;
    }

  meta_feedback_actor_set_position (META_FEEDBACK_ACTOR (popup->feedback_actor),
                                    x, y);
}

void
meta_wayland_input_popup_surface_set_text_input_rect (MetaWaylandInputPopupSurface *popup,
                                                      const graphene_rect_t        *rect)
{
  popup->text_input_rect = *rect;
  sync_position (popup);
}

void
meta_wayland_input_popup_surface_set_visible (MetaWaylandInputPopupSurface *popup,
                                              gboolean                      visible)
{
  popup->visible = visible;

  if (!popup->feedback_actor)
    return;

  if (visible)
    clutter_actor_show (popup->feedback_actor);
  else
    clutter_actor_hide (popup->feedback_actor);
}

static void
input_popup_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandInputPopupSurface *popup =
    META_WAYLAND_INPUT_POPUP_SURFACE (actor_surface);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_input_popup_surface_parent_class);
  MetaSurfaceActor *surface_actor =
    meta_wayland_actor_surface_get_actor (actor_surface);

  if (surface_actor && !popup->feedback_actor)
    {
      ClutterActor *actor = CLUTTER_ACTOR (surface_actor);
      ClutterActor *parent = clutter_actor_get_parent (actor);

      if (parent)
        clutter_actor_remove_child (parent, actor);

      /* meta_feedback_actor_new parents into the feedback group, the compositor's
       * topmost overlay layer, so the candidate popup renders above everything
       * (Cinnamon keeps that group at the top of the stage). */
      popup->feedback_actor = meta_feedback_actor_new (0, 0);
      meta_feedback_actor_set_anchor (META_FEEDBACK_ACTOR (popup->feedback_actor),
                                      0, 0);
      clutter_actor_add_child (popup->feedback_actor, actor);
      if (!popup->visible)
        clutter_actor_hide (popup->feedback_actor);
      sync_position (popup);
    }

  actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_input_popup_surface_dispose (GObject *object)
{
  MetaWaylandInputPopupSurface *popup =
    META_WAYLAND_INPUT_POPUP_SURFACE (object);

  if (popup->feedback_actor)
    {
      clutter_actor_remove_all_children (popup->feedback_actor);
      clutter_actor_destroy (popup->feedback_actor);
      popup->feedback_actor = NULL;
    }

  G_OBJECT_CLASS (meta_wayland_input_popup_surface_parent_class)->dispose (object);
}

static void
meta_wayland_input_popup_surface_init (MetaWaylandInputPopupSurface *popup)
{
}

static void
meta_wayland_input_popup_surface_class_init (MetaWaylandInputPopupSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  object_class->dispose = meta_wayland_input_popup_surface_dispose;

  actor_surface_class->sync_actor_state = input_popup_surface_sync_actor_state;
}
