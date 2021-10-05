/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

/**
 * SECTION:clutter-offscreen-effect
 * @short_description: Base class for effects using offscreen buffers
 * @see_also: #ClutterBlurEffect, #ClutterEffect
 *
 * #ClutterOffscreenEffect is an abstract class that can be used by
 * #ClutterEffect sub-classes requiring access to an offscreen buffer.
 *
 * Some effects, like the fragment shader based effects, can only use GL
 * textures, and in order to apply those effects to any kind of actor they
 * require that all drawing operations are applied to an offscreen framebuffer
 * that gets redirected to a texture.
 *
 * #ClutterOffscreenEffect provides all the heavy-lifting for creating the
 * offscreen framebuffer, the redirection and the final paint of the texture on
 * the desired stage.
 *
 * #ClutterOffscreenEffect is available since Clutter 1.4
 *
 * ## Implementing a ClutterOffscreenEffect
 *
 * Creating a sub-class of #ClutterOffscreenEffect requires, in case
 * of overriding the #ClutterEffect virtual functions, to chain up to the
 * #ClutterOffscreenEffect's implementation.
 *
 * On top of the #ClutterEffect's virtual functions,
 * #ClutterOffscreenEffect also provides a #ClutterOffscreenEffectClass.paint_target()
 * function, which encapsulates the effective painting of the texture that
 * contains the result of the offscreen redirection.
 *
 * The size of the target material is defined to be as big as the
 * transformed size of the #ClutterActor using the offscreen effect.
 * Sub-classes of #ClutterOffscreenEffect can change the texture creation
 * code to provide bigger textures by overriding the
 * #ClutterOffscreenEffectClass.create_texture() virtual function; no chain up
 * to the #ClutterOffscreenEffect implementation is required in this
 * case.
 */

#include "clutter-build-config.h"

#include "clutter-offscreen-effect.h"

#include <math.h>

#include "cogl/cogl.h"

#include "clutter-actor-private.h"
#include "clutter-debug.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-paint-context-private.h"
#include "clutter-paint-volume-private.h"
#include "clutter-actor-box-private.h"

struct _ClutterOffscreenEffectPrivate
{
  CoglHandle offscreen;
  CoglPipeline *target;
  CoglHandle texture;

  ClutterActor *actor;
  ClutterActor *stage;

  graphene_point3d_t position;

  int fbo_offset_x;
  int fbo_offset_y;

  /* This is the calculated size of the fbo before being passed
     through create_texture(). This needs to be tracked separately so
     that we can detect when a different size is calculated and
     regenerate the fbo */
  int target_width;
  int target_height;

  gint old_opacity_override;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterOffscreenEffect,
                                     clutter_offscreen_effect,
                                     CLUTTER_TYPE_EFFECT)

static void
clutter_offscreen_effect_set_actor (ClutterActorMeta *meta,
                                    ClutterActor     *actor)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (meta);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  ClutterActorMetaClass *meta_class;

  meta_class = CLUTTER_ACTOR_META_CLASS (clutter_offscreen_effect_parent_class);
  meta_class->set_actor (meta, actor);

  /* clear out the previous state */
  g_clear_pointer (&priv->offscreen, cogl_object_unref);

  /* we keep a back pointer here, to avoid going through the ActorMeta */
  priv->actor = clutter_actor_meta_get_actor (meta);
}

static CoglHandle
clutter_offscreen_effect_real_create_texture (ClutterOffscreenEffect *effect,
                                              gfloat                  width,
                                              gfloat                  height)
{
  return cogl_texture_new_with_size (MAX (width, 1), MAX (height, 1),
                                     COGL_TEXTURE_NO_SLICING,
                                     COGL_PIXEL_FORMAT_RGBA_8888_PRE);
}

static void
ensure_pipeline_filter_for_scale (ClutterOffscreenEffect *self,
                                  float                   resource_scale)
{
  CoglPipelineFilter filter;

  if (!self->priv->target)
    return;

  /* If no fractional scaling is set, we're always going to render the texture
     at a 1:1 texel:pixel ratio so, in such case we can use 'nearest' filtering
     to decrease the effects of rounding errors in the geometry calculation;
     if instead we we're using a global fractional scaling we need to make sure
     that we're using the default linear effect, not to create artifacts when
     scaling down the texture */
  if (fmodf (resource_scale, 1.0f) == 0)
    filter = COGL_PIPELINE_FILTER_NEAREST;
  else
    filter = COGL_PIPELINE_FILTER_LINEAR;

  cogl_pipeline_set_layer_filters (self->priv->target, 0 /* layer_index */,
                                   filter, filter);
}

