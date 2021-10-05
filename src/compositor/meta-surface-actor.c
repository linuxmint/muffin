/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-surface-actor
 * @title: MetaSurfaceActor
 * @short_description: An actor representing a surface in the scene graph
 *
 * MetaSurfaceActor is an abstract class which represents a surface in the
 * Clutter scene graph. A subclass can implement the specifics of a surface
 * depending on the way it is handled by a display protocol.
 *
 * An important feature of #MetaSurfaceActor is that it allows you to set an
 * "input region": all events that occur in the surface, but outside of the
 * input region are to be explicitly ignored. By default, this region is to
 * %NULL, which means events on the whole surface is allowed.
 */

#include "config.h"

#include "compositor/meta-surface-actor.h"

#include "clutter/clutter.h"
#include "compositor/clutter-utils.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/region-utils.h"
#include "meta/meta-shaped-texture.h"

typedef struct _MetaSurfaceActorPrivate
{
  MetaShapedTexture *texture;

  cairo_region_t *input_region;

  /* MetaCullable regions, see that documentation for more details */
  cairo_region_t *unobscured_region;

  /* Freeze/thaw accounting */
  cairo_region_t *pending_damage;
  guint frozen : 1;
} MetaSurfaceActorPrivate;

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaSurfaceActor, meta_surface_actor, CLUTTER_TYPE_ACTOR,
                                  G_ADD_PRIVATE (MetaSurfaceActor)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

enum
{
  REPAINT_SCHEDULED,
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

typedef enum
{
  IN_STAGE_PERSPECTIVE,
  IN_ACTOR_PERSPECTIVE
} ScalePerspectiveType;

static cairo_region_t *
effective_unobscured_region (MetaSurfaceActor *surface_actor)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  ClutterActor *actor;

  /* Fail if we have any mapped clones. */
  actor = CLUTTER_ACTOR (surface_actor);
  do
    {
      if (clutter_actor_has_mapped_clones (actor))
        return NULL;
      actor = clutter_actor_get_parent (actor);
    }
  while (actor != NULL);

  return priv->unobscured_region;
}

static cairo_region_t*
get_scaled_region (MetaSurfaceActor     *surface_actor,
                   cairo_region_t       *region,
                   ScalePerspectiveType  scale_perspective)
{
  MetaWindowActor *window_actor;
  cairo_region_t *scaled_region;
  int geometry_scale;
  float x, y;

  window_actor = meta_window_actor_from_actor (CLUTTER_ACTOR (surface_actor));
  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);

  clutter_actor_get_position (CLUTTER_ACTOR (surface_actor), &x, &y);
  cairo_region_translate (region, x, y);

  switch (scale_perspective)
    {
    case IN_STAGE_PERSPECTIVE:
      scaled_region = meta_region_scale_double (region,
                                                geometry_scale,
                                                META_ROUNDING_STRATEGY_GROW);
      break;
    case IN_ACTOR_PERSPECTIVE:
      scaled_region = meta_region_scale_double (region,
                                                1.0 / geometry_scale,
                                                META_ROUNDING_STRATEGY_GROW);
      break;
    }

  cairo_region_translate (region, -x, -y);
  cairo_region_translate (scaled_region, -x, -y);

  return scaled_region;
}

static void
set_unobscured_region (MetaSurfaceActor *surface_actor,
                       cairo_region_t   *unobscured_region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);

  g_clear_pointer (&priv->unobscured_region, cairo_region_destroy);
  if (unobscured_region)
    {
      if (cairo_region_is_empty (unobscured_region))
        {
          priv->unobscured_region = cairo_region_reference (unobscured_region);
        }
      else
        {
          cairo_rectangle_int_t bounds = { 0, };
          float width, height;

          clutter_content_get_preferred_size (CLUTTER_CONTENT (priv->texture),
                                              &width,
                                              &height);
          bounds = (cairo_rectangle_int_t) {
            .width = width,
            .height = height,
          };

          priv->unobscured_region = get_scaled_region (surface_actor,
                                                       unobscured_region,
                                                       IN_ACTOR_PERSPECTIVE);

          cairo_region_intersect_rectangle (priv->unobscured_region, &bounds);
        }
    }
}

