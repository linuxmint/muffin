/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

/**
 * SECTION:meta-shaped-texture
 * @title: MetaShapedTexture
 * @short_description: An actor to draw a masked texture.
 */

#include <config.h>

#include <meta/meta-shaped-texture.h>
#include "clutter-utils.h"
#include "meta-texture-tower.h"
#include "meta-texture-rectangle.h"
#include "meta-shaped-texture-private.h"
#include "cogl-utils.h"

#include <clutter/clutter.h>
#include <cogl/cogl.h>
#include <cogl/winsys/cogl-texture-pixmap-x11.h>
#include <gdk/gdk.h> /* for gdk_rectangle_intersect() */
#include <string.h>

/* MAX_MIPMAPPING_FPS needs to be as small as possible for the best GPU
 * performance, but higher than the refresh rate of commonly slow updating
 * windows like top or a blinking cursor, so that such windows do get
 * mipmapped.
 */
#define MAX_MIPMAPPING_FPS 5
#define MIN_MIPMAP_AGE_USEC (G_USEC_PER_SEC / MAX_MIPMAPPING_FPS)

/* MIN_FAST_UPDATES_BEFORE_UNMIPMAP allows windows to update themselves
 * occasionally without causing mipmapping to be disabled, so long as such
 * an update takes fewer update_area calls than:
 */
#define MIN_FAST_UPDATES_BEFORE_UNMIPMAP 20

static void meta_shaped_texture_dispose  (GObject    *object);

static void meta_shaped_texture_paint (ClutterActor       *actor);
static void meta_shaped_texture_pick  (ClutterActor       *actor,
				       const ClutterColor *color);

static void meta_shaped_texture_get_preferred_width (ClutterActor *self,
                                                     gfloat        for_height,
                                                     gfloat       *min_width_p,
                                                     gfloat       *natural_width_p);

static void meta_shaped_texture_get_preferred_height (ClutterActor *self,
                                                      gfloat        for_width,
                                                      gfloat       *min_height_p,
                                                      gfloat       *natural_height_p);

static gboolean meta_shaped_texture_get_paint_volume (ClutterActor *self, ClutterPaintVolume *volume);

G_DEFINE_TYPE (MetaShapedTexture, meta_shaped_texture,
               CLUTTER_TYPE_ACTOR);

#define META_SHAPED_TEXTURE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), META_TYPE_SHAPED_TEXTURE, \
                                MetaShapedTexturePrivate))

enum {
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

struct _MetaShapedTexturePrivate
{
  MetaTextureTower *paint_tower;
  Pixmap pixmap;
  CoglTexture *texture;
  CoglTexture *mask_texture;

  cairo_region_t *clip_region;
  cairo_region_t *unobscured_region;
  cairo_region_t *shape_region;
  cairo_region_t *opaque_region;

  cairo_region_t *overlay_region;
  cairo_path_t *overlay_path;

  guint tex_width, tex_height;

  gint64 prev_invalidation, last_invalidation;
  guint fast_updates;
  guint remipmap_timeout_id;
  gint64 earliest_remipmap;