static gboolean
update_fbo (ClutterEffect *effect,
            int            target_width,
            int            target_height,
            float          resource_scale)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;

  priv->stage = clutter_actor_get_stage (priv->actor);
  if (priv->stage == NULL)
    {
      CLUTTER_NOTE (MISC, "The actor '%s' is not part of a stage",
                    clutter_actor_get_name (priv->actor) == NULL
                      ? G_OBJECT_TYPE_NAME (priv->actor)
                      : clutter_actor_get_name (priv->actor));
      return FALSE;
    }

  if (priv->target_width == target_width &&
      priv->target_height == target_height &&
      priv->offscreen != NULL)
  {
    ensure_pipeline_filter_for_scale (self, resource_scale);
    return TRUE;
  }

  if (priv->target == NULL)
    {
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      priv->target = cogl_pipeline_new (ctx);
      ensure_pipeline_filter_for_scale (self, resource_scale);
    }

  g_clear_pointer (&priv->texture, cogl_object_unref);
  g_clear_pointer (&priv->offscreen, cogl_object_unref);

  priv->texture =
    clutter_offscreen_effect_create_texture (self, target_width, target_height);
  if (priv->texture == NULL)
    return FALSE;

  cogl_pipeline_set_layer_texture (priv->target, 0, priv->texture);

  priv->target_width = target_width;
  priv->target_height = target_height;

  priv->offscreen = cogl_offscreen_new_to_texture (priv->texture);
  if (priv->offscreen == NULL)
    {
      g_warning ("%s: Unable to create an Offscreen buffer", G_STRLOC);

      cogl_object_unref (priv->target);
      priv->target = NULL;

      priv->target_width = 0;
      priv->target_height = 0;

      return FALSE;
    }

  return TRUE;
}

static gboolean
clutter_offscreen_effect_pre_paint (ClutterEffect       *effect,
                                    ClutterPaintContext *paint_context)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  ClutterActorBox raw_box, box;
  ClutterActor *stage;
  CoglMatrix projection, old_modelview, modelview;
  const ClutterPaintVolume *volume;
  CoglColor transparent;
  gfloat stage_width, stage_height;
  gfloat target_width = -1, target_height = -1;
  CoglFramebuffer *framebuffer;
  gfloat resource_scale;
  gfloat ceiled_resource_scale;
  graphene_point3d_t local_offset;
  gfloat old_viewport[4];

  local_offset = GRAPHENE_POINT3D_INIT (0.0f, 0.0f, 0.0f);

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (effect)))
    return FALSE;

  if (priv->actor == NULL)
    return FALSE;

  stage = _clutter_actor_get_stage_internal (priv->actor);
  clutter_actor_get_size (stage, &stage_width, &stage_height);

  if (_clutter_actor_get_real_resource_scale (priv->actor, &resource_scale))
    {
      ceiled_resource_scale = ceilf (resource_scale);
      stage_width *= ceiled_resource_scale;
      stage_height *= ceiled_resource_scale;
    }
  else
    {
      /* We are sure we have a resource scale set to a good value at paint */
      g_assert_not_reached ();
    }

  /* Get the minimal bounding box for what we want to paint, relative to the
   * parent of priv->actor. Note that we may actually be painting a clone of
   * priv->actor so we need to be careful to avoid querying the transformation
   * of priv->actor (like clutter_actor_get_paint_box would). Just stay in
   * local coordinates for now...
   */
  volume = clutter_actor_get_paint_volume (priv->actor);
  if (volume)
    {
      ClutterPaintVolume mutable_volume;

      _clutter_paint_volume_copy_static (volume, &mutable_volume);
      _clutter_paint_volume_get_bounding_box (&mutable_volume, &raw_box);
      clutter_paint_volume_free (&mutable_volume);
    }
  else
    {
      clutter_actor_get_allocation_box (priv->actor, &raw_box);
    }

  box = raw_box;
  _clutter_actor_box_enlarge_for_effects (&box);

  priv->fbo_offset_x = box.x1 - raw_box.x1;
  priv->fbo_offset_y = box.y1 - raw_box.y1;

  clutter_actor_box_scale (&box, ceiled_resource_scale);
  clutter_actor_box_get_size (&box, &target_width, &target_height);

  target_width = ceilf (target_width);
  target_height = ceilf (target_height);

  /* First assert that the framebuffer is the right size... */
  if (!update_fbo (effect, target_width, target_height, resource_scale))
    return FALSE;

  framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  cogl_framebuffer_get_modelview_matrix (framebuffer, &old_modelview);

  clutter_paint_context_push_framebuffer (paint_context, priv->offscreen);

  /* We don't want the FBO contents to be transformed. That could waste memory
   * (e.g. during zoom), or result in something that's not rectangular (clipped
   * incorrectly). So drop the modelview matrix of the current paint chain.
   * This is fine since paint_texture runs with the same modelview matrix,
   * so it will come out correctly whenever that is used to put the FBO
   * contents on screen...
   */
  clutter_actor_get_transform (priv->stage, &modelview);
  cogl_framebuffer_set_modelview_matrix (priv->offscreen, &modelview);

  /* Save the original viewport for calculating priv->position */
  _clutter_stage_get_viewport (CLUTTER_STAGE (priv->stage),
                               &old_viewport[0],
                               &old_viewport[1],
                               &old_viewport[2],
                               &old_viewport[3]);

  /* Set up the viewport so that it has the same size as the stage (avoid
   * distortion), but translated to account for the FBO offset...
   */
  cogl_framebuffer_set_viewport (priv->offscreen,
                                 -priv->fbo_offset_x,
                                 -priv->fbo_offset_y,
                                 stage_width,
                                 stage_height);

  /* Copy the stage's projection matrix across to the framebuffer */
  _clutter_stage_get_projection_matrix (CLUTTER_STAGE (priv->stage),
                                        &projection);

  /* Now save the global position of the effect (not just of the actor).
   * It doesn't appear anyone actually uses this yet, but get_target_rect is
   * documented as returning it. So we should...
   */
  _clutter_util_fully_transform_vertices (&old_modelview,
                                          &projection,
                                          old_viewport,
                                          &local_offset,
                                          &priv->position,
                                          1);

  cogl_framebuffer_set_projection_matrix (priv->offscreen, &projection);

  cogl_color_init_from_4ub (&transparent, 0, 0, 0, 0);
  cogl_framebuffer_clear (priv->offscreen,
                          COGL_BUFFER_BIT_COLOR |
                          COGL_BUFFER_BIT_DEPTH,
                          &transparent);

  cogl_framebuffer_push_matrix (priv->offscreen);

  /* Override the actor's opacity to fully opaque - we paint the offscreen
   * texture with the actor's paint opacity, so we need to do this to avoid
   * multiplying the opacity twice.
   */
  priv->old_opacity_override =
    clutter_actor_get_opacity_override (priv->actor);
  clutter_actor_set_opacity_override (priv->actor, 0xff);

  return TRUE;
}

