/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background-actor.c: Actor for painting the root window background
 *
 * Copyright 2009 Sander Dijkhuis
 * Copyright 2010 Red Hat, Inc.
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
 *
 * Portions adapted from gnome-shell/src/shell-global.c
 */

/**
 * SECTION:meta-background-actor
 * @title: MetaBackgroundActor
 * @short_description: Actor for painting the root window background
 */

/*
 * The overall model drawing model of this widget is that we have one
 * texture, or two interpolated textures, possibly with alpha or
 * margins that let the underlying background show through, blended
 * over a solid color or a gradient. The result of that combination
 * can then be affected by a "vignette" that darkens the background
 * away from a central point (or as a no-GLSL fallback, simply darkens
 * the background) and by overall opacity.
 *
 * As of GNOME 3.14, GNOME is only using a fraction of this when the
 * user sets the background through the control center - what can be
 * set is:
 *
 *  A single image without a border
 *  An animation of images without a border that blend together,
 *   with the blend changing every 4-5 minutes
 *  A solid color with a repeated noise texture blended over it
 *
 * This all is pretty easy to do in a fragment shader, except when:
 *
 *  A) We don't have GLSL - in this case, the operation of
 *     interpolating the two textures and blending the result over the
 *     background can't be expressed with Cogl's fixed-function layer
 *     combining (which is confined to what GL's texture environment
 *     combining can do) So we can only handle the above directly if
 *     there are no margins or alpha.
 *
 *  B) The image textures are sliced. Texture size limits on older
 *     hardware (pre-965 intel hardware, r300, etc.)  is often 2048,
 *     and it would be common to use a texture larger than this for a
 *     background and expect it to be scaled down. Cogl can compensate
 *     for this by breaking the texture up into multiple textures, but
 *     can't multitexture with sliced textures. So we can only handle
 *     the above if there's a single texture.
 *
 * However, even when we *can* represent everything in a single pass,
 * it's not necessarily efficient. If we want to draw a 1024x768
 * background, it's pretty inefficient to bilinearly texture from
 * two 2560x1440 images and mix that. So the drawing model we take
 * here is that MetaBackground generates a single texture (which
 * might be a 1x1 texture for a solid color, or a 1x2 texture for a
 * gradient, or a repeated texture for wallpaper, or a pre-rendered
 * texture the size of the screen), and we draw with that, possibly
 * adding the vignette and opacity.
 */

#include <config.h>

#include <clutter/clutter.h>

#include "cogl-utils.h"
#include "clutter-utils.h"
#include <meta/errors.h>
#include "meta-background-actor-private.h"
#include "meta-background-private.h"

enum
{
  PROP_META_SCREEN = 1,
  PROP_MONITOR,
  PROP_BACKGROUND,
  PROP_VIGNETTE,
  PROP_VIGNETTE_SHARPNESS,
  PROP_BRIGHTNESS
};

typedef enum {
  CHANGED_BACKGROUND = 1 << 0,
  CHANGED_EFFECTS = 1 << 2,
  CHANGED_VIGNETTE_PARAMETERS = 1 << 3,
  CHANGED_ALL = 0xFFFF
} ChangedFlags;

#define VERTEX_SHADER_DECLARATIONS                                      \
"uniform vec2 scale;\n"                                                 \
"uniform vec2 offset;\n"                                                \
"varying vec2 position;\n"                                              \

#define VERTEX_SHADER_CODE                                              \
"position = cogl_tex_coord0_in.xy * scale + offset;\n"                  \

#define FRAGMENT_SHADER_DECLARATIONS                                    \
"uniform float vignette_sharpness;\n"                                   \
"varying vec2 position;\n"                                              \

#define FRAGMENT_SHADER_CODE                                                   \
"float t = 2.0 * length(position);\n"                                          \
"t = min(t, 1.0);\n"                                                           \
"float pixel_brightness = 1.0 - t * vignette_sharpness;\n"                     \
"cogl_color_out.rgb = cogl_color_out.rgb * pixel_brightness;\n"                \

typedef struct _MetaBackgroundLayer MetaBackgroundLayer;

typedef enum {
  PIPELINE_VIGNETTE = (1 << 0),
  PIPELINE_BLEND = (1 << 1),
} PipelineFlags;

struct _MetaBackgroundActorPrivate
{
  MetaScreen *screen;
  int monitor;

  MetaBackground *background;

  gboolean vignette;
  double brightness;
  double vignette_sharpness;

  ChangedFlags changed;
  CoglPipeline *pipeline;
  PipelineFlags pipeline_flags;
  cairo_rectangle_int_t texture_area;
  gboolean force_bilinear;