  guint create_mipmaps : 1;
  guint mask_needs_update : 1;
};

static void
meta_shaped_texture_class_init (MetaShapedTextureClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  ClutterActorClass *actor_class = (ClutterActorClass *) klass;

  gobject_class->dispose = meta_shaped_texture_dispose;

  actor_class->get_preferred_width = meta_shaped_texture_get_preferred_width;
  actor_class->get_preferred_height = meta_shaped_texture_get_preferred_height;
  actor_class->paint = meta_shaped_texture_paint;
  actor_class->pick = meta_shaped_texture_pick;
  actor_class->get_paint_volume = meta_shaped_texture_get_paint_volume;

  signals[SIZE_CHANGED] = g_signal_new ("size-changed",
                                        G_TYPE_FROM_CLASS (gobject_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);

  g_type_class_add_private (klass, sizeof (MetaShapedTexturePrivate));
}

static void
meta_shaped_texture_init (MetaShapedTexture *self)
{
  MetaShapedTexturePrivate *priv;

  priv = self->priv = META_SHAPED_TEXTURE_GET_PRIVATE (self);

  priv->shape_region = NULL;
  priv->overlay_path = NULL;
  priv->overlay_region = NULL;
  priv->paint_tower = meta_texture_tower_new ();
  priv->texture = NULL;
  priv->mask_texture = NULL;
  priv->create_mipmaps = TRUE;
  priv->mask_needs_update = TRUE;
}

static void
meta_shaped_texture_dispose (GObject *object)
{
  MetaShapedTexture *self = (MetaShapedTexture *) object;
  MetaShapedTexturePrivate *priv = self->priv;

  if (priv->remipmap_timeout_id)
    {
      g_source_remove (priv->remipmap_timeout_id);
      priv->remipmap_timeout_id = 0;
    }

  if (priv->paint_tower)
    meta_texture_tower_free (priv->paint_tower);
  priv->paint_tower = NULL;

  meta_shaped_texture_dirty_mask (self);
  g_clear_pointer (&priv->texture, cogl_object_unref);
  g_clear_pointer (&priv->opaque_region, cairo_region_destroy);

  meta_shaped_texture_set_shape_region (self, NULL);
  meta_shaped_texture_set_clip_region (self, NULL);
  meta_shaped_texture_set_overlay_path (self, NULL, NULL);

  G_OBJECT_CLASS (meta_shaped_texture_parent_class)->dispose (object);
}

static CoglPipeline *
get_base_pipeline (CoglContext *ctx)
{
  static CoglPipeline *template = NULL;
  if (G_UNLIKELY (template == NULL))
    {
      template = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_wrap_mode_s (template, 0, COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
      cogl_pipeline_set_layer_wrap_mode_t (template, 0, COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
      cogl_pipeline_set_layer_wrap_mode_s (template, 1, COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
      cogl_pipeline_set_layer_wrap_mode_t (template, 1, COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
    }
  return template;
}

static CoglPipeline *
get_unmasked_pipeline (CoglContext *ctx)
{
  return get_base_pipeline (ctx);
}

static CoglPipeline *
get_masked_pipeline (CoglContext *ctx)
{
  static CoglPipeline *template = NULL;
  if (G_UNLIKELY (template == NULL))
    {
      template = cogl_pipeline_copy (get_base_pipeline (ctx));
      cogl_pipeline_set_layer_combine (template, 1,
                                       "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                       NULL);
    }

  return template;
}

static CoglPipeline *
get_unblended_pipeline (CoglContext *ctx)
{
  static CoglPipeline *template = NULL;
  if (G_UNLIKELY (template == NULL))
    {
      CoglColor color;
      template = cogl_pipeline_copy (get_base_pipeline (ctx));
      cogl_color_init_from_4ub (&color, 255, 255, 255, 255);
      cogl_pipeline_set_blend (template,
                               "RGBA = ADD (SRC_COLOR, 0)",
                               NULL);
      cogl_pipeline_set_color (template, &color);
    }

  return template;
}

static void
paint_clipped_rectangle (CoglFramebuffer       *fb,
                         CoglPipeline          *pipeline,
                         cairo_rectangle_int_t *rect,
                         ClutterActorBox       *alloc)
{
  float coords[8];
  float x1, y1, x2, y2;

  x1 = rect->x;
  y1 = rect->y;
  x2 = rect->x + rect->width;
  y2 = rect->y + rect->height;

  coords[0] = rect->x / (alloc->x2 - alloc->x1);
  coords[1] = rect->y / (alloc->y2 - alloc->y1);
  coords[2] = (rect->x + rect->width) / (alloc->x2 - alloc->x1);
  coords[3] = (rect->y + rect->height) / (alloc->y2 - alloc->y1);

  coords[4] = coords[0];
  coords[5] = coords[1];
  coords[6] = coords[2];
  coords[7] = coords[3];

  cogl_framebuffer_draw_multitextured_rectangle (fb, pipeline,
                                                 x1, y1, x2, y2,
                                                 &coords[0], 8);

}

LOCAL_SYMBOL void
meta_shaped_texture_dirty_mask (MetaShapedTexture *stex)
{
  MetaShapedTexturePrivate *priv = stex->priv;

  g_clear_pointer (&priv->mask_texture, cogl_object_unref);
}

static void
install_overlay_path (MetaShapedTexture *stex,
                      guchar            *mask_data,
                      int                tex_width,
                      int                tex_height,
                      int                stride)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  int i, n_rects;
  cairo_t *cr;
  cairo_rectangle_int_t rect;
  cairo_surface_t *surface;

  if (priv->overlay_region == NULL)
    return;

  surface = cairo_image_surface_create_for_data (mask_data,
                                                 CAIRO_FORMAT_A8,
                                                 tex_width,
                                                 tex_height,
                                                 stride);

  cr = cairo_create (surface);
  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);

  n_rects = cairo_region_num_rectangles (priv->overlay_region);
  for (i = 0; i < n_rects; i++)
    {
      cairo_region_get_rectangle (priv->overlay_region, i, &rect);
      cairo_rectangle (cr, rect.x, rect.y, rect.width, rect.height);
    }

  cairo_fill_preserve (cr);
  if (priv->overlay_path == NULL)
    {
      /* If we have an overlay region but not an overlay path, then we
       * just need to clear the rectangles in the overlay region. */
      goto out;
    }

  cairo_clip (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba (cr, 1, 1, 1, 1);

  cairo_append_path (cr, priv->overlay_path);
  cairo_fill (cr);

 out:
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}

LOCAL_SYMBOL void
meta_shaped_texture_ensure_mask (MetaShapedTexture *stex,
                                 gboolean           has_frame)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  CoglTexture *paint_tex;
  guint tex_width, tex_height;

  paint_tex = priv->texture;

  if (paint_tex == NULL)
    return;

  tex_width = cogl_texture_get_width (paint_tex);
  tex_height = cogl_texture_get_height (paint_tex);

  /* If the mask texture we have was created for a different size then
     recreate it */
  if (priv->mask_texture != NULL && priv->mask_needs_update)
    {
      priv->mask_needs_update = FALSE;
      meta_shaped_texture_dirty_mask (stex);
    }

  /* If we don't have a mask texture yet then create one */
  if (priv->mask_texture == NULL)
    {
      guchar *mask_data;
      int i;
      int n_rects;
      int stride;

      /* If we have no shape region and no (or an empty) overlay region, we
       * don't need to create a full mask texture, so quit early. */
      if (priv->shape_region == NULL &&
          (priv->overlay_region == NULL ||
           cairo_region_num_rectangles (priv->overlay_region) == 0))
        {
          return;
        }

      if (priv->shape_region == NULL)
        return;

      n_rects = cairo_region_num_rectangles (priv->shape_region);

      if (n_rects == 0)
        return;

      stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, tex_width);

      /* Create data for an empty image */
      mask_data = g_malloc0 (stride * tex_height);

      /* Fill in each rectangle. */
      for (i = 0; i < n_rects; i ++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (priv->shape_region, i, &rect);

          gint x1 = rect.x, x2 = x1 + rect.width;
          gint y1 = rect.y, y2 = y1 + rect.height;
          guchar *p;

          /* Clip the rectangle to the size of the texture */
          x1 = CLAMP (x1, 0, (gint) tex_width - 1);
          x2 = CLAMP (x2, x1, (gint) tex_width);
          y1 = CLAMP (y1, 0, (gint) tex_height - 1);
          y2 = CLAMP (y2, y1, (gint) tex_height);

          /* Fill the rectangle */
          for (p = mask_data + y1 * stride + x1;
               y1 < y2;
               y1++, p += stride)
            memset (p, 255, x2 - x1);
        }

      if (has_frame)
        install_overlay_path (stex, mask_data, tex_width, tex_height, stride);

      if (meta_texture_rectangle_check (paint_tex))
        priv->mask_texture = meta_cogl_rectangle_new (tex_width, tex_height,
                                                        COGL_PIXEL_FORMAT_A_8,
                                                        stride, mask_data);
      else
        priv->mask_texture = meta_cogl_texture_new_from_data_wrapper (tex_width, tex_height,
                                                                      COGL_TEXTURE_NONE,
                                                                      COGL_PIXEL_FORMAT_A_8,
                                                                      COGL_PIXEL_FORMAT_ANY,
                                                                      stride,
                                                                      mask_data);

      g_free (mask_data);
    }
}

static gboolean
texture_is_idle_and_not_mipmapped (gpointer user_data)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (user_data);
  MetaShapedTexturePrivate *priv = stex->priv;

  if ((g_get_monotonic_time () - priv->earliest_remipmap) < 0)
    return G_SOURCE_CONTINUE;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stex));
  priv->remipmap_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static void
meta_shaped_texture_paint (ClutterActor *actor)
{
  MetaShapedTexture *stex = (MetaShapedTexture *) actor;
  MetaShapedTexturePrivate *priv = stex->priv;
  guint tex_width, tex_height;
  guchar opacity;
  CoglContext *ctx;
  CoglFramebuffer *fb;
  CoglTexture *paint_tex = NULL;
  ClutterActorBox alloc;
  CoglPipelineFilter filter;
  gint64 now = g_get_monotonic_time ();

  if (priv->clip_region && cairo_region_is_empty (priv->clip_region))
    return;

  if (!CLUTTER_ACTOR_IS_REALIZED (CLUTTER_ACTOR (stex)))
    clutter_actor_realize (CLUTTER_ACTOR (stex));

  /* The GL EXT_texture_from_pixmap extension does allow for it to be
   * used together with SGIS_generate_mipmap, however this is very
   * rarely supported. Also, even when it is supported there
   * are distinct performance implications from:
   *
   *  - Updating mipmaps that we don't need
   *  - Having to reallocate pixmaps on the server into larger buffers
   *
   * So, we just unconditionally use our mipmap emulation code. If we
   * wanted to use SGIS_generate_mipmap, we'd have to  query COGL to
   * see if it was supported (no API currently), and then if and only
   * if that was the case, set the clutter texture quality to HIGH.
   * Setting the texture quality to high without SGIS_generate_mipmap
   * support for TFP textures will result in fallbacks to XGetImage.
   */
  if (priv->create_mipmaps && priv->last_invalidation)
    {
      gint64 age = now - priv->last_invalidation;

      if (age >= MIN_MIPMAP_AGE_USEC ||
          priv->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        paint_tex = meta_texture_tower_get_paint_texture (priv->paint_tower);
    }

  if (paint_tex == NULL)
    {
      paint_tex = COGL_TEXTURE (priv->texture);

      if (paint_tex == NULL)
        return;

      if (priv->create_mipmaps)
        {
          /* Minus 1000 to ensure we don't fail the age test in timeout */
          priv->earliest_remipmap = now + MIN_MIPMAP_AGE_USEC - 1000;

          if (!priv->remipmap_timeout_id)
            priv->remipmap_timeout_id =
              g_timeout_add (MIN_MIPMAP_AGE_USEC / 1000,
                             texture_is_idle_and_not_mipmapped,
                             stex);
        }
    }

  tex_width = priv->tex_width;
  tex_height = priv->tex_height;

  if (tex_width == 0 || tex_height == 0) /* no contents yet */
    return;

  cairo_rectangle_int_t tex_rect = { 0, 0, tex_width, tex_height };

  /* Use nearest-pixel interpolation if the texture is unscaled. This
   * improves performance, especially with software rendering.
   */

  filter = COGL_PIPELINE_FILTER_LINEAR;

  if (meta_actor_painting_untransformed (tex_width, tex_height, NULL, NULL))
    filter = COGL_PIPELINE_FILTER_NEAREST;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  fb = cogl_get_draw_framebuffer ();

  opacity = clutter_actor_get_paint_opacity (actor);
  clutter_actor_get_allocation_box (actor, &alloc);

  cairo_region_t *blended_region;
  gboolean use_opaque_region = (priv->opaque_region != NULL && opacity == 255);

  if (use_opaque_region)
    {
      if (priv->clip_region != NULL)
        blended_region = cairo_region_copy (priv->clip_region);
      else
        blended_region = cairo_region_create_rectangle (&tex_rect);

      cairo_region_subtract (blended_region, priv->opaque_region);
    }
  else
    {
      if (priv->clip_region != NULL)
        blended_region = cairo_region_reference (priv->clip_region);
      else
        blended_region = NULL;
    }

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 16

  if (blended_region != NULL)
    {
      int n_rects = cairo_region_num_rectangles (blended_region);
      if (n_rects > MAX_RECTS)
        {
          /* Fall back to taking the fully blended path. */
          use_opaque_region = FALSE;

          cairo_region_destroy (blended_region);
          blended_region = NULL;
        }
    }

  /* First, paint the unblended parts, which are part of the opaque region. */
  if (use_opaque_region)
    {
      cairo_region_t *region;
      int n_rects;
      int i;

      if (priv->clip_region != NULL)
        {
          region = cairo_region_copy (priv->clip_region);
          cairo_region_intersect (region, priv->opaque_region);
        }
      else
        {
          region = cairo_region_reference (priv->opaque_region);
        }

      if (!cairo_region_is_empty (region))
        {
          CoglPipeline *opaque_pipeline = get_unblended_pipeline (ctx);
          cogl_pipeline_set_layer_texture (opaque_pipeline, 0, paint_tex);
          cogl_pipeline_set_layer_filters (opaque_pipeline, 0, filter, filter);

          n_rects = cairo_region_num_rectangles (region);
          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (region, i, &rect);
              paint_clipped_rectangle (fb, opaque_pipeline, &rect, &alloc);
            }
        }

      cairo_region_destroy (region);
    }

  /* Now, go ahead and paint the blended parts. */

  /* We have three cases:
   *   1) blended_region has rectangles - paint the rectangles.
   *   2) blended_region is empty - don't paint anything
   *   3) blended_region is NULL - paint fully-blended.
   *
   *   1) and 3) are the times where we have to paint stuff. This tests
   *   for 1) and 3).
   */
  if (blended_region == NULL || !cairo_region_is_empty (blended_region))
    {
      CoglPipeline *blended_pipeline;

      if (priv->mask_texture == NULL)
        {
          blended_pipeline = get_unmasked_pipeline (ctx);
        }
      else
        {
          blended_pipeline = get_masked_pipeline (ctx);
          cogl_pipeline_set_layer_texture (blended_pipeline, 1, priv->mask_texture);
          cogl_pipeline_set_layer_filters (blended_pipeline, 1, filter, filter);
        }

      cogl_pipeline_set_layer_texture (blended_pipeline, 0, paint_tex);
      cogl_pipeline_set_layer_filters (blended_pipeline, 0, filter, filter);

      CoglColor color;
      cogl_color_init_from_4ub (&color, opacity, opacity, opacity, opacity);
      cogl_pipeline_set_color (blended_pipeline, &color);

      if (blended_region != NULL)
        {
          /* 1) blended_region is not empty. Paint the rectangles. */
          int i;
          int n_rects = cairo_region_num_rectangles (blended_region);

          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (blended_region, i, &rect);

              if (!gdk_rectangle_intersect (&tex_rect, &rect, &rect))
                continue;

              paint_clipped_rectangle (fb, blended_pipeline, &rect, &alloc);
            }
        }
      else
        {
          /* 3) blended_region is NULL. Do a full paint. */
          cogl_framebuffer_draw_rectangle (fb, blended_pipeline,
                                           0, 0,
                                           alloc.x2 - alloc.x1,
                                           alloc.y2 - alloc.y1);
        }
    }

  if (blended_region != NULL)
    cairo_region_destroy (blended_region);
}

static void
meta_shaped_texture_pick (ClutterActor       *actor,
			                    const ClutterColor *color)
{
  if (!clutter_actor_should_pick_paint (actor))
    return;

  MetaShapedTexture *stex = (MetaShapedTexture *) actor;
  MetaShapedTexturePrivate *priv = stex->priv;
  ClutterActorIter iter;
  ClutterActor *child;

  /* If there is no region then use the regular pick */
  if (priv->shape_region == NULL)
    CLUTTER_ACTOR_CLASS (meta_shaped_texture_parent_class)->pick (actor, color);
  else
    {
      int n_rects;
      float *rectangles;
      int i;
      CoglPipeline *pipeline;
      CoglContext *ctx;
      CoglFramebuffer *fb;
      CoglColor cogl_color;

      n_rects = cairo_region_num_rectangles (priv->shape_region);
      rectangles = g_alloca (sizeof (float) * 4 * n_rects);

      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          int pos = i * 4;

          cairo_region_get_rectangle (priv->shape_region, i, &rect);

          rectangles[pos + 0] = rect.x;
          rectangles[pos + 1] = rect.y;
          rectangles[pos + 2] = rect.x + rect.width;
          rectangles[pos + 3] = rect.y + rect.height;
        }

      ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
      fb = cogl_get_draw_framebuffer ();

      cogl_color_init_from_4ub (&cogl_color, color->red, color->green, color->blue, color->alpha);

      pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color (pipeline, &cogl_color);
      cogl_framebuffer_draw_rectangles (fb, pipeline, rectangles, n_rects);
      cogl_object_unref (pipeline);
    }