static void
clutter_offscreen_effect_real_paint_target (ClutterOffscreenEffect *effect,
                                            ClutterPaintContext    *paint_context)
{
  ClutterOffscreenEffectPrivate *priv = effect->priv;
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  guint8 paint_opacity;

  paint_opacity = clutter_actor_get_paint_opacity (priv->actor);

  cogl_pipeline_set_color4ub (priv->target,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity);

  /* At this point we are in stage coordinates translated so if
   * we draw our texture using a textured quad the size of the paint
   * box then we will overlay where the actor would have drawn if it
   * hadn't been redirected offscreen.
   */
  cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                            priv->target,
                                            0, 0,
                                            cogl_texture_get_width (priv->texture),
                                            cogl_texture_get_height (priv->texture),
                                            0.0, 0.0,
                                            1.0, 1.0);
}

static void
clutter_offscreen_effect_paint_texture (ClutterOffscreenEffect *effect,
                                        ClutterPaintContext    *paint_context)
{
  ClutterOffscreenEffectPrivate *priv = effect->priv;
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglMatrix modelview;
  float resource_scale;

  cogl_framebuffer_push_matrix (framebuffer);

  /* The current modelview matrix is *almost* perfect already. It's only
   * missing a correction for the expanded FBO and offset rendering within...
   */
  cogl_framebuffer_get_modelview_matrix (framebuffer, &modelview);

  if (clutter_actor_get_resource_scale (priv->actor, &resource_scale) &&
      resource_scale != 1.0f)
    {
      float paint_scale = 1.0f / resource_scale;
      cogl_matrix_scale (&modelview, paint_scale, paint_scale, 1);
    }

  cogl_matrix_translate (&modelview,
                         priv->fbo_offset_x,
                         priv->fbo_offset_y,
                         0.0f);
  cogl_framebuffer_set_modelview_matrix (framebuffer, &modelview);

  /* paint the target material; this is virtualized for
   * sub-classes that require special hand-holding
   */
  clutter_offscreen_effect_paint_target (effect, paint_context);

  cogl_framebuffer_pop_matrix (framebuffer);
}