  cairo_region_t *visible_region;
};

G_DEFINE_TYPE (MetaBackgroundActor, meta_background_actor, CLUTTER_TYPE_ACTOR);

static void
meta_background_actor_dispose (GObject *object)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);
  MetaBackgroundActorPrivate *priv = self->priv;

  meta_background_actor_set_visible_region (self, NULL);
  meta_background_actor_set_background (self, NULL);
  if (priv->pipeline)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }

  G_OBJECT_CLASS (meta_background_actor_parent_class)->dispose (object);
}

static void
get_preferred_size (MetaBackgroundActor *self,
                    gfloat              *width,
                    gfloat              *height)
{
  MetaBackgroundActorPrivate *priv = META_BACKGROUND_ACTOR (self)->priv;
  MetaRectangle monitor_geometry;

  meta_screen_get_monitor_geometry (priv->screen, priv->monitor, &monitor_geometry);

  if (width != NULL)
    *width = monitor_geometry.width;

  if (height != NULL)
    *height = monitor_geometry.height;
}

static void
meta_background_actor_get_preferred_width (ClutterActor *actor,
                                           gfloat        for_height,
                                           gfloat       *min_width_p,
                                           gfloat       *natural_width_p)
{
  gfloat width;

  get_preferred_size (META_BACKGROUND_ACTOR (actor), &width, NULL);

  if (min_width_p)
    *min_width_p = width;
  if (natural_width_p)
    *natural_width_p = width;
}

static void
meta_background_actor_get_preferred_height (ClutterActor *actor,
                                            gfloat        for_width,
                                            gfloat       *min_height_p,
                                            gfloat       *natural_height_p)

{
  gfloat height;

  get_preferred_size (META_BACKGROUND_ACTOR (actor), NULL, &height);

  if (min_height_p)
    *min_height_p = height;
  if (natural_height_p)
    *natural_height_p = height;
}

static CoglPipeline *
make_pipeline (PipelineFlags pipeline_flags)
{
  static CoglPipeline *templates[4];
  CoglPipeline **templatep;

  templatep = &templates[pipeline_flags];
  if (*templatep == NULL)
    {
      /* Cogl automatically caches pipelines with no eviction policy,
       * so we need to prevent identical pipelines from getting cached
       * separately, by reusing the same shader snippets.
       */
      *templatep = COGL_PIPELINE (meta_create_texture_pipeline (NULL));

      if ((pipeline_flags & PIPELINE_VIGNETTE) != 0)
        {
          static CoglSnippet *vertex_snippet;
          static CoglSnippet *fragment_snippet;

          if (!vertex_snippet)
            vertex_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                               VERTEX_SHADER_DECLARATIONS, VERTEX_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, vertex_snippet);

          if (!fragment_snippet)
            fragment_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                                 FRAGMENT_SHADER_DECLARATIONS, FRAGMENT_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, fragment_snippet);
        }

      if ((pipeline_flags & PIPELINE_BLEND) == 0)
        cogl_pipeline_set_blend (*templatep, "RGBA = ADD (SRC_COLOR, 0)", NULL);
    }

  return cogl_pipeline_copy (*templatep);
}