  clutter_actor_iter_init (&iter, actor);

  while (clutter_actor_iter_next (&iter, &child))
    clutter_actor_paint (child);
}

static void
meta_shaped_texture_get_preferred_width (ClutterActor *self,
                                         gfloat        for_height,
                                         gfloat       *min_width_p,
                                         gfloat       *natural_width_p)
{
  MetaShapedTexturePrivate *priv = META_SHAPED_TEXTURE (self)->priv;

  if (min_width_p)
    *min_width_p = priv->tex_width;

  if (natural_width_p)
    *natural_width_p = priv->tex_width;
}

static void
meta_shaped_texture_get_preferred_height (ClutterActor *self,
                                          gfloat        for_width,
                                          gfloat       *min_height_p,
                                          gfloat       *natural_height_p)
{
  MetaShapedTexturePrivate *priv = META_SHAPED_TEXTURE (self)->priv;

  if (min_height_p)
    *min_height_p = priv->tex_height;

  if (natural_height_p)
    *natural_height_p = priv->tex_height;
}

static gboolean
meta_shaped_texture_get_paint_volume (ClutterActor *self,
                                      ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, self);
}

ClutterActor *
meta_shaped_texture_new (void)
{
  ClutterActor *self = g_object_new (META_TYPE_SHAPED_TEXTURE, NULL);

  return self;
}

