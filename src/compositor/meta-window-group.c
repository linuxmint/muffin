/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include <config.h>

#define _ISOC99_SOURCE /* for roundf */
#include <math.h>

#include <gdk/gdk.h> /* for gdk_rectangle_intersect() */

#include <core/screen-private.h>
#include <core/window-private.h>
#include "clutter-utils.h"
#include "compositor-private.h"
#include "meta-window-actor-private.h"
#include "meta-window-group.h"
#include "meta-background-actor-private.h"

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
meta_window_group_cull_out (MetaWindowGroup *group,
                            ClutterActor    *unredirected_window,
                            gboolean         has_unredirected_window,
                            cairo_region_t  *unobscured_region,
                            cairo_region_t  *clip_region)
{
  ClutterActor *actor = CLUTTER_ACTOR (group);
  ClutterActor *child;
  ClutterActorIter iter;

  /* We walk the list from top to bottom (opposite of painting order),
   * and subtract the opaque area of each window out of the visible
   * region that we pass to the windows below.
   */
  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_prev (&iter, &child))
    {
      if (!CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      if (has_unredirected_window && child == unredirected_window)
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
      if (clutter_actor_has_effects (child))
        continue;

      if (META_IS_WINDOW_ACTOR (child))
        {
          MetaWindowActor *window_actor = META_WINDOW_ACTOR (child);
          int x, y;

          if (!meta_actor_is_untransformed (CLUTTER_ACTOR (window_actor), &x, &y))
            continue;

          /* Temporarily move to the coordinate system of the actor */
          cairo_region_translate (unobscured_region, - x, - y);
          cairo_region_translate (clip_region, - x, - y);

          meta_window_actor_set_unobscured_region (window_actor, unobscured_region);
          meta_window_actor_set_clip_region (window_actor, clip_region);

          if (clutter_actor_get_paint_opacity (CLUTTER_ACTOR (window_actor)) == 0xff)
            {
              MetaWindowActorPrivate *priv = window_actor->priv;
              cairo_region_t *obscured_region = NULL;

              if (priv->opaque_region && priv->pixmap && priv->opacity == 0xff)
                {
                  cairo_region_subtract (unobscured_region, priv->opaque_region);
                  cairo_region_subtract (clip_region, priv->opaque_region);
                }
            }

          meta_window_actor_set_clip_region_beneath (window_actor, clip_region);

          cairo_region_translate (unobscured_region, x, y);
          cairo_region_translate (clip_region, x, y);
        }
      else if (META_IS_BACKGROUND_ACTOR (child))
        {
          int x, y;

          if (!meta_actor_is_untransformed (child, &x, &y))
            continue;

          cairo_region_translate (clip_region, - x, - y);

          meta_background_actor_set_visible_region (META_BACKGROUND_ACTOR (child), clip_region);

          cairo_region_translate (clip_region, x, y);
        }
    }
}

static void
meta_window_group_reset_culling (MetaWindowGroup *group)
{
  ClutterActor *actor = CLUTTER_ACTOR (group);
  ClutterActor *child;
  ClutterActorIter iter;

  /* Now that we are done painting, unset the visible regions (they will
   * mess up painting clones of our actors)
   */
  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      if (META_IS_WINDOW_ACTOR (child))
        {
          MetaWindowActor *window_actor = META_WINDOW_ACTOR (child);
          meta_window_actor_reset_visible_regions (window_actor);
        }
      else if (META_IS_BACKGROUND_ACTOR (child))
        {
          MetaBackgroundActor *background_actor = META_BACKGROUND_ACTOR (child);
          meta_background_actor_set_visible_region (background_actor, NULL);
        }
    }
}

