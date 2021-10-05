/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2009 Sander Dijkhuis
 * Copyright 2014 Red Hat, Inc.
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
 * Portions adapted from gnome-shell/src/shell-global.c
 */

/**
 * SECTION:meta-background-actor
 * @title: MetaBackgroundActor
 * @short_description: Actor for painting the root window background
 *
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

#include "config.h"

#include "compositor/meta-background-actor-private.h"

#include "clutter/clutter.h"
#include "compositor/clutter-utils.h"
#include "compositor/cogl-utils.h"
#include "compositor/meta-background-private.h"
#include "compositor/meta-cullable.h"
#include "meta/display.h"
#include "meta/meta-x11-errors.h"

enum
{
  PROP_META_DISPLAY = 1,
  PROP_MONITOR,
  PROP_BACKGROUND,
  PROP_GRADIENT,
  PROP_GRADIENT_HEIGHT,
  PROP_GRADIENT_MAX_DARKNESS,
  PROP_VIGNETTE,
  PROP_VIGNETTE_SHARPNESS,
  PROP_VIGNETTE_BRIGHTNESS
};

typedef enum
{
  CHANGED_BACKGROUND = 1 << 0,
  CHANGED_EFFECTS = 1 << 2,
  CHANGED_VIGNETTE_PARAMETERS = 1 << 3,
  CHANGED_GRADIENT_PARAMETERS = 1 << 4,
  CHANGED_ALL = 0xFFFF
} ChangedFlags;

#define GRADIENT_VERTEX_SHADER_DECLARATIONS                             \
"uniform vec2 scale;\n"                                                 \
"varying vec2 position;\n"                                              \

#define GRADIENT_VERTEX_SHADER_CODE                                     \
"position = cogl_tex_coord0_in.xy * scale;\n"                           \

#define GRADIENT_FRAGMENT_SHADER_DECLARATIONS                           \
"uniform float gradient_height_perc;\n"                                 \
"uniform float gradient_max_darkness;\n"                                \
"varying vec2 position;\n"                                              \

#define GRADIENT_FRAGMENT_SHADER_CODE                                                    \
"float min_brightness = 1.0 - gradient_max_darkness;\n"                                  \
"float gradient_y_pos = min(position.y, gradient_height_perc) / gradient_height_perc;\n" \
"float pixel_brightness = (1.0 - min_brightness) * gradient_y_pos + min_brightness;\n"   \
"cogl_color_out.rgb = cogl_color_out.rgb * pixel_brightness;\n"                          \

#define VIGNETTE_VERTEX_SHADER_DECLARATIONS                             \
"uniform vec2 scale;\n"                                                 \
"uniform vec2 offset;\n"                                                \
"varying vec2 position;\n"                                              \

#define VIGNETTE_VERTEX_SHADER_CODE                                     \
"position = cogl_tex_coord0_in.xy * scale + offset;\n"                  \

#define VIGNETTE_SQRT_2 "1.4142"

#define VIGNETTE_FRAGMENT_SHADER_DECLARATIONS                                                   \
"uniform float vignette_sharpness;\n"                                                           \
"varying vec2 position;\n"                                                                      \
"float rand(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123); }\n"    \

#define VIGNETTE_FRAGMENT_SHADER_CODE                                          \
"float t = " VIGNETTE_SQRT_2 " * length(position);\n"                          \
"t = min(t, 1.0);\n"                                                           \
"float pixel_brightness = 1.0 - t * vignette_sharpness;\n"                     \
"cogl_color_out.rgb = cogl_color_out.rgb * pixel_brightness;\n"                \
"cogl_color_out.rgb += (rand(position) - 0.5) / 255.0;\n"                      \

typedef struct _MetaBackgroundLayer MetaBackgroundLayer;

typedef enum
{
  PIPELINE_VIGNETTE = (1 << 0),
  PIPELINE_BLEND = (1 << 1),
  PIPELINE_GRADIENT = (1 << 2),
} PipelineFlags;

struct _MetaBackgroundActor
{
  ClutterActor parent;

  MetaDisplay *display;
  int monitor;

  MetaBackground *background;

  gboolean gradient;
  double gradient_max_darkness;
  int gradient_height;

  gboolean vignette;
  double vignette_brightness;
  double vignette_sharpness;

  ChangedFlags changed;
  CoglPipeline *pipeline;
  PipelineFlags pipeline_flags;
  cairo_rectangle_int_t texture_area;
  gboolean force_bilinear;

  cairo_region_t *clip_region;
  cairo_region_t *unobscured_region;
};

static void cullable_iface_init (MetaCullableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackgroundActor, meta_background_actor, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

static void
set_clip_region (MetaBackgroundActor *self,
                 cairo_region_t      *clip_region)
{
  g_clear_pointer (&self->clip_region, cairo_region_destroy);
  if (clip_region)
    {
      if (cairo_region_is_empty (clip_region))
        self->clip_region = cairo_region_reference (clip_region);
      else
        self->clip_region = cairo_region_copy (clip_region);
    }
}

static void
set_unobscured_region (MetaBackgroundActor *self,
                       cairo_region_t      *unobscured_region)
{
  g_clear_pointer (&self->unobscured_region, cairo_region_destroy);
  if (unobscured_region)
    {
      if (cairo_region_is_empty (unobscured_region))
        self->unobscured_region = cairo_region_reference (unobscured_region);
      else
        self->unobscured_region = cairo_region_copy (unobscured_region);
    }
}

static void
meta_background_actor_dispose (GObject *object)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);

  set_clip_region (self, NULL);
  set_unobscured_region (self, NULL);
  meta_background_actor_set_background (self, NULL);
  if (self->pipeline)
    {
      cogl_object_unref (self->pipeline);
      self->pipeline = NULL;
    }

  G_OBJECT_CLASS (meta_background_actor_parent_class)->dispose (object);
}

static void
get_preferred_size (MetaBackgroundActor *self,
                    gfloat              *width,
                    gfloat              *height)
{
  MetaRectangle monitor_geometry;

  meta_display_get_monitor_geometry (self->display,
                                     self->monitor,
                                     &monitor_geometry);

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
  static CoglPipeline *templates[8];
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
          static CoglSnippet *vignette_vertex_snippet;
          static CoglSnippet *vignette_fragment_snippet;

          if (!vignette_vertex_snippet)
            vignette_vertex_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                                        VIGNETTE_VERTEX_SHADER_DECLARATIONS,
                                                        VIGNETTE_VERTEX_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, vignette_vertex_snippet);

          if (!vignette_fragment_snippet)
            vignette_fragment_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                                          VIGNETTE_FRAGMENT_SHADER_DECLARATIONS,
                                                          VIGNETTE_FRAGMENT_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, vignette_fragment_snippet);
        }

      if ((pipeline_flags & PIPELINE_GRADIENT) != 0)
        {
          static CoglSnippet *gradient_vertex_snippet;
          static CoglSnippet *gradient_fragment_snippet;

          if (!gradient_vertex_snippet)
            gradient_vertex_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                                        GRADIENT_VERTEX_SHADER_DECLARATIONS,
                                                        GRADIENT_VERTEX_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, gradient_vertex_snippet);

          if (!gradient_fragment_snippet)
            gradient_fragment_snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                                          GRADIENT_FRAGMENT_SHADER_DECLARATIONS,
                                                          GRADIENT_FRAGMENT_SHADER_CODE);

          cogl_pipeline_add_snippet (*templatep, gradient_fragment_snippet);
        }

      if ((pipeline_flags & PIPELINE_BLEND) == 0)
        cogl_pipeline_set_blend (*templatep, "RGBA = ADD (SRC_COLOR, 0)", NULL);
    }

  return cogl_pipeline_copy (*templatep);
}

static void
setup_pipeline (MetaBackgroundActor   *self,
                ClutterPaintContext   *paint_context,
                cairo_rectangle_int_t *actor_pixel_rect)
{
  PipelineFlags pipeline_flags = 0;
  guint8 opacity;
  float color_component;
  CoglFramebuffer *fb;
  CoglPipelineFilter min_filter, mag_filter;

  opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self));
  if (opacity < 255)
    pipeline_flags |= PIPELINE_BLEND;
  if (self->vignette && clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
    pipeline_flags |= PIPELINE_VIGNETTE;
  if (self->gradient && clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
    pipeline_flags |= PIPELINE_GRADIENT;

  if (self->pipeline &&
      pipeline_flags != self->pipeline_flags)
    {
      cogl_object_unref (self->pipeline);
      self->pipeline = NULL;
    }

  if (self->pipeline == NULL)
    {
      self->pipeline_flags = pipeline_flags;
      self->pipeline = make_pipeline (pipeline_flags);
      self->changed = CHANGED_ALL;
    }

  if ((self->changed & CHANGED_BACKGROUND) != 0)
    {
      CoglPipelineWrapMode wrap_mode;
      CoglTexture *texture = meta_background_get_texture (self->background,
                                                          self->monitor,
                                                          &self->texture_area,
                                                          &wrap_mode);
      self->force_bilinear = texture &&
        (self->texture_area.width != (int)cogl_texture_get_width (texture) ||
         self->texture_area.height != (int)cogl_texture_get_height (texture));

      cogl_pipeline_set_layer_texture (self->pipeline, 0, texture);
      cogl_pipeline_set_layer_wrap_mode (self->pipeline, 0, wrap_mode);

      self->changed &= ~CHANGED_BACKGROUND;
    }

  if ((self->changed & CHANGED_VIGNETTE_PARAMETERS) != 0)
    {
      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    cogl_pipeline_get_uniform_location (self->pipeline,
                                                                        "vignette_sharpness"),
                                    self->vignette_sharpness);

      self->changed &= ~CHANGED_VIGNETTE_PARAMETERS;
    }

  if ((self->changed & CHANGED_GRADIENT_PARAMETERS) != 0)
    {
      MetaRectangle monitor_geometry;
      float gradient_height_perc;

      meta_display_get_monitor_geometry (self->display,
                                         self->monitor, &monitor_geometry);
      gradient_height_perc = MAX (0.0001, self->gradient_height / (float)monitor_geometry.height);
      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    cogl_pipeline_get_uniform_location (self->pipeline,
                                                                        "gradient_height_perc"),
                                    gradient_height_perc);
      cogl_pipeline_set_uniform_1f (self->pipeline,
                                    cogl_pipeline_get_uniform_location (self->pipeline,
                                                                        "gradient_max_darkness"),
                                    self->gradient_max_darkness);

      self->changed &= ~CHANGED_GRADIENT_PARAMETERS;
    }

  if (self->vignette)
    {
      color_component = self->vignette_brightness * opacity / 255.;

      if (!clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
        {
          /* Darken everything to match the average brightness that would
           * be there if we were drawing the vignette, which is
           * (1 - (pi/12.) * vignette_sharpness) [exercise for the reader :]
           */
          color_component *= (1 - 0.74 * self->vignette_sharpness);
        }
    }
  else
    color_component = opacity / 255.;

  cogl_pipeline_set_color4f (self->pipeline,
                             color_component,
                             color_component,
                             color_component,
                             opacity / 255.);

  fb = clutter_paint_context_get_framebuffer (paint_context);
  if (!self->force_bilinear &&
      meta_actor_painting_untransformed (fb,
                                         actor_pixel_rect->width,
                                         actor_pixel_rect->height,
                                         actor_pixel_rect->width,
                                         actor_pixel_rect->height,
                                         NULL, NULL))
    {
      min_filter = COGL_PIPELINE_FILTER_NEAREST;
      mag_filter = COGL_PIPELINE_FILTER_NEAREST;
    }
  else
    {
      min_filter = COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST;
      mag_filter = COGL_PIPELINE_FILTER_LINEAR;
    }

  cogl_pipeline_set_layer_filters (self->pipeline, 0, min_filter, mag_filter);
}