void
meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *stex,
					gboolean           create_mipmaps)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  create_mipmaps = create_mipmaps != FALSE;

  if (create_mipmaps != priv->create_mipmaps)
    {
      CoglTexture *base_texture;
      priv->create_mipmaps = create_mipmaps;
      base_texture = create_mipmaps ? priv->texture : NULL;

      meta_texture_tower_set_base_texture (priv->paint_tower, base_texture);
    }
}

void
meta_shaped_texture_set_shape_region (MetaShapedTexture *stex,
                                      cairo_region_t    *region)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->shape_region != NULL)
    {
      cairo_region_destroy (priv->shape_region);
      priv->shape_region = NULL;
    }

  if (region != NULL)
    {
      cairo_region_reference (region);
      priv->shape_region = region;
    }
}

/**
 * meta_shaped_texture_update_area:
 * @stex: #MetaShapedTexture
 * @x: the x coordinate of the damaged area
 * @y: the y coordinate of the damaged area
 * @width: the width of the damaged area
 * @height: the height of the damaged area
 * @unobscured_region: The unobscured region of the window or %NULL if
 * there is no valid one (like when the actor is transformed or
 * has a mapped clone)
 *
 * Repairs the damaged area indicated by @x, @y, @width and @height
 * and queues a redraw for the intersection @visibible_region and
 * the damage area. If @visibible_region is %NULL a redraw will always
 * get queued.
 *
 * Return value: Whether a redraw have been queued or not
 */