static void
clutter_offscreen_effect_post_paint (ClutterEffect       *effect,
                                     ClutterPaintContext *paint_context)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  CoglFramebuffer *framebuffer;

  if (priv->offscreen == NULL ||
      priv->target == NULL ||
      priv->actor == NULL)
    return;

  /* Restore the previous opacity override */
  clutter_actor_set_opacity_override (priv->actor, priv->old_opacity_override);

  framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  cogl_framebuffer_pop_matrix (framebuffer);
  clutter_paint_context_pop_framebuffer (paint_context);

  clutter_offscreen_effect_paint_texture (self, paint_context);
}

static void
clutter_offscreen_effect_paint (ClutterEffect           *effect,
                                ClutterPaintContext     *paint_context,
                                ClutterEffectPaintFlags  flags)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;

  if (flags & CLUTTER_EFFECT_PAINT_BYPASS_EFFECT)
    {
      clutter_actor_continue_paint (priv->actor, paint_context);
      cogl_clear_object (&priv->offscreen);
      return;
    }

  /* If we've already got a cached image and the actor hasn't been redrawn
   * then we can just use the cached image in the FBO.
   */
  if (priv->offscreen == NULL || (flags & CLUTTER_EFFECT_PAINT_ACTOR_DIRTY))
    {
      ClutterEffectClass *effect_class = CLUTTER_EFFECT_GET_CLASS (effect);
      gboolean pre_paint_succeeded;

      pre_paint_succeeded = effect_class->pre_paint (effect, paint_context);

      clutter_actor_continue_paint (priv->actor, paint_context);

      if (pre_paint_succeeded)
        effect_class->post_paint (effect, paint_context);
      else
        g_clear_pointer (&priv->offscreen, cogl_object_unref);
    }
  else
    clutter_offscreen_effect_paint_texture (self, paint_context);
}

static void
clutter_offscreen_effect_notify (GObject    *gobject,
                                 GParamSpec *pspec)
{
  ClutterOffscreenEffect *offscreen_effect = CLUTTER_OFFSCREEN_EFFECT (gobject);
  ClutterOffscreenEffectPrivate *priv = offscreen_effect->priv;

  if (strcmp (pspec->name, "enabled") == 0)
    g_clear_pointer (&priv->offscreen, cogl_object_unref);

  G_OBJECT_CLASS (clutter_offscreen_effect_parent_class)->notify (gobject, pspec);
}

static void
clutter_offscreen_effect_finalize (GObject *gobject)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (gobject);
  ClutterOffscreenEffectPrivate *priv = self->priv;

  g_clear_pointer (&priv->offscreen, cogl_object_unref);
  g_clear_pointer (&priv->texture, cogl_object_unref);
  g_clear_pointer (&priv->target, cogl_object_unref);

  G_OBJECT_CLASS (clutter_offscreen_effect_parent_class)->finalize (gobject);
}

static void
clutter_offscreen_effect_class_init (ClutterOffscreenEffectClass *klass)
{
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  klass->create_texture = clutter_offscreen_effect_real_create_texture;
  klass->paint_target = clutter_offscreen_effect_real_paint_target;

  meta_class->set_actor = clutter_offscreen_effect_set_actor;

  effect_class->pre_paint = clutter_offscreen_effect_pre_paint;
  effect_class->post_paint = clutter_offscreen_effect_post_paint;
  effect_class->paint = clutter_offscreen_effect_paint;

  gobject_class->finalize = clutter_offscreen_effect_finalize;
  gobject_class->notify = clutter_offscreen_effect_notify;
}

static void
clutter_offscreen_effect_init (ClutterOffscreenEffect *self)
{
  self->priv = clutter_offscreen_effect_get_instance_private (self);
}

/**
 * clutter_offscreen_effect_get_texture:
 * @effect: a #ClutterOffscreenEffect
 *
 * Retrieves the texture used as a render target for the offscreen
 * buffer created by @effect
 *
 * You should only use the returned texture when painting. The texture
 * may change after ClutterEffect::pre_paint is called so the effect
 * implementation should update any references to the texture after
 * chaining-up to the parent's pre_paint implementation. This can be
 * used instead of clutter_offscreen_effect_get_target() when the
 * effect subclass wants to paint using its own material.
 *
 * Return value: (transfer none): a #CoglHandle or %NULL. The
 *   returned texture is owned by Clutter and it should not be
 *   modified or freed
 *
 * Since: 1.10
 */