static void
set_glsl_parameters (MetaBackgroundActor   *self,
                     cairo_rectangle_int_t *actor_pixel_rect)
{
  float scale[2];
  float offset[2];

  /* Compute a scale and offset for transforming texture coordinates to the
   * coordinate system from [-0.5 to 0.5] across the area of the actor
   */
  scale[0] = self->texture_area.width / (float)actor_pixel_rect->width;
  scale[1] = self->texture_area.height / (float)actor_pixel_rect->height;
  offset[0] = self->texture_area.x / (float)actor_pixel_rect->width - 0.5;
  offset[1] = self->texture_area.y / (float)actor_pixel_rect->height - 0.5;

  cogl_pipeline_set_uniform_float (self->pipeline,
                                   cogl_pipeline_get_uniform_location (self->pipeline,
                                                                       "scale"),
                                   2, 1, scale);

  cogl_pipeline_set_uniform_float (self->pipeline,
                                   cogl_pipeline_get_uniform_location (self->pipeline,
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
meta_background_actor_paint (ClutterActor        *actor,
                             ClutterPaintContext *paint_context)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  ClutterActorBox actor_box;
  cairo_rectangle_int_t actor_pixel_rect;
  CoglFramebuffer *fb;
  cairo_region_t *region;
  int i, n_rects;

  if ((self->clip_region && cairo_region_is_empty (self->clip_region)))
    return;

  clutter_actor_get_content_box (actor, &actor_box);
  actor_pixel_rect.x = actor_box.x1;
  actor_pixel_rect.y = actor_box.y1;
  actor_pixel_rect.width = actor_box.x2 - actor_box.x1;
  actor_pixel_rect.height = actor_box.y2 - actor_box.y1;

  setup_pipeline (self, paint_context, &actor_pixel_rect);
  set_glsl_parameters (self, &actor_pixel_rect);

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 64

  fb = clutter_paint_context_get_framebuffer (paint_context);

  /* Now figure out what to actually paint.
   */
  if (self->clip_region)
    {
      region = cairo_region_copy (self->clip_region);
      cairo_region_intersect_rectangle (region, &actor_pixel_rect);
    }
  else
    {
      region = cairo_region_create_rectangle (&actor_pixel_rect);
    }

  if (self->unobscured_region)
    cairo_region_intersect (region, self->unobscured_region);

  if (cairo_region_is_empty (region))
    {
      cairo_region_destroy (region);
      return;
    }

  n_rects = cairo_region_num_rectangles (region);
  if (n_rects <= MAX_RECTS)
    {
      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (region, i, &rect);
          paint_clipped_rectangle (fb, self->pipeline, &rect,
                                   &self->texture_area);
        }
    }
  else
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_extents (region, &rect);
      paint_clipped_rectangle (fb, self->pipeline, &rect,
                               &self->texture_area);
    }

  cairo_region_destroy (region);
}