static void
set_clip_region (MetaSurfaceActor *surface_actor,
                 cairo_region_t   *clip_region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  MetaShapedTexture *stex = priv->texture;

  if (clip_region && !cairo_region_is_empty (clip_region))
    {
      cairo_region_t *region;

      region = get_scaled_region (surface_actor,
                                  clip_region,
                                  IN_ACTOR_PERSPECTIVE);
      meta_shaped_texture_set_clip_region (stex, region);

      cairo_region_destroy (region);
    }
  else
    {
      meta_shaped_texture_set_clip_region (stex, clip_region);
    }
}

static void
meta_surface_actor_pick (ClutterActor       *actor,
                         ClutterPickContext *pick_context)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (actor);
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);
  ClutterActorIter iter;
  ClutterActor *child;

  if (!clutter_actor_should_pick_paint (actor))
    return;

  /* If there is no region then use the regular pick */
  if (priv->input_region == NULL)
    {
      ClutterActorClass *actor_class =
        CLUTTER_ACTOR_CLASS (meta_surface_actor_parent_class);

      actor_class->pick (actor, pick_context);
    }
  else
    {
      int n_rects;
      int i;

      n_rects = cairo_region_num_rectangles (priv->input_region);

      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          ClutterActorBox box;

          cairo_region_get_rectangle (priv->input_region, i, &rect);

          box.x1 = rect.x;
          box.y1 = rect.y;
          box.x2 = rect.x + rect.width;
          box.y2 = rect.y + rect.height;
          clutter_actor_pick_box (actor, pick_context, &box);
        }
    }

  clutter_actor_iter_init (&iter, actor);

  while (clutter_actor_iter_next (&iter, &child))
    clutter_actor_pick (child, pick_context);
}

static gboolean
meta_surface_actor_get_paint_volume (ClutterActor       *actor,
                                     ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}

static void
meta_surface_actor_dispose (GObject *object)
{
  MetaSurfaceActor *self = META_SURFACE_ACTOR (object);
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  g_clear_pointer (&priv->input_region, cairo_region_destroy);
  g_clear_object (&priv->texture);

  set_unobscured_region (self, NULL);

  G_OBJECT_CLASS (meta_surface_actor_parent_class)->dispose (object);
}