static void
setup_pipeline (MetaBackgroundActor   *self,
                cairo_rectangle_int_t *actor_pixel_rect)
{
  MetaBackgroundActorPrivate *priv = self->priv;
  PipelineFlags pipeline_flags = 0;
  guint8 opacity;
  float color_component;
  CoglPipelineFilter filter;

  opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self));
  if (opacity < 255)
    pipeline_flags |= PIPELINE_BLEND;
  if (priv->vignette && clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
    pipeline_flags |= PIPELINE_VIGNETTE;

  if (priv->pipeline &&
      pipeline_flags != priv->pipeline_flags)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }

  if (priv->pipeline == NULL)
    {
      priv->pipeline_flags = pipeline_flags;
      priv->pipeline = make_pipeline (pipeline_flags);
      priv->changed = CHANGED_ALL;
    }

  if ((priv->changed & CHANGED_BACKGROUND) != 0)
    {
      CoglPipelineWrapMode wrap_mode;
      CoglTexture *texture = meta_background_get_texture (priv->background,
                                                          priv->monitor,
                                                          &priv->texture_area,
                                                          &wrap_mode);
      priv->force_bilinear = texture &&
        (priv->texture_area.width != (int)cogl_texture_get_width (texture) ||
         priv->texture_area.height != (int)cogl_texture_get_height (texture));

      cogl_pipeline_set_layer_texture (priv->pipeline, 0, texture);
      cogl_pipeline_set_layer_wrap_mode (priv->pipeline, 0, wrap_mode);
    }

  if ((priv->changed & CHANGED_VIGNETTE_PARAMETERS) != 0)
    {
      cogl_pipeline_set_uniform_1f (priv->pipeline,
                                    cogl_pipeline_get_uniform_location (priv->pipeline,
                                                                        "vignette_sharpness"),
                                    priv->vignette_sharpness);
    }

  if (priv->vignette)
    {
      color_component = priv->brightness * opacity / 255.;

      if (!clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
        {
          /* Darken everything to match the average brightness that would
           * be there if we were drawing the vignette, which is
           * (1 - (pi/12.) * vignette_sharpness) [exercise for the reader :]
           */
          color_component *= (1 - 0.74 * priv->vignette_sharpness);
        }
    }
  else
    color_component = opacity / 255.;

  cogl_pipeline_set_color4f (priv->pipeline,
                             color_component,
                             color_component,
                             color_component,
                             opacity / 255.);

  if (!priv->force_bilinear &&
      meta_actor_painting_untransformed (actor_pixel_rect->width, actor_pixel_rect->height, NULL, NULL))
    filter = COGL_PIPELINE_FILTER_NEAREST;
  else
    filter = COGL_PIPELINE_FILTER_LINEAR;

  cogl_pipeline_set_layer_filters (priv->pipeline, 0, filter, filter);
}

static void
set_glsl_parameters (MetaBackgroundActor   *self,
                     cairo_rectangle_int_t *actor_pixel_rect)
{
  MetaBackgroundActorPrivate *priv = self->priv;
  float scale[2];
  float offset[2];

  /* Compute a scale and offset for transforming texture coordinates to the
   * coordinate system from [-0.5 to 0.5] across the area of the actor
   */
  scale[0] = priv->texture_area.width / (float)actor_pixel_rect->width;
  scale[1] = priv->texture_area.height / (float)actor_pixel_rect->height;
  offset[0] = priv->texture_area.x / (float)actor_pixel_rect->width - 0.5;
  offset[1] = priv->texture_area.y / (float)actor_pixel_rect->height - 0.5;

  cogl_pipeline_set_uniform_float (priv->pipeline,
                                   cogl_pipeline_get_uniform_location (priv->pipeline,
                                                                       "scale"),
                                   2, 1, scale);

  cogl_pipeline_set_uniform_float (priv->pipeline,
                                   cogl_pipeline_get_uniform_location (priv->pipeline,
                                                                       "offset"),
                                   2, 1, offset);
}

static void
paint_clipped_rectangle (CoglFramebuffer       *fb,
                         CoglPipeline          *pipeline,
                         cairo_rectangle_int_t *rect,
                         cairo_rectangle_int_t *texture_area)
{
  float x1, y1, x2, y2;
  float tx1, ty1, tx2, ty2;

  x1 = rect->x;
  y1 = rect->y;
  x2 = rect->x + rect->width;
  y2 = rect->y + rect->height;

  tx1 = (x1 - texture_area->x) / texture_area->width;
  ty1 = (y1 - texture_area->y) / texture_area->height;
  tx2 = (x2 - texture_area->x) / texture_area->width;
  ty2 = (y2 - texture_area->y) / texture_area->height;

  cogl_framebuffer_draw_textured_rectangle (fb, pipeline,
                                            x1, y1, x2, y2,
                                            tx1, ty1, tx2, ty2);
}

static gboolean
meta_background_actor_get_paint_volume (ClutterActor       *actor,
                                        ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}

static void
meta_background_actor_paint (ClutterActor *actor)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  MetaBackgroundActorPrivate *priv = self->priv;
  ClutterActorBox actor_box;
  cairo_rectangle_int_t actor_pixel_rect;
  CoglFramebuffer *fb;
  int i;

  if ((priv->visible_region && cairo_region_is_empty (priv->visible_region)))
    return;

  clutter_actor_get_content_box (actor, &actor_box);
  actor_pixel_rect.x = actor_box.x1;
  actor_pixel_rect.y = actor_box.y1;
  actor_pixel_rect.width = actor_box.x2 - actor_box.x1;
  actor_pixel_rect.height = actor_box.y2 - actor_box.y1;

  setup_pipeline (self, &actor_pixel_rect);
  set_glsl_parameters (self, &actor_pixel_rect);

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 64

  fb = cogl_get_draw_framebuffer ();

  /* Now figure out what to actually paint.
   */
  if (priv->visible_region != NULL)
    {
      int n_rects = cairo_region_num_rectangles (priv->visible_region);
      if (n_rects <= MAX_RECTS)
        {
           for (i = 0; i < n_rects; i++)
             {
               cairo_rectangle_int_t rect;
               cairo_region_get_rectangle (priv->visible_region, i, &rect);

               if (!gdk_rectangle_intersect (&actor_pixel_rect, &rect, &rect))
                 continue;

               paint_clipped_rectangle (fb, priv->pipeline, &rect, &priv->texture_area);
             }

           return;
        }
    }

  paint_clipped_rectangle (fb, priv->pipeline, &actor_pixel_rect, &priv->texture_area);
}