static void
meta_background_actor_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      self->display = g_value_get_object (value);
      break;
    case PROP_MONITOR:
      meta_background_actor_set_monitor (self, g_value_get_int (value));
      break;
    case PROP_BACKGROUND:
      meta_background_actor_set_background (self, g_value_get_object (value));
      break;
    case PROP_GRADIENT:
      meta_background_actor_set_gradient (self,
                                          g_value_get_boolean (value),
                                          self->gradient_height,
                                          self->gradient_max_darkness);
      break;
    case PROP_GRADIENT_HEIGHT:
      meta_background_actor_set_gradient (self,
                                          self->gradient,
                                          g_value_get_int (value),
                                          self->gradient_max_darkness);
      break;
    case PROP_GRADIENT_MAX_DARKNESS:
      meta_background_actor_set_gradient (self,
                                          self->gradient,
                                          self->gradient_height,
                                          g_value_get_double (value));
      break;
    case PROP_VIGNETTE:
      meta_background_actor_set_vignette (self,
                                          g_value_get_boolean (value),
                                          self->vignette_brightness,
                                          self->vignette_sharpness);
      break;
    case PROP_VIGNETTE_SHARPNESS:
      meta_background_actor_set_vignette (self,
                                          self->vignette,
                                          self->vignette_brightness,
                                          g_value_get_double (value));
      break;
    case PROP_VIGNETTE_BRIGHTNESS:
      meta_background_actor_set_vignette (self,
                                          self->vignette,
                                          g_value_get_double (value),
                                          self->vignette_sharpness);
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
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    case PROP_MONITOR:
      g_value_set_int (value, self->monitor);
      break;
    case PROP_BACKGROUND:
      g_value_set_object (value, self->background);
      break;
    case PROP_GRADIENT:
      g_value_set_boolean (value, self->gradient);
      break;
    case PROP_GRADIENT_HEIGHT:
      g_value_set_int (value, self->gradient_height);
      break;
    case PROP_GRADIENT_MAX_DARKNESS:
      g_value_set_double (value, self->gradient_max_darkness);
      break;
    case PROP_VIGNETTE:
      g_value_set_boolean (value, self->vignette);
      break;
    case PROP_VIGNETTE_BRIGHTNESS:
      g_value_set_double (value, self->vignette_brightness);
      break;
    case PROP_VIGNETTE_SHARPNESS:
      g_value_set_double (value, self->vignette_sharpness);
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

  object_class->dispose = meta_background_actor_dispose;
  object_class->set_property = meta_background_actor_set_property;
  object_class->get_property = meta_background_actor_get_property;

  actor_class->get_preferred_width = meta_background_actor_get_preferred_width;
  actor_class->get_preferred_height = meta_background_actor_get_preferred_height;
  actor_class->get_paint_volume = meta_background_actor_get_paint_volume;
  actor_class->paint = meta_background_actor_paint;

  param_spec = g_param_spec_object ("meta-display",
                                    "MetaDisplay",
                                    "MetaDisplay",
                                    META_TYPE_DISPLAY,
                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_DISPLAY,
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

  param_spec = g_param_spec_boolean ("gradient",
                                     "Gradient",
                                     "Whether gradient effect is enabled",
                                     FALSE,
                                     G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_GRADIENT,
                                   param_spec);

  param_spec = g_param_spec_int ("gradient-height",
                                 "Gradient Height",
                                 "Height of gradient effect",
                                 0, G_MAXINT, 0,
                                 G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_GRADIENT_HEIGHT,
                                   param_spec);

  param_spec = g_param_spec_double ("gradient-max-darkness",
                                    "Gradient Max Darkness",
                                    "How dark is the gradient initially",
                                     0.0, 1.0, 0.0,
                                     G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_GRADIENT_MAX_DARKNESS,
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
                                    "Vignette Brightness",
                                    "Brightness of vignette effect",
                                    0.0, 1.0, 1.0,
                                    G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_VIGNETTE_BRIGHTNESS,
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
  self->gradient = FALSE;
  self->gradient_height = 0;
  self->gradient_max_darkness = 0.0;

  self->vignette = FALSE;
  self->vignette_brightness = 1.0;
  self->vignette_sharpness = 0.0;
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
meta_background_actor_new (MetaDisplay *display,
                           int          monitor)
{
  MetaBackgroundActor *self;

  self = g_object_new (META_TYPE_BACKGROUND_ACTOR,
                       "meta-display", display,
                       "monitor", monitor,
                       NULL);

  return CLUTTER_ACTOR (self);
}

static void
meta_background_actor_cull_out (MetaCullable   *cullable,
                                cairo_region_t *unobscured_region,
                                cairo_region_t *clip_region)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (cullable);

  set_unobscured_region (self, unobscured_region);
  set_clip_region (self, clip_region);
}

static void
meta_background_actor_reset_culling (MetaCullable *cullable)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (cullable);

  set_unobscured_region (self, NULL);
  set_clip_region (self, NULL);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_background_actor_cull_out;
  iface->reset_culling = meta_background_actor_reset_culling;
}