static void
meta_surface_actor_class_init (MetaSurfaceActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  object_class->dispose = meta_surface_actor_dispose;
  actor_class->pick = meta_surface_actor_pick;
  actor_class->get_paint_volume = meta_surface_actor_get_paint_volume;

  signals[REPAINT_SCHEDULED] = g_signal_new ("repaint-scheduled",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST,
                                             0,
                                             NULL, NULL, NULL,
                                             G_TYPE_NONE, 0);

  signals[SIZE_CHANGED] = g_signal_new ("size-changed",
                                        G_TYPE_FROM_CLASS (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
}

gboolean
meta_surface_actor_is_opaque (MetaSurfaceActor *self)
{
  return META_SURFACE_ACTOR_GET_CLASS (self)->is_opaque (self);
}

static void
meta_surface_actor_cull_out (MetaCullable   *cullable,
                             cairo_region_t *unobscured_region,
                             cairo_region_t *clip_region)
{
  MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (cullable);
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (surface_actor);
  uint8_t opacity = clutter_actor_get_opacity (CLUTTER_ACTOR (cullable));

  set_unobscured_region (surface_actor, unobscured_region);
  set_clip_region (surface_actor, clip_region);

  if (opacity == 0xff)
    {
      cairo_region_t *opaque_region;
      cairo_region_t *scaled_opaque_region;

      opaque_region = meta_shaped_texture_get_opaque_region (priv->texture);

      if (!opaque_region)
        return;

      scaled_opaque_region = get_scaled_region (surface_actor,
                                                opaque_region,
                                                IN_STAGE_PERSPECTIVE);

      if (unobscured_region)
        cairo_region_subtract (unobscured_region, scaled_opaque_region);
      if (clip_region)
        cairo_region_subtract (clip_region, scaled_opaque_region);

      cairo_region_destroy (scaled_opaque_region);
    }
}

static gboolean
meta_surface_actor_is_untransformed (MetaCullable *cullable)
{
  ClutterActor *actor = CLUTTER_ACTOR (cullable);
  MetaWindowActor *window_actor;
  float width, height;
  graphene_point3d_t verts[4];
  int geometry_scale;

  clutter_actor_get_size (actor, &width, &height);
  clutter_actor_get_abs_allocation_vertices (actor, verts);

  window_actor = meta_window_actor_from_actor (actor);
  geometry_scale = meta_window_actor_get_geometry_scale (window_actor);

  return meta_actor_vertices_are_untransformed (verts,
                                                width * geometry_scale,
                                                height * geometry_scale,
                                                NULL, NULL);
}

static void
meta_surface_actor_reset_culling (MetaCullable *cullable)
{
  MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (cullable);

  set_clip_region (surface_actor, NULL);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_surface_actor_cull_out;
  iface->is_untransformed = meta_surface_actor_is_untransformed;
  iface->reset_culling = meta_surface_actor_reset_culling;
}

static void
texture_size_changed (MetaShapedTexture *texture,
                      gpointer           user_data)
{
  MetaSurfaceActor *actor = META_SURFACE_ACTOR (user_data);
  g_signal_emit (actor, signals[SIZE_CHANGED], 0);
}

static void
meta_surface_actor_init (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  priv->texture = meta_shaped_texture_new ();
  g_signal_connect_object (priv->texture, "size-changed",
                           G_CALLBACK (texture_size_changed), self, 0);
  clutter_actor_set_content (CLUTTER_ACTOR (self),
                             CLUTTER_CONTENT (priv->texture));
  clutter_actor_set_request_mode (CLUTTER_ACTOR (self),
                                  CLUTTER_REQUEST_CONTENT_SIZE);
}

/**
 * meta_surface_actor_get_image:
 * @self: A #MetaSurfaceActor
 * @clip: (nullable): A clipping rectangle. The clip region is in
 * the same coordinate space as the contents preferred size.
 * For a shaped texture of a wl_surface, this means surface
 * coordinate space. If NULL, the whole content will be used.
 *
 * Get the image from the texture content. The resulting size of
 * the returned image may be different from the preferred size of
 * the shaped texture content.
 *
 * Returns: (nullable) (transfer full): a new cairo surface to be freed
 * with cairo_surface_destroy().
 */
cairo_surface_t *
meta_surface_actor_get_image (MetaSurfaceActor      *self,
                              cairo_rectangle_int_t *clip)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return meta_shaped_texture_get_image (priv->texture, clip);
}

MetaShapedTexture *
meta_surface_actor_get_texture (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return priv->texture;
}

void
meta_surface_actor_update_area (MetaSurfaceActor *self,
                                int               x,
                                int               y,
                                int               width,
                                int               height)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);
  gboolean repaint_scheduled = FALSE;
  cairo_rectangle_int_t clip;

  if (meta_shaped_texture_update_area (priv->texture, x, y, width, height, &clip))
    {
      cairo_region_t *unobscured_region;

      unobscured_region = effective_unobscured_region (self);

      if (unobscured_region)
        {
          cairo_region_t *intersection;

          if (cairo_region_is_empty (unobscured_region))
            return;

          intersection = cairo_region_copy (unobscured_region);
          cairo_region_intersect_rectangle (intersection, &clip);

          if (!cairo_region_is_empty (intersection))
            {
              cairo_rectangle_int_t damage_rect;

              cairo_region_get_extents (intersection, &damage_rect);
              clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (self), &damage_rect);
              repaint_scheduled = TRUE;
            }

          cairo_region_destroy (intersection);
        }
      else
        {
          clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (self), &clip);
          repaint_scheduled = TRUE;
        }
    }

  if (repaint_scheduled)
    g_signal_emit (self, signals[REPAINT_SCHEDULED], 0);
}