gboolean
meta_shaped_texture_update_area (MetaShapedTexture *stex,
				 int                x,
				 int                y,
				 int                width,
				 int                height,
				 cairo_region_t    *unobscured_region)
{
  MetaShapedTexturePrivate *priv;
  const cairo_rectangle_int_t clip = { x, y, width, height };

  priv = stex->priv;

  if (priv->texture == NULL)
    return FALSE;

  meta_texture_tower_update_area (priv->paint_tower, x, y, width, height);

  priv->prev_invalidation = priv->last_invalidation;
  priv->last_invalidation = g_get_monotonic_time ();

  if (priv->prev_invalidation)
    {
      gint64 interval = priv->last_invalidation - priv->prev_invalidation;
      gboolean fast_update = interval < MIN_MIPMAP_AGE_USEC;

      if (!fast_update)
        priv->fast_updates = 0;
      else if (priv->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        priv->fast_updates++;
    }

  if (unobscured_region)
    {
      cairo_region_t *intersection;

      if (cairo_region_is_empty (unobscured_region))
        return FALSE;

      intersection = cairo_region_copy (unobscured_region);
      cairo_region_intersect_rectangle (intersection, &clip);

      if (!cairo_region_is_empty (intersection))
        {
          cairo_rectangle_int_t damage_rect;
          cairo_region_get_extents (intersection, &damage_rect);
          clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stex), &damage_rect);
          cairo_region_destroy (intersection);
          return TRUE;
        }

      cairo_region_destroy (intersection);
      return FALSE;
    }
  else
    {
      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stex), &clip);
      return TRUE;
    }
}