static void
meta_window_group_paint (ClutterActor *actor)
{
  cairo_region_t *clip_region;
  cairo_region_t *unobscured_region;
  ClutterActorIter iter;
  ClutterActor *child;
  cairo_rectangle_int_t visible_rect, clip_rect;
  int paint_x_offset, paint_y_offset;
  int paint_x_origin, paint_y_origin;
  int actor_x_origin, actor_y_origin;
  int screen_width, screen_height;

  MetaWindowGroup *window_group = META_WINDOW_GROUP (actor);
  MetaCompositor *compositor = window_group->screen->display->compositor;
  ClutterActor *stage = CLUTTER_STAGE (compositor->stage);
  meta_screen_get_size (window_group->screen, &screen_width, &screen_height);

  /* Start off by treating all windows as completely unobscured, so damage anywhere
   * in a window queues redraws, but confine it more below. */
  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      if (META_IS_WINDOW_ACTOR (child))
        {
          MetaWindowActor *window_actor = META_WINDOW_ACTOR (child);
          meta_window_actor_set_unobscured_region (window_actor, NULL);
        }
    }

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
  if (!meta_actor_painting_untransformed (cogl_get_draw_framebuffer (),
                                          screen_width,
                                          screen_height,
                                          &paint_x_origin,
                                          &paint_y_origin) ||
      !meta_actor_is_untransformed (actor, &actor_x_origin, &actor_y_origin))
    {
      CLUTTER_ACTOR_CLASS (meta_window_group_parent_class)->paint (actor);
      return;
    }

  paint_x_offset = paint_x_origin - actor_x_origin;
  paint_y_offset = paint_y_origin - actor_y_origin;

  visible_rect.x = visible_rect.y = 0;
  visible_rect.width = clutter_actor_get_width (stage);
  visible_rect.height = clutter_actor_get_height (stage);

  unobscured_region = cairo_region_create_rectangle (&visible_rect);

  /* Get the clipped redraw bounds from Clutter so that we can avoid
   * painting shadows on windows that don't need to be painted in this
   * frame. In the case of a multihead setup with mismatched monitor
   * sizes, we could intersect this with an accurate union of the
   * monitors to avoid painting shadows that are visible only in the
   * holes. */
  clutter_stage_get_redraw_clip_bounds (stage, &clip_rect);

  clip_region = cairo_region_create_rectangle (&clip_rect);

  cairo_region_translate (clip_region, -paint_x_offset, -paint_y_offset);

  gboolean has_unredirected_window = compositor->unredirected_window != NULL;
  if (has_unredirected_window)
    {
      cairo_rectangle_int_t unredirected_rect;
      MetaWindow *window = compositor->unredirected_window->priv->window;

      unredirected_rect.x = window->outer_rect.x;
      unredirected_rect.y = window->outer_rect.y;
      unredirected_rect.width = window->outer_rect.width;
      unredirected_rect.height = window->outer_rect.height;
      cairo_region_subtract_rectangle (unobscured_region, &unredirected_rect);
      cairo_region_subtract_rectangle (clip_region, &unredirected_rect);
    }

  meta_window_group_cull_out (window_group,
                              CLUTTER_ACTOR (compositor->unredirected_window),
                              has_unredirected_window,
                              unobscured_region,
                              clip_region);

  cairo_region_destroy (unobscured_region);
  cairo_region_destroy (clip_region);

  CLUTTER_ACTOR_CLASS (meta_window_group_parent_class)->paint (actor);

  meta_window_group_reset_culling (window_group);
}

static gboolean
meta_window_group_get_paint_volume (ClutterActor       *actor,
                                    ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
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
  ClutterActor *actor = CLUTTER_ACTOR (window_group);

  clutter_actor_set_flags (actor, CLUTTER_ACTOR_NO_LAYOUT);
}

LOCAL_SYMBOL ClutterActor *
meta_window_group_new (MetaScreen *screen)
{
  MetaWindowGroup *window_group;

  window_group = g_object_new (META_TYPE_WINDOW_GROUP, NULL);

  window_group->screen = screen;

  return CLUTTER_ACTOR (window_group);
}