CoglHandle
clutter_offscreen_effect_get_texture (ClutterOffscreenEffect *effect)
{
  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect),
                        NULL);

  return effect->priv->texture;
}

/**
 * clutter_offscreen_effect_get_target: (skip)
 * @effect: a #ClutterOffscreenEffect
 *
 * Retrieves the material used as a render target for the offscreen
 * buffer created by @effect
 *
 * You should only use the returned #CoglMaterial when painting. The
 * returned material might change between different frames.
 *
 * Return value: (transfer none): a #CoglMaterial or %NULL. The
 *   returned material is owned by Clutter and it should not be
 *   modified or freed
 *
 * Since: 1.4
 */
CoglMaterial *
clutter_offscreen_effect_get_target (ClutterOffscreenEffect *effect)
{
  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect),
                        NULL);

  return (CoglMaterial *)effect->priv->target;
}

/**
 * clutter_offscreen_effect_paint_target:
 * @effect: a #ClutterOffscreenEffect
 * @paint_context: a #ClutterPaintContext
 *
 * Calls the paint_target() virtual function of the @effect
 *
 * Since: 1.4
 */
void
clutter_offscreen_effect_paint_target (ClutterOffscreenEffect *effect,
                                       ClutterPaintContext    *paint_context)
{
  g_return_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect));

  CLUTTER_OFFSCREEN_EFFECT_GET_CLASS (effect)->paint_target (effect,
                                                             paint_context);
}

/**
 * clutter_offscreen_effect_create_texture:
 * @effect: a #ClutterOffscreenEffect
 * @width: the minimum width of the target texture
 * @height: the minimum height of the target texture
 *
 * Calls the create_texture() virtual function of the @effect
 *
 * Return value: (transfer full): a handle to a Cogl texture, or
 *   %NULL. The returned handle has its reference
 *   count increased.
 *
 * Since: 1.4
 */
CoglHandle
clutter_offscreen_effect_create_texture (ClutterOffscreenEffect *effect,
                                         gfloat                  width,
                                         gfloat                  height)
{
  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect),
                        NULL);

  return CLUTTER_OFFSCREEN_EFFECT_GET_CLASS (effect)->create_texture (effect,
                                                                      width,
                                                                      height);
}

/**
 * clutter_offscreen_effect_get_target_size:
 * @effect: a #ClutterOffscreenEffect
 * @width: (out): return location for the target width, or %NULL
 * @height: (out): return location for the target height, or %NULL
 *
 * Retrieves the size of the offscreen buffer used by @effect to
 * paint the actor to which it has been applied.
 *
 * This function should only be called by #ClutterOffscreenEffect
 * implementations, from within the #ClutterOffscreenEffectClass.paint_target()
 * virtual function.
 *
 * Return value: %TRUE if the offscreen buffer has a valid size,
 *   and %FALSE otherwise
 *
 * Since: 1.8
 *
 * Deprecated: 1.14: Use clutter_offscreen_effect_get_target_rect() instead
 */
gboolean
clutter_offscreen_effect_get_target_size (ClutterOffscreenEffect *effect,
                                          gfloat                 *width,
                                          gfloat                 *height)
{
  ClutterOffscreenEffectPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect), FALSE);

  priv = effect->priv;

  if (priv->texture == NULL)
    return FALSE;

  if (width)
    *width = cogl_texture_get_width (priv->texture);

  if (height)
    *height = cogl_texture_get_height (priv->texture);

  return TRUE;
}

/**
 * clutter_offscreen_effect_get_target_rect:
 * @effect: a #ClutterOffscreenEffect
 * @rect: (out caller-allocates): return location for the target area
 *
 * Retrieves the origin and size of the offscreen buffer used by @effect to
 * paint the actor to which it has been applied.
 *
 * This function should only be called by #ClutterOffscreenEffect
 * implementations, from within the #ClutterOffscreenEffectClass.paint_target()
 * virtual function.
 *
 * Return value: %TRUE if the offscreen buffer has a valid rectangle,
 *   and %FALSE otherwise
 *
 * Since: 1.14
 */
gboolean
clutter_offscreen_effect_get_target_rect (ClutterOffscreenEffect *effect,
                                          graphene_rect_t        *rect)
{
  ClutterOffscreenEffectPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect), FALSE);
  g_return_val_if_fail (rect != NULL, FALSE);

  priv = effect->priv;

  if (priv->texture == NULL)
    return FALSE;

  graphene_rect_init (rect,
                      priv->position.x,
                      priv->position.y,
                      cogl_texture_get_width (priv->texture),
                      cogl_texture_get_height (priv->texture));

  return TRUE;
}