static void
set_cogl_texture (MetaShapedTexture *stex,
                  CoglTexture     *cogl_tex)
{
  MetaShapedTexturePrivate *priv;
  guint width, height;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->texture != NULL)
    cogl_object_unref (priv->texture);

  priv->texture = cogl_tex;

  if (cogl_tex != NULL)
    {
      width = cogl_texture_get_width (COGL_TEXTURE (cogl_tex));
      height = cogl_texture_get_height (COGL_TEXTURE (cogl_tex));
    }
  else
    {
      width = 0;
      height = 0;
    }

  priv->mask_needs_update = (priv->tex_width != width ||
                             priv->tex_height != height);

  if (priv->mask_needs_update)
    {
      priv->tex_width = width;
      priv->tex_height = height;
      meta_shaped_texture_dirty_mask (stex);
      clutter_actor_queue_relayout (CLUTTER_ACTOR (stex));
      g_signal_emit (stex, signals[SIZE_CHANGED], 0);
    }

  /* NB: We don't queue a redraw of the actor here because we don't
   * know how much of the buffer has changed with respect to the
   * previous buffer. We only queue a redraw in response to surface
   * damage. */

  if (priv->create_mipmaps)
    meta_texture_tower_set_base_texture (priv->paint_tower, cogl_tex);
}