/**
 * meta_background_actor_get_clip_region:
 * @self: a #MetaBackgroundActor
 *
 * Return value (transfer none): a #cairo_region_t that represents the part of
 * the background not obscured by other #MetaBackgroundActor or
 * #MetaWindowActor objects.
 */
cairo_region_t *
meta_background_actor_get_clip_region (MetaBackgroundActor *self)
{
  return self->clip_region;
}

static void
invalidate_pipeline (MetaBackgroundActor *self,
                     ChangedFlags         changed)
{
  self->changed |= changed;
}

static void
on_background_changed (MetaBackground      *background,
                       MetaBackgroundActor *self)
{
  invalidate_pipeline (self, CHANGED_BACKGROUND);
  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_background_actor_set_background (MetaBackgroundActor *self,
                                      MetaBackground      *background)
{
  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));
  g_return_if_fail (background == NULL || META_IS_BACKGROUND (background));

  if (background == self->background)
    return;

  if (self->background)
    {
      g_signal_handlers_disconnect_by_func (self->background,
                                            (gpointer)on_background_changed,
                                            self);
      g_object_unref (self->background);
      self->background = NULL;
    }

  if (background)
    {
      self->background = g_object_ref (background);
      g_signal_connect (self->background, "changed",
                        G_CALLBACK (on_background_changed), self);
    }

  invalidate_pipeline (self, CHANGED_BACKGROUND);
  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_background_actor_set_gradient (MetaBackgroundActor *self,
                                    gboolean             enabled,
                                    int                  height,
                                    double               max_darkness)
{
  gboolean changed = FALSE;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));
  g_return_if_fail (height >= 0);
  g_return_if_fail (max_darkness >= 0. && max_darkness <= 1.);

  enabled = enabled != FALSE && height != 0;

  if (enabled != self->gradient)
    {
      self->gradient = enabled;
      invalidate_pipeline (self, CHANGED_EFFECTS);
      changed = TRUE;
    }

  if (height != self->gradient_height || max_darkness != self->gradient_max_darkness)
    {
      self->gradient_height = height;
      self->gradient_max_darkness = max_darkness;
      invalidate_pipeline (self, CHANGED_GRADIENT_PARAMETERS);
      changed = TRUE;
    }

  if (changed)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_background_actor_set_monitor (MetaBackgroundActor *self,
                                   int                  monitor)
{
  MetaRectangle old_monitor_geometry;
  MetaRectangle new_monitor_geometry;
  MetaDisplay *display = self->display;

  if(self->monitor == monitor)
      return;

  meta_display_get_monitor_geometry (display, self->monitor, &old_monitor_geometry);
  meta_display_get_monitor_geometry (display, monitor, &new_monitor_geometry);
  if(old_monitor_geometry.height != new_monitor_geometry.height)
      invalidate_pipeline (self, CHANGED_GRADIENT_PARAMETERS);

  self->monitor = monitor;
}

void
meta_background_actor_set_vignette (MetaBackgroundActor *self,
                                    gboolean             enabled,
                                    double               brightness,
                                    double               sharpness)
{
  gboolean changed = FALSE;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));
  g_return_if_fail (brightness >= 0. && brightness <= 1.);
  g_return_if_fail (sharpness >= 0.);

  enabled = enabled != FALSE;

  if (enabled != self->vignette)
    {
      self->vignette = enabled;
      invalidate_pipeline (self, CHANGED_EFFECTS);
      changed = TRUE;
    }

  if (brightness != self->vignette_brightness || sharpness != self->vignette_sharpness)
    {
      self->vignette_brightness = brightness;
      self->vignette_sharpness = sharpness;
      invalidate_pipeline (self, CHANGED_VIGNETTE_PARAMETERS);
      changed = TRUE;
    }

  if (changed)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}