static void
meta_background_actor_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);
  MetaBackgroundActorPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_META_SCREEN:
      priv->screen = g_value_get_object (value);
      break;
    case PROP_MONITOR:
      priv->monitor = g_value_get_int (value);
      break;
    case PROP_BACKGROUND:
      meta_background_actor_set_background (self, g_value_get_object (value));
      break;
    case PROP_VIGNETTE:
      meta_background_actor_set_vignette (self,
                                          g_value_get_boolean (value),
                                          priv->brightness,
                                          priv->vignette_sharpness);
      break;
    case PROP_VIGNETTE_SHARPNESS:
      meta_background_actor_set_vignette (self,
                                          priv->vignette,
                                          priv->brightness,
                                          g_value_get_double (value));
      break;
    case PROP_BRIGHTNESS:
      meta_background_actor_set_vignette (self,
                                          priv->vignette,
                                          g_value_get_double (value),
                                          priv->vignette_sharpness);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_actor_get_property (GObject      *object,
                                    guint         prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  MetaBackgroundActorPrivate *priv = META_BACKGROUND_ACTOR (object)->priv;

  switch (prop_id)
    {
    case PROP_META_SCREEN:
      g_value_set_object (value, priv->screen);
      break;
    case PROP_MONITOR:
      g_value_set_int (value, priv->monitor);
      break;
    case PROP_BACKGROUND:
      g_value_set_object (value, priv->background);
      break;
    case PROP_VIGNETTE:
      g_value_set_boolean (value, priv->vignette);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_double (value, priv->brightness);
      break;
    case PROP_VIGNETTE_SHARPNESS:
      g_value_set_double (value, priv->vignette_sharpness);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_actor_class_init (MetaBackgroundActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (MetaBackgroundActorPrivate));

  object_class->dispose = meta_background_actor_dispose;
  object_class->set_property = meta_background_actor_set_property;
  object_class->get_property = meta_background_actor_get_property;

  actor_class->get_preferred_width = meta_background_actor_get_preferred_width;
  actor_class->get_preferred_height = meta_background_actor_get_preferred_height;
  actor_class->get_paint_volume = meta_background_actor_get_paint_volume;
  actor_class->paint = meta_background_actor_paint;

  param_spec = g_param_spec_object ("meta-screen",
                                    "MetaScreen",
                                    "MetaScreen",
                                    META_TYPE_SCREEN,
                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_SCREEN,
                                   param_spec);

  param_spec = g_param_spec_int ("monitor",
                                 "monitor",
                                 "monitor",
                                 0, G_MAXINT, 0,
                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_MONITOR,
                                   param_spec);

  param_spec = g_param_spec_object ("background",
                                    "Background",
                                    "MetaBackground object holding background parameters",
                                    META_TYPE_BACKGROUND,
                                    G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_BACKGROUND,
                                   param_spec);

  param_spec = g_param_spec_boolean ("vignette",
                                     "Vignette",
                                     "Whether vignette effect is enabled",
                                     FALSE,
                                     G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_VIGNETTE,
                                   param_spec);

  param_spec = g_param_spec_double ("brightness",
                                    "Brightness",
                                    "Brightness of vignette effect",
                                    0.0, 1.0, 1.0,
                                    G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_BRIGHTNESS,
                                   param_spec);

  param_spec = g_param_spec_double ("vignette-sharpness",
                                    "Vignette Sharpness",
                                    "Sharpness of vignette effect",
                                    0.0, G_MAXDOUBLE, 0.0,
                                    G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_VIGNETTE_SHARPNESS,
                                   param_spec);
}

static void
meta_background_actor_init (MetaBackgroundActor *self)
{
  MetaBackgroundActorPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   META_TYPE_BACKGROUND_ACTOR,
                                                   MetaBackgroundActorPrivate);

  priv->vignette = FALSE;
  priv->brightness = 1.0;
  priv->vignette_sharpness = 0.0;
}