/**
 * meta_shaped_texture_set_texture:
 * @stex: The #MetaShapedTexture
 * @pixmap: The #CoglTexture to display
 */
void
meta_shaped_texture_set_texture (MetaShapedTexture *stex,
                                 CoglTexture       *texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  set_cogl_texture (stex, texture);
}

/**
 * meta_shaped_texture_get_texture:
 * @stex: The #MetaShapedTexture
 *
 * Returns: (transfer none): the unshaped texture
 */
CoglTexture *
meta_shaped_texture_get_texture (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);
  return stex->priv->texture;
}

/**
 * meta_shaped_texture_set_overlay_path:
 * @stex: a #MetaShapedTexture
 * @overlay_region: A region containing the parts of the mask to overlay.
 *   All rectangles in this region are wiped clear to full transparency,
 *   and the overlay path is clipped to this region.
 * @overlay_path: (transfer full): This path will be painted onto the mask
 *   texture with a fully opaque source. Due to the lack of refcounting
 *   in #cairo_path_t, ownership of the path is assumed.
 */
void
meta_shaped_texture_set_overlay_path (MetaShapedTexture *stex,
                                      cairo_region_t    *overlay_region,
                                      cairo_path_t      *overlay_path)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->overlay_region != NULL)
    {
      cairo_region_destroy (priv->overlay_region);
      priv->overlay_region = NULL;
    }

  if (priv->overlay_path != NULL)
    {
      cairo_path_destroy (priv->overlay_path);
      priv->overlay_path = NULL;
    }

  cairo_region_reference (overlay_region);
  priv->overlay_region = overlay_region;

  /* cairo_path_t does not have refcounting. */
  priv->overlay_path = overlay_path;
}

/**
 * meta_shaped_texture_set_clip_region:
 * @stex: a #MetaShapedTexture
 * @clip_region: the region of the texture that is visible and
 *   should be painted.
 *
 * Provides a hint to the texture about what areas of the texture
 * are not completely obscured and thus need to be painted. This
 * is an optimization and is not supposed to have any effect on
 * the output.
 *
 * Typically a parent container will set the clip region before
 * painting its children, and then unset it afterwards.
 */
