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

/* Help macros to scale from OpenGL <-1,1> coordinates system to
 * window coordinates ranging [0,window-size]. Borrowed from clutter-utils.c
 */
#define MTX_GL_SCALE_X(x,w,v1,v2) ((((((x) / (w)) + 1.0f) / 2.0f) * (v1)) + (v2))
#define MTX_GL_SCALE_Y(y,w,v1,v2) ((v1) - (((((y) / (w)) + 1.0f) / 2.0f) * (v1)) + (v2))

/* Check if we're painting the MetaWindowGroup "untransformed". This can
 * differ from the result of actor_is_untransformed(window_group) if we're
 * inside a clone paint. The integer translation, if any, is returned.
 */
static gboolean
painting_untransformed (MetaWindowGroup *window_group,
                        int             *x_origin,
                        int             *y_origin)
{
  CoglMatrix modelview, projection, modelview_projection;
  ClutterVertex vertices[4];
  int width, height;
  float viewport[4];
  int i;

  cogl_get_modelview_matrix (&modelview);
  cogl_get_projection_matrix (&projection);

  cogl_matrix_multiply (&modelview_projection,
                        &projection,
                        &modelview);

  meta_screen_get_size (window_group->screen, &width, &height);

  vertices[0].x = 0;
  vertices[0].y = 0;
  vertices[0].z = 0;
  vertices[1].x = width;
  vertices[1].y = 0;
  vertices[1].z = 0;
  vertices[2].x = 0;
  vertices[2].y = height;
  vertices[2].z = 0;
  vertices[3].x = width;
  vertices[3].y = height;
  vertices[3].z = 0;

  cogl_get_viewport (viewport);

  for (i = 0; i < 4; i++)
    {
      float w = 1;
      cogl_matrix_transform_point (&modelview_projection, &vertices[i].x, &vertices[i].y, &vertices[i].z, &w);
      vertices[i].x = MTX_GL_SCALE_X (vertices[i].x, w,
                                      viewport[2], viewport[0]);
      vertices[i].y = MTX_GL_SCALE_Y (vertices[i].y, w,
                                      viewport[3], viewport[1]);
    }

  return meta_actor_vertices_are_untransformed (vertices, width, height, x_origin, y_origin);
}

static void
meta_window_group_cull_out (MetaWindowGroup *group,
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
      MetaCompScreen *info = meta_screen_get_compositor_data (group->screen);

      if (!CLUTTER_ACTOR_IS_VISIBLE (child))
        continue;

      if (info->unredirected_window != NULL &&
          child == CLUTTER_ACTOR (info->unredirected_window))
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
          meta_window_actor_set_visible_region (window_actor, clip_region);

          if (clutter_actor_get_paint_opacity (CLUTTER_ACTOR (window_actor)) == 0xff)
            {
              cairo_region_t *obscured_region = meta_window_actor_get_obscured_region (window_actor);
              if (obscured_region)
                {
                  cairo_region_subtract (unobscured_region, obscured_region);
                  cairo_region_subtract (clip_region, obscured_region);
                }
            }

          meta_window_actor_set_visible_region_beneath (window_actor, clip_region);

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

  MetaWindowGroup *window_group = META_WINDOW_GROUP (actor);
  ClutterActor *stage = clutter_actor_get_stage (actor);
  MetaCompScreen *info = meta_screen_get_compositor_data (window_group->screen);

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
  if (!painting_untransformed (window_group, &paint_x_origin, &paint_y_origin) ||
      !meta_actor_is_untransformed (actor, &actor_x_origin, &actor_y_origin))
    {
      CLUTTER_ACTOR_CLASS (meta_window_group_parent_class)->paint (actor);
      return;
    }

  paint_x_offset = paint_x_origin - actor_x_origin;
  paint_y_offset = paint_y_origin - actor_y_origin;

  visible_rect.x = visible_rect.y = 0;
  visible_rect.width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
  visible_rect.height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

  unobscured_region = cairo_region_create_rectangle (&visible_rect);

  /* Get the clipped redraw bounds from Clutter so that we can avoid
   * painting shadows on windows that don't need to be painted in this
   * frame. In the case of a multihead setup with mismatched monitor
   * sizes, we could intersect this with an accurate union of the
   * monitors to avoid painting shadows that are visible only in the
   * holes. */
  clutter_stage_get_redraw_clip_bounds (CLUTTER_STAGE (stage),
                                        &clip_rect);

  clip_region = cairo_region_create_rectangle (&clip_rect);

  cairo_region_translate (clip_region, -paint_x_offset, -paint_y_offset);

  if (info->unredirected_window != NULL)
    {
      cairo_rectangle_int_t unredirected_rect;
      MetaWindow *window = meta_window_actor_get_meta_window (info->unredirected_window);

      meta_window_get_outer_rect (window, (MetaRectangle *)&unredirected_rect);
      cairo_region_subtract_rectangle (unobscured_region, &unredirected_rect);
      cairo_region_subtract_rectangle (clip_region, &unredirected_rect);
    }

  meta_window_group_cull_out (window_group, unobscured_region, clip_region);

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