/**
 * meta_background_actor_new:
 * @monitor: Index of the monitor for which to draw the background
 *
 * Creates a new actor to draw the background for the given monitor.
 *
 * Return value: the newly created background actor
 */
ClutterActor *
meta_background_actor_new (MetaScreen *screen,
                           int         monitor)
{
  MetaBackgroundActor *self;

  self = g_object_new (META_TYPE_BACKGROUND_ACTOR,
                       "meta-screen", screen,
                       "monitor", monitor,
                       NULL);

  return CLUTTER_ACTOR (self);
}

/**
 * meta_background_actor_set_visible_region:
 * @self: a #MetaBackgroundActor
 * @visible_region: (allow-none): the area of the actor (in allocate-relative
 *   coordinates) that is visible.
 *
 * Sets the area of the background that is unobscured by overlapping windows.
 * This is used to optimize and only paint the visible portions.
 */
LOCAL_SYMBOL void
meta_background_actor_set_visible_region (MetaBackgroundActor *self,
                                          cairo_region_t      *visible_region)
{
  MetaBackgroundActorPrivate *priv;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));

  priv = self->priv;

  g_clear_pointer (&priv->visible_region,
                   (GDestroyNotify)
                   cairo_region_destroy);

  if (visible_region)
    priv->visible_region = cairo_region_copy (visible_region);
}

/**
 * meta_background_actor_get_visible_region:
 * @self: a #MetaBackgroundActor
 *
 * Return value (transfer full): a #cairo_region_t that represents the part of
 * the background not obscured by other #MetaBackgroundActor or
 * #MetaWindowActor objects.
 */
cairo_region_t *
meta_background_actor_get_visible_region (MetaBackgroundActor *self)
{
  MetaBackgroundActorPrivate *priv = self->priv;
  ClutterActorBox content_box;
  cairo_rectangle_int_t content_area = { 0 };
  cairo_region_t *visible_region;

  g_return_val_if_fail (META_IS_BACKGROUND_ACTOR (self), NULL);

  if (!priv->visible_region)
      return NULL;

  clutter_actor_get_content_box (CLUTTER_ACTOR (self), &content_box);

  content_area.x = content_box.x1;
  content_area.y = content_box.y1;
  content_area.width = content_box.x2 - content_box.x1;
  content_area.height = content_box.y2 - content_box.y1;

  visible_region = cairo_region_create_rectangle (&content_area);
  cairo_region_intersect (visible_region, priv->visible_region);

  return visible_region;
}

static void
invalidate_pipeline (MetaBackgroundActor *self,
                     ChangedFlags         changed)
{
  MetaBackgroundActorPrivate *priv = self->priv;

  priv->changed |= changed;
}

static void
on_background_changed (MetaBackground      *background,
                       MetaBackgroundActor *self)
{
  invalidate_pipeline (self, CHANGED_BACKGROUND);
}

void
meta_background_actor_set_background (MetaBackgroundActor *self,
                                      MetaBackground      *background)
{
  MetaBackgroundActorPrivate *priv;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));
  g_return_if_fail (background == NULL || META_IS_BACKGROUND (background));

  priv = self->priv;

  if (background == priv->background)
    return;

  if (priv->background)
    {
      g_signal_handlers_disconnect_by_func (priv->background,
                                            (gpointer)on_background_changed,
                                            self);
      g_object_unref (priv->background);
      priv->background = NULL;
    }

  if (background)
    {
      priv->background = g_object_ref (background);
      g_signal_connect (priv->background, "changed",
                        G_CALLBACK (on_background_changed), self);
    }

  invalidate_pipeline (self, CHANGED_BACKGROUND);
  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_background_actor_set_vignette (MetaBackgroundActor *self,
                                    gboolean             enabled,
                                    double               brightness,
                                    double               sharpness)
{
  MetaBackgroundActorPrivate *priv;
  gboolean changed = FALSE;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));
  g_return_if_fail (brightness >= 0. && brightness <= 1.);
  g_return_if_fail (sharpness >= 0.);

  priv = self->priv;

  enabled = enabled != FALSE;

  if (enabled != priv->vignette)
    {
      priv->vignette = enabled;
      invalidate_pipeline (self, CHANGED_EFFECTS);
      changed = TRUE;
    }

  if (brightness != priv->brightness || sharpness != priv->vignette_sharpness)
    {
      priv->brightness = brightness;
      priv->vignette_sharpness = sharpness;
      invalidate_pipeline (self, CHANGED_VIGNETTE_PARAMETERS);
      changed = TRUE;
    }

  if (changed)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}