void
meta_shaped_texture_set_clip_region (MetaShapedTexture *stex,
				     cairo_region_t    *clip_region)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->clip_region)
    cairo_region_destroy (priv->clip_region);

  if (clip_region)
    priv->clip_region = cairo_region_copy (clip_region);
  else
    priv->clip_region = NULL;
}

/**
 * meta_shaped_texture_set_opaque_region:
 * @stex: a #MetaShapedTexture
 * @opaque_region: (transfer full): the region of the texture that
 *   can have blending turned off.
 *
 * As most windows have a large portion that does not require blending,
 * we can easily turn off blending if we know the areas that do not
 * require blending. This sets the region where we will not blend for
 * optimization purposes.
 */
void
meta_shaped_texture_set_opaque_region (MetaShapedTexture *stex,
                                       cairo_region_t    *opaque_region)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->opaque_region)
    cairo_region_destroy (priv->opaque_region);

  if (opaque_region)
    priv->opaque_region = cairo_region_reference (opaque_region);
  else
    priv->opaque_region = NULL;
}

/**
 * meta_shaped_texture_get_image:
 * @stex: A #MetaShapedTexture
 * @clip: A clipping rectangle, to help prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the texture, the rectangle will be clipped.
 *
 * Flattens the two layers of the shaped texture into one ARGB32
 * image by alpha blending the two images, and returns the flattened
 * image.
 *
 * Returns: (transfer full): a new cairo surface to be freed with
 * cairo_surface_destroy().
 */
cairo_surface_t *
meta_shaped_texture_get_image (MetaShapedTexture     *stex,
                               cairo_rectangle_int_t *clip)
{
  CoglTexture *texture, *mask_texture;
  cairo_rectangle_int_t texture_rect = { 0, 0, 0, 0 };
  cairo_surface_t *surface;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);

  texture = stex->priv->texture;

  if (texture == NULL)
    return NULL;

  texture_rect.width = cogl_texture_get_width (texture);
  texture_rect.height = cogl_texture_get_height (texture);

  if (clip != NULL)
    {
      /* GdkRectangle is just a typedef of cairo_rectangle_int_t,
       * so we can use the gdk_rectangle_* APIs on these. */
      if (!gdk_rectangle_intersect (&texture_rect, clip, clip))
        return NULL;
    }

  if (clip != NULL)
    texture = cogl_texture_new_from_sub_texture (texture,
                                                 clip->x,
                                                 clip->y,
                                                 clip->width,
                                                 clip->height);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        cogl_texture_get_width (texture),
                                        cogl_texture_get_height (texture));

  cogl_texture_get_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                         cairo_image_surface_get_stride (surface),
                         cairo_image_surface_get_data (surface));

  cairo_surface_mark_dirty (surface);

  if (clip != NULL)
    cogl_object_unref (texture);

  mask_texture = stex->priv->mask_texture;
  if (mask_texture != NULL)
    {
      cairo_t *cr;
      cairo_surface_t *mask_surface;

      if (clip != NULL)
        mask_texture = cogl_texture_new_from_sub_texture (mask_texture,
                                                          clip->x,
                                                          clip->y,
                                                          clip->width,
                                                          clip->height);

      mask_surface = cairo_image_surface_create (CAIRO_FORMAT_A8,
                                                 cogl_texture_get_width (mask_texture),
                                                 cogl_texture_get_height (mask_texture));

      cogl_texture_get_data (mask_texture, COGL_PIXEL_FORMAT_A_8,
                             cairo_image_surface_get_stride (mask_surface),
                             cairo_image_surface_get_data (mask_surface));

      cairo_surface_mark_dirty (mask_surface);

      cr = cairo_create (surface);
      cairo_set_source_surface (cr, mask_surface, 0, 0);
      cairo_set_operator (cr, CAIRO_OPERATOR_DEST_IN);
      cairo_paint (cr);
      cairo_destroy (cr);

      cairo_surface_destroy (mask_surface);

      if (clip != NULL)
        cogl_object_unref (mask_texture);
    }

  return surface;
}
