/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include <config.h>

#define _ISOC99_SOURCE /* for roundf */
#include <math.h>

#include <gdk/gdk.h> /* for gdk_rectangle_intersect() */

#include "clutter-utils.h"
#include "compositor-private.h"
#include "meta-window-actor-private.h"
#include "meta-window-group.h"
#include "meta-background-actor-private.h"
#include "meta-background-group-private.h"

struct _MetaWindowGroupClass
{
  ClutterActorClass parent_class;
};

struct _MetaWindowGroup
{
  ClutterActor parent;

  MetaScreen *screen;
};

G_DEFINE_TYPE (MetaWindowGroup, meta_window_group, CLUTTER_TYPE_ACTOR);

static void
meta_window_group_paint (ClutterActor *actor)
{
  cairo_region_t *visible_region;
  ClutterActor *stage;
  cairo_rectangle_int_t visible_rect;
  GList *children, *l;
  int paint_x_origin, paint_y_origin;
  int actor_x_origin, actor_y_origin;
  int paint_x_offset, paint_y_offset;
  int screen_width, screen_height;

  MetaWindowGroup *window_group = META_WINDOW_GROUP (actor);
  MetaCompScreen *info = meta_screen_get_compositor_data (window_group->screen);

  meta_screen_get_size (window_group->screen, &screen_width, &screen_height);

  /* Normally we expect an actor to be drawn at it's position on the screen.
   * However, if we're inside the paint of a ClutterClone, that won't be the
   * case and we need to compensate. We look at the position of the window
   * group under the current model-view matrix and the position of the actor.
   * If they are both simply integer translations, then we can compensate
   * easily, otherwise we give up.
   *
   * Possible cleanup: work entirely in paint space - we can compute the
   * combination of the model-view matrix with the local matrix for each child
   * actor and get a total transformation for that actor for how we are
   * painting currently, and never worry about how actors are positioned
   * on the stage.
   */
  if (!meta_actor_painting_untransformed (screen_width, screen_height, &paint_x_origin, &paint_y_origin) ||
      !meta_actor_is_untransformed (actor, &actor_x_origin, &actor_y_origin))
    {
      CLUTTER_ACTOR_CLASS (meta_window_group_parent_class)->paint (actor);
      return;
    }

  paint_x_offset = paint_x_origin - actor_x_origin;
  paint_y_offset = paint_y_origin - actor_y_origin;

  /* We walk the list from top to bottom (opposite of painting order),
   * and subtract the opaque area of each window out of the visible
   * region that we pass to the windows below.
   */
  children = clutter_actor_get_children (actor);
  children = g_list_reverse (children);

  /* Get the clipped redraw bounds from Clutter so that we can avoid
   * painting shadows on windows that don't need to be painted in this
   * frame. In the case of a multihead setup with mismatched monitor
   * sizes, we could intersect this with an accurate union of the
   * monitors to avoid painting shadows that are visible only in the
   * holes. */
  stage = clutter_actor_get_stage (actor);
  clutter_stage_get_redraw_clip_bounds (CLUTTER_STAGE (stage),
                                        &visible_rect);

  visible_region = cairo_region_create_rectangle (&visible_rect);

  if (info->unredirected_window != NULL)
    {
      cairo_rectangle_int_t unredirected_rect;
      MetaWindow *window = meta_window_actor_get_meta_window (info->unredirected_window);
      meta_window_get_outer_rect (window, (MetaRectangle*) &unredirected_rect);
      cairo_region_subtract_rectangle (visible_region, &unredirected_rect);
    }

  for (l = children; l; l = l->next)
    {
      if (!CLUTTER_ACTOR_IS_VISIBLE (l->data))
        continue;

      if (l->data == info->unredirected_window)
        continue;

      /* If an actor has effects applied, then that can change the area
       * it paints and the opacity, so we no longer can figure out what
       * portion of the actor is obscured and what portion of the screen
       * it obscures, so we skip the actor.
       *
       * This has a secondary beneficial effect: if a ClutterOffscreenEffect
       * is applied to an actor, then our clipped redraws interfere with the
       * caching of the FBO - even if we only need to draw a small portion
       * of the window right now, ClutterOffscreenEffect may use other portions
       * of the FBO later. So, skipping actors with effects applied also
       * prevents these bugs.
       *
       * Theoretically, we should check clutter_actor_get_offscreen_redirect()
       * as well for the same reason, but omitted for simplicity in the
       * hopes that no-one will do that.
       */
      if (clutter_actor_has_effects (l->data))
        continue;

      if (META_IS_WINDOW_ACTOR (l->data))
        {
          MetaWindowActor *window_actor = l->data;
          int x, y;

          if (!meta_actor_is_untransformed (CLUTTER_ACTOR (window_actor), &x, &y))
            continue;

          x += paint_x_offset;
          y += paint_y_offset;

          /* Temporarily move to the coordinate system of the actor */
          cairo_region_translate (visible_region, - x, - y);

          meta_window_actor_set_visible_region (window_actor, visible_region);

          if (clutter_actor_get_paint_opacity (CLUTTER_ACTOR (window_actor)) == 0xff)
            {
              cairo_region_t *obscured_region = meta_window_actor_get_obscured_region (window_actor);
              if (obscured_region)
                cairo_region_subtract (visible_region, obscured_region);
            }

          meta_window_actor_set_visible_region_beneath (window_actor, visible_region);
          cairo_region_translate (visible_region, x, y);
        }
      else if (META_IS_BACKGROUND_ACTOR (l->data) ||
               META_IS_BACKGROUND_GROUP (l->data))
        {
          ClutterActor *background_actor = l->data;
          int x, y;

          if (!meta_actor_is_untransformed (CLUTTER_ACTOR (background_actor), &x, &y))
            continue;

          x += paint_x_offset;
          y += paint_y_offset;

          cairo_region_translate (visible_region, - x, - y);
          
          if (META_IS_BACKGROUND_GROUP (background_actor))
            meta_background_group_set_visible_region (META_BACKGROUND_GROUP (background_actor), visible_region);
          else
            meta_background_actor_set_visible_region (META_BACKGROUND_ACTOR (background_actor), visible_region);
          cairo_region_translate (visible_region, x, y);
        }
    }

  cairo_region_destroy (visible_region);

  CLUTTER_ACTOR_CLASS (meta_window_group_parent_class)->paint (actor);

  /* Now that we are done painting, unset the visible regions (they will
   * mess up painting clones of our actors)
   */
  for (l = children; l; l = l->next)
    {
      if (META_IS_WINDOW_ACTOR (l->data))
        {
          MetaWindowActor *window_actor = l->data;
          meta_window_actor_reset_visible_regions (window_actor);
        }
      else if (META_IS_BACKGROUND_ACTOR (l->data))
        {
          MetaBackgroundActor *background_actor = l->data;
          meta_background_actor_set_visible_region (background_actor, NULL);
        }
    }

  g_list_free (children);
}