gboolean
meta_surface_actor_is_obscured (MetaSurfaceActor *self)
{
  cairo_region_t *unobscured_region;

  unobscured_region = effective_unobscured_region (self);

  if (unobscured_region)
    return cairo_region_is_empty (unobscured_region);
  else
    return FALSE;
}

void
meta_surface_actor_set_input_region (MetaSurfaceActor *self,
                                     cairo_region_t   *region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  if (priv->input_region)
    cairo_region_destroy (priv->input_region);

  if (region)
    priv->input_region = cairo_region_reference (region);
  else
    priv->input_region = NULL;
}

void
meta_surface_actor_set_opaque_region (MetaSurfaceActor *self,
                                      cairo_region_t   *region)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_set_opaque_region (priv->texture, region);
}

cairo_region_t *
meta_surface_actor_get_opaque_region (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return meta_shaped_texture_get_opaque_region (priv->texture);
}

void
meta_surface_actor_process_damage (MetaSurfaceActor *self,
                                   int x, int y, int width, int height)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  if (meta_surface_actor_is_frozen (self))
    {
      /* The window is frozen due to an effect in progress: we ignore damage
       * here on the off chance that this will stop the corresponding
       * texture_from_pixmap from being update.
       *
       * pending_damage tracks any damage that happened while the window was
       * frozen so that when can apply it when the window becomes unfrozen.
       *
       * It should be noted that this is an unreliable mechanism since it's
       * quite likely that drivers will aim to provide a zero-copy
       * implementation of the texture_from_pixmap extension and in those cases
       * any drawing done to the window is always immediately reflected in the
       * texture regardless of damage event handling.
       */
      cairo_rectangle_int_t rect = { .x = x, .y = y, .width = width, .height = height };

      if (!priv->pending_damage)
        priv->pending_damage = cairo_region_create_rectangle (&rect);
      else
        cairo_region_union_rectangle (priv->pending_damage, &rect);
      return;
    }

  META_SURFACE_ACTOR_GET_CLASS (self)->process_damage (self, x, y, width, height);
}

void
meta_surface_actor_pre_paint (MetaSurfaceActor *self)
{
  META_SURFACE_ACTOR_GET_CLASS (self)->pre_paint (self);
}

void
meta_surface_actor_set_frozen (MetaSurfaceActor *self,
                               gboolean          frozen)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  priv->frozen = frozen;

  if (!frozen && priv->pending_damage)
    {
      int i, n_rects = cairo_region_num_rectangles (priv->pending_damage);
      cairo_rectangle_int_t rect;

      /* Since we ignore damage events while a window is frozen for certain effects
       * we need to apply the tracked damage now. */

      for (i = 0; i < n_rects; i++)
        {
          cairo_region_get_rectangle (priv->pending_damage, i, &rect);
          meta_surface_actor_process_damage (self, rect.x, rect.y,
                                             rect.width, rect.height);
        }
      g_clear_pointer (&priv->pending_damage, cairo_region_destroy);
    }
}

gboolean
meta_surface_actor_is_frozen (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  return priv->frozen;
}

void
meta_surface_actor_set_transform (MetaSurfaceActor     *self,
                                  MetaMonitorTransform  transform)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_set_transform (priv->texture, transform);
}

void
meta_surface_actor_set_viewport_src_rect (MetaSurfaceActor *self,
                                          graphene_rect_t  *src_rect)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_set_viewport_src_rect (priv->texture, src_rect);
}

void
meta_surface_actor_reset_viewport_src_rect (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_reset_viewport_src_rect (priv->texture);
}

void
meta_surface_actor_set_viewport_dst_size (MetaSurfaceActor *self,
                                          int               dst_width,
                                          int               dst_height)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_set_viewport_dst_size (priv->texture,
                                             dst_width,
                                             dst_height);
}

void
meta_surface_actor_reset_viewport_dst_size (MetaSurfaceActor *self)
{
  MetaSurfaceActorPrivate *priv =
    meta_surface_actor_get_instance_private (self);

  meta_shaped_texture_reset_viewport_dst_size (priv->texture);
}