/* Adapted from clutter_actor_update_default_paint_volume() */
static gboolean
meta_window_group_get_paint_volume (ClutterActor       *self,
                                    ClutterPaintVolume *volume)
{
  ClutterActorIter iter;
  ClutterActor *child;

  clutter_actor_iter_init (&iter, self);
  while (clutter_actor_iter_next (&iter, &child))
    {
      const ClutterPaintVolume *child_volume;

      if (!CLUTTER_ACTOR_IS_MAPPED (child))
        continue;

      child_volume = clutter_actor_get_transformed_paint_volume (child, self);
      if (child_volume == NULL)
        return FALSE;

      clutter_paint_volume_union (volume, child_volume);
    }

  return TRUE;
}

static void
meta_window_group_class_init (MetaWindowGroupClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->paint = meta_window_group_paint;
  actor_class->get_paint_volume = meta_window_group_get_paint_volume;
}

static void
meta_window_group_init (MetaWindowGroup *window_group)
{
}

LOCAL_SYMBOL ClutterActor *
meta_window_group_new (MetaScreen *screen)
{
  MetaWindowGroup *window_group;

  window_group = g_object_new (META_TYPE_WINDOW_GROUP, NULL);

  window_group->screen = screen;

  return CLUTTER_ACTOR (window_group);
}
