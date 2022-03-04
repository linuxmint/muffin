/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 */

#include "config.h"

#include "compositor/meta-background-private.h"

#include <string.h>

#include "backends/meta-backend-private.h"
#include "compositor/cogl-utils.h"
#include "meta/display.h"
#include "meta/meta-background-image.h"
#include "meta/meta-background.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

enum
{
  CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct _MetaBackgroundMonitor MetaBackgroundMonitor;

struct _MetaBackgroundMonitor
{
  gboolean dirty;
  CoglTexture *texture;
  CoglFramebuffer *fbo;
};

struct _MetaBackground
{
  GObject parent;

  MetaDisplay *display;
  MetaBackgroundMonitor *monitors;
  int n_monitors;

  GDesktopBackgroundStyle   style;
  GDesktopBackgroundShading shading_direction;
  ClutterColor              color;
  ClutterColor              second_color;

  GFile *file1;
  MetaBackgroundImage *background_image1;
  GFile *file2;
  MetaBackgroundImage *background_image2;

  CoglTexture *color_texture;
  CoglTexture *wallpaper_texture;

  float blend_factor;

  guint wallpaper_allocation_failed : 1;
};

enum
{
  PROP_META_DISPLAY = 1,
  PROP_MONITOR,
};

G_DEFINE_TYPE (MetaBackground, meta_background, G_TYPE_OBJECT)

static gboolean texture_has_alpha (CoglTexture *texture);

static GSList *all_backgrounds = NULL;

static void
free_fbos (MetaBackground *self)
{
  int i;

  for (i = 0; i < self->n_monitors; i++)
    {
      MetaBackgroundMonitor *monitor = &self->monitors[i];
      if (monitor->fbo)
        {
          cogl_object_unref (monitor->fbo);
          monitor->fbo = NULL;
        }
      if (monitor->texture)
        {
          cogl_object_unref (monitor->texture);
          monitor->texture = NULL;
        }
    }
}

static void
free_color_texture (MetaBackground *self)
{
  if (self->color_texture != NULL)
    {
      cogl_object_unref (self->color_texture);
      self->color_texture = NULL;
    }
}

static void
free_wallpaper_texture (MetaBackground *self)
{
  if (self->wallpaper_texture != NULL)
    {
      cogl_object_unref (self->wallpaper_texture);
      self->wallpaper_texture = NULL;
    }

  self->wallpaper_allocation_failed = FALSE;
}

static void
invalidate_monitor_backgrounds (MetaBackground *self)
{
  free_fbos (self);
  g_free (self->monitors);
  self->monitors = NULL;
  self->n_monitors = 0;

  if (self->display)
    {
      int i;

      self->n_monitors = meta_display_get_n_monitors (self->display);
      self->monitors = g_new0 (MetaBackgroundMonitor, self->n_monitors);

      for (i = 0; i < self->n_monitors; i++)
        self->monitors[i].dirty = TRUE;
    }
}

static void
on_monitors_changed (MetaBackground *self)
{
  invalidate_monitor_backgrounds (self);
}

static void
set_display (MetaBackground *self,
             MetaDisplay    *display)
{
  g_set_object (&self->display, display);

  invalidate_monitor_backgrounds (self);
}

static void
meta_background_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      set_display (META_BACKGROUND (object), g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_get_property (GObject      *object,
                              guint         prop_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
  MetaBackground *self = META_BACKGROUND (object);

  switch (prop_id)
    {
    case PROP_META_DISPLAY:
      g_value_set_object (value, self->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
need_prerender (MetaBackground *self)
{
  CoglTexture *texture1 = self->background_image1 ? meta_background_image_get_texture (self->background_image1) : NULL;
  CoglTexture *texture2 = self->background_image2 ? meta_background_image_get_texture (self->background_image2) : NULL;

  if (texture1 == NULL && texture2 == NULL)
    return FALSE;

  if (texture2 == NULL && self->style == G_DESKTOP_BACKGROUND_STYLE_WALLPAPER)
    return FALSE;

  return TRUE;
}

static void
mark_changed (MetaBackground *self)
{
  int i;

  if (!need_prerender (self))
    free_fbos (self);

  for (i = 0; i < self->n_monitors; i++)
    self->monitors[i].dirty = TRUE;

  g_signal_emit (self, signals[CHANGED], 0);
}

static void
on_background_loaded (MetaBackgroundImage *image,
                      MetaBackground      *self)
{
  mark_changed (self);
}

static gboolean
file_equal0 (GFile *file1,
             GFile *file2)
{
  if (file1 == file2)
    return TRUE;

  if ((file1 == NULL) || (file2 == NULL))
    return FALSE;

  return g_file_equal (file1, file2);
}

static void
set_file (MetaBackground       *self,
          GFile               **filep,
          MetaBackgroundImage **imagep,
          GFile                *file,
          gboolean              force_reload)
{
  if (force_reload || !file_equal0 (*filep, file))
    {
      if (*imagep)
        {
          g_signal_handlers_disconnect_by_func (*imagep,
                                                (gpointer)on_background_loaded,
                                                self);
          g_object_unref (*imagep);
          *imagep = NULL;
        }

      g_set_object (filep, file);

      if (file)
        {
          MetaBackgroundImageCache *cache = meta_background_image_cache_get_default ();

          *imagep = meta_background_image_cache_load (cache, file);
          g_signal_connect (*imagep, "loaded",
                            G_CALLBACK (on_background_loaded), self);
        }
    }
}

static void
on_gl_video_memory_purged (MetaBackground *self)
{
  MetaBackgroundImageCache *cache = meta_background_image_cache_get_default ();

  /* The GPU memory that just got invalidated is the texture inside
   * self->background_image1,2 and/or its mipmaps. However, to save memory the
   * original pixbuf isn't kept in RAM so we can't do a simple re-upload. The
   * only copy of the image was the one in texture memory that got invalidated.
   * So we need to do a full reload from disk.
   */
  if (self->file1)
    {
      meta_background_image_cache_purge (cache, self->file1);
      set_file (self, &self->file1, &self->background_image1, self->file1, TRUE);
    }

  if (self->file2)
    {
      meta_background_image_cache_purge (cache, self->file2);
      set_file (self, &self->file2, &self->background_image2, self->file2, TRUE);
    }

  mark_changed (self);
}

static void
meta_background_dispose (GObject *object)
{
  MetaBackground        *self = META_BACKGROUND (object);

  free_color_texture (self);
  free_wallpaper_texture (self);

  set_file (self, &self->file1, &self->background_image1, NULL, FALSE);
  set_file (self, &self->file2, &self->background_image2, NULL, FALSE);

  set_display (self, NULL);

  G_OBJECT_CLASS (meta_background_parent_class)->dispose (object);
}

static void
meta_background_finalize (GObject *object)
{
  all_backgrounds = g_slist_remove (all_backgrounds, object);

  G_OBJECT_CLASS (meta_background_parent_class)->finalize (object);
}

static void
meta_background_constructed (GObject *object)
{
  MetaBackground        *self = META_BACKGROUND (object);
  MetaMonitorManager *monitor_manager = meta_monitor_manager_get ();

  G_OBJECT_CLASS (meta_background_parent_class)->constructed (object);

  g_signal_connect_object (self->display, "gl-video-memory-purged",
                           G_CALLBACK (on_gl_video_memory_purged), object, G_CONNECT_SWAPPED);

  g_signal_connect_object (monitor_manager, "monitors-changed",
                           G_CALLBACK (on_monitors_changed), self,
                           G_CONNECT_SWAPPED);
}

static void
meta_background_class_init (MetaBackgroundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->dispose = meta_background_dispose;
  object_class->finalize = meta_background_finalize;
  object_class->constructed = meta_background_constructed;
  object_class->set_property = meta_background_set_property;
  object_class->get_property = meta_background_get_property;

  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  param_spec = g_param_spec_object ("meta-display",
                                    "MetaDisplay",
                                    "MetaDisplay",
                                    META_TYPE_DISPLAY,
                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_DISPLAY,
                                   param_spec);

}

static void
meta_background_init (MetaBackground *self)
{
  all_backgrounds = g_slist_prepend (all_backgrounds, self);
}

static void
set_texture_area_from_monitor_area (cairo_rectangle_int_t *monitor_area,
                                    cairo_rectangle_int_t *texture_area)
{
  texture_area->x = 0;
  texture_area->y = 0;
  texture_area->width = monitor_area->width;
  texture_area->height = monitor_area->height;
}

static void
get_texture_area (MetaBackground          *self,
                  cairo_rectangle_int_t   *monitor_rect,
                  float                    monitor_scale,
                  CoglTexture             *texture,
                  cairo_rectangle_int_t   *texture_area)
{
  cairo_rectangle_int_t image_area;
  int screen_width, screen_height;
  float texture_width, texture_height;
  float monitor_x_scale, monitor_y_scale;

  texture_width = cogl_texture_get_width (texture);
  texture_height = cogl_texture_get_height (texture);

  switch (self->style)
    {
    case G_DESKTOP_BACKGROUND_STYLE_STRETCHED:
    default:
      /* paint region is whole actor, and the texture
       * is scaled disproportionately to fit the actor
       */
      set_texture_area_from_monitor_area (monitor_rect, texture_area);
      break;
    case G_DESKTOP_BACKGROUND_STYLE_WALLPAPER:
      meta_display_get_size (self->display, &screen_width, &screen_height);

      /* Start off by centering a tile in the middle of the
       * total screen area taking care of the monitor scaling.
       */
      image_area.x = (screen_width - texture_width) / 2.0;
      image_area.y = (screen_height - texture_height) / 2.0;
      image_area.width = texture_width;
      image_area.height = texture_height;

      /* Translate into the coordinate system of the particular monitor */
      image_area.x -= monitor_rect->x;
      image_area.y -= monitor_rect->y;

      *texture_area = image_area;
      break;
    case G_DESKTOP_BACKGROUND_STYLE_CENTERED:
      /* paint region is the original image size centered in the actor,
       * and the texture is scaled to the original image size */
      image_area.width = texture_width;
      image_area.height = texture_height;
      image_area.x = monitor_rect->width / 2 - image_area.width / 2;
      image_area.y = monitor_rect->height / 2 - image_area.height / 2;

      *texture_area = image_area;
      break;
    case G_DESKTOP_BACKGROUND_STYLE_SCALED:
    case G_DESKTOP_BACKGROUND_STYLE_ZOOM:
      /* paint region is the actor size in one dimension, and centered and
       * scaled by proportional amount in the other dimension.
       *
       * SCALED forces the centered dimension to fit on screen.
       * ZOOM forces the centered dimension to grow off screen
       */
      monitor_x_scale = monitor_rect->width / texture_width;
      monitor_y_scale = monitor_rect->height / texture_height;

      if ((self->style == G_DESKTOP_BACKGROUND_STYLE_SCALED &&
           (monitor_x_scale < monitor_y_scale)) ||
          (self->style == G_DESKTOP_BACKGROUND_STYLE_ZOOM &&
           (monitor_x_scale > monitor_y_scale)))
        {
          /* Fill image to exactly fit actor horizontally */
          image_area.width = monitor_rect->width;
          image_area.height = texture_height * monitor_x_scale;

          /* Position image centered vertically in actor */
          image_area.x = 0;
          image_area.y = monitor_rect->height / 2 - image_area.height / 2;
        }
      else
        {
          /* Scale image to exactly fit actor vertically */
          image_area.width = texture_width * monitor_y_scale;
          image_area.height = monitor_rect->height;

          /* Position image centered horizontally in actor */
          image_area.x = monitor_rect->width / 2 - image_area.width / 2;
          image_area.y = 0;
        }

      *texture_area = image_area;
      break;

    case G_DESKTOP_BACKGROUND_STYLE_SPANNED:
      {
        /* paint region is the union of all monitors, with the origin
         * of the region set to align with monitor associated with the background.
         */
        meta_display_get_size (self->display, &screen_width, &screen_height);

        /* unclipped texture area is whole screen, scaled depending on monitor */
        image_area.width = screen_width * monitor_scale;
        image_area.height = screen_height * monitor_scale;

        /* But make (0,0) line up with the appropriate monitor */
        image_area.x = -monitor_rect->x;
        image_area.y = -monitor_rect->y;

        *texture_area = image_area;
        break;
      }
    }
}

static gboolean
draw_texture (MetaBackground        *self,
              CoglFramebuffer       *framebuffer,
              CoglPipeline          *pipeline,
              CoglTexture           *texture,
              cairo_rectangle_int_t *monitor_area,
              float                  monitor_scale)
{
  cairo_rectangle_int_t texture_area;
  gboolean bare_region_visible;

  get_texture_area (self, monitor_area, monitor_scale, texture, &texture_area);

  switch (self->style)
    {
    case G_DESKTOP_BACKGROUND_STYLE_STRETCHED:
    case G_DESKTOP_BACKGROUND_STYLE_WALLPAPER:
    case G_DESKTOP_BACKGROUND_STYLE_ZOOM:
    case G_DESKTOP_BACKGROUND_STYLE_SPANNED:
      /* Draw the entire monitor */
      cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                pipeline,
                                                0,
                                                0,
                                                monitor_area->width,
                                                monitor_area->height,
                                                - texture_area.x / (float)texture_area.width,
                                                - texture_area.y / (float)texture_area.height,
                                                (monitor_area->width - texture_area.x) / (float)texture_area.width,
                                                (monitor_area->height - texture_area.y) / (float)texture_area.height);

      bare_region_visible = texture_has_alpha (texture);

      /* Draw just the texture */
      break;
    case G_DESKTOP_BACKGROUND_STYLE_CENTERED:
    case G_DESKTOP_BACKGROUND_STYLE_SCALED:
      cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                pipeline,
                                                texture_area.x, texture_area.y,
                                                texture_area.x + texture_area.width,
                                                texture_area.y + texture_area.height,
                                                0, 0, 1.0, 1.0);
      bare_region_visible = texture_has_alpha (texture) || memcmp (&texture_area, monitor_area, sizeof (cairo_rectangle_int_t)) != 0;
      break;
    case G_DESKTOP_BACKGROUND_STYLE_NONE:
      bare_region_visible = TRUE;
      break;
    default:
      g_return_val_if_reached(FALSE);
    }

  return bare_region_visible;
}

static void
ensure_color_texture (MetaBackground *self)
{
  if (self->color_texture == NULL)
    {
      ClutterBackend *backend = clutter_get_default_backend ();
      CoglContext *ctx = clutter_backend_get_cogl_context (backend);
      GError *error = NULL;
      uint8_t pixels[6];
      int width, height;

      if (self->shading_direction == G_DESKTOP_BACKGROUND_SHADING_SOLID)
        {
          width = 1;
          height = 1;

          pixels[0] = self->color.red;
          pixels[1] = self->color.green;
          pixels[2] = self->color.blue;
        }
      else
        {
          switch (self->shading_direction)
            {
            case G_DESKTOP_BACKGROUND_SHADING_VERTICAL:
              width = 1;
              height = 2;
              break;
            case G_DESKTOP_BACKGROUND_SHADING_HORIZONTAL:
              width = 2;
              height = 1;
              break;
            default:
              g_return_if_reached ();
            }

          pixels[0] = self->color.red;
          pixels[1] = self->color.green;
          pixels[2] = self->color.blue;
          pixels[3] = self->second_color.red;
          pixels[4] = self->second_color.green;
          pixels[5] = self->second_color.blue;
        }

      self->color_texture = COGL_TEXTURE (cogl_texture_2d_new_from_data (ctx, width, height,
                                                                         COGL_PIXEL_FORMAT_RGB_888,
                                                                         width * 3,
                                                                         pixels,
                                                                         &error));

      if (error != NULL)
        {
          meta_warning ("Failed to allocate color texture: %s\n", error->message);
          g_error_free (error);
        }
    }
}

typedef enum
{
  PIPELINE_REPLACE,
  PIPELINE_ADD,
  PIPELINE_OVER_REVERSE,
} PipelineType;

static CoglPipeline *
create_pipeline (PipelineType type)
{
  const char * const blend_strings[3] = {
    [PIPELINE_REPLACE] = "RGBA = ADD (SRC_COLOR, 0)",
    [PIPELINE_ADD] = "RGBA = ADD (SRC_COLOR, DST_COLOR)",
    [PIPELINE_OVER_REVERSE] = "RGBA = ADD (SRC_COLOR * (1 - DST_COLOR[A]), DST_COLOR)",
  };
  static CoglPipeline *templates[3];

  if (templates[type] == NULL)
    {
      templates[type] = meta_create_texture_pipeline (NULL);
      cogl_pipeline_set_blend (templates[type], blend_strings[type], NULL);
    }

  cogl_pipeline_set_layer_filters (templates[type],
                                   0,
                                   COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);

  return cogl_pipeline_copy (templates[type]);
}

static gboolean
texture_has_alpha (CoglTexture *texture)
{
  if (!texture)
    return FALSE;

  switch (cogl_texture_get_components (texture))
    {
    case COGL_TEXTURE_COMPONENTS_A:
    case COGL_TEXTURE_COMPONENTS_RGBA:
      return TRUE;
    case COGL_TEXTURE_COMPONENTS_RG:
    case COGL_TEXTURE_COMPONENTS_RGB:
    case COGL_TEXTURE_COMPONENTS_DEPTH:
      return FALSE;
    default:
      g_assert_not_reached ();
      return FALSE;
    }
}

static gboolean
ensure_wallpaper_texture (MetaBackground *self,
                          CoglTexture    *texture)
{
  if (self->wallpaper_texture == NULL && !self->wallpaper_allocation_failed)
    {
      int width = cogl_texture_get_width (texture);
      int height = cogl_texture_get_height (texture);
      CoglOffscreen *offscreen;
      CoglFramebuffer *fbo;
      GError *catch_error = NULL;
      CoglPipeline *pipeline;

      self->wallpaper_texture = meta_create_texture (width, height,
                                                     COGL_TEXTURE_COMPONENTS_RGBA,
                                                     META_TEXTURE_FLAGS_NONE);
      offscreen = cogl_offscreen_new_with_texture (self->wallpaper_texture);
      fbo = COGL_FRAMEBUFFER (offscreen);

      if (!cogl_framebuffer_allocate (fbo, &catch_error))
        {
          /* This probably means that the size of the wallpapered texture is larger
           * than the maximum texture size; we treat it as permanent until the
           * background is changed again.
           */
          g_error_free (catch_error);

          cogl_object_unref (self->wallpaper_texture);
          self->wallpaper_texture = NULL;
          cogl_object_unref (fbo);

          self->wallpaper_allocation_failed = TRUE;
          return FALSE;
        }

      cogl_framebuffer_orthographic (fbo, 0, 0,
                                     width, height, -1., 1.);

      pipeline = create_pipeline (PIPELINE_REPLACE);
      cogl_pipeline_set_layer_texture (pipeline, 0, texture);
      cogl_framebuffer_draw_textured_rectangle (fbo, pipeline, 0, 0, width, height,
                                                0., 0., 1., 1.);
      cogl_object_unref (pipeline);

      if (texture_has_alpha (texture))
        {
          ensure_color_texture (self);

          pipeline = create_pipeline (PIPELINE_OVER_REVERSE);
          cogl_pipeline_set_layer_texture (pipeline, 0, self->color_texture);
          cogl_framebuffer_draw_rectangle (fbo, pipeline, 0, 0, width, height);
          cogl_object_unref (pipeline);
        }

      cogl_object_unref (fbo);
    }

  return self->wallpaper_texture != NULL;
}

static CoglPipelineWrapMode
get_wrap_mode (GDesktopBackgroundStyle style)
{
  switch (style)
    {
      case G_DESKTOP_BACKGROUND_STYLE_WALLPAPER:
          return COGL_PIPELINE_WRAP_MODE_REPEAT;
      case G_DESKTOP_BACKGROUND_STYLE_NONE:
      case G_DESKTOP_BACKGROUND_STYLE_STRETCHED:
      case G_DESKTOP_BACKGROUND_STYLE_CENTERED:
      case G_DESKTOP_BACKGROUND_STYLE_SCALED:
      case G_DESKTOP_BACKGROUND_STYLE_ZOOM:
      case G_DESKTOP_BACKGROUND_STYLE_SPANNED:
      default:
          return COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE;
    }
}

static int
get_best_mipmap_level (CoglTexture *texture,
                       int          visible_width,
                       int          visible_height)
{
  int mipmap_width = cogl_texture_get_width (texture);
  int mipmap_height = cogl_texture_get_height (texture);
  int halves = 0;

  while (mipmap_width >= visible_width && mipmap_height >= visible_height)
    {
      halves++;
      mipmap_width /= 2;
      mipmap_height /= 2;
    }

  return MAX (0, halves - 1);
}

CoglTexture *
meta_background_get_texture (MetaBackground         *self,
                             int                     monitor_index,
                             cairo_rectangle_int_t  *texture_area,
                             CoglPipelineWrapMode   *wrap_mode)
{
  MetaBackgroundMonitor *monitor;
  MetaRectangle geometry;
  cairo_rectangle_int_t monitor_area;
  CoglTexture *texture1, *texture2;
  float monitor_scale;

  g_return_val_if_fail (META_IS_BACKGROUND (self), NULL);
  g_return_val_if_fail (monitor_index >= 0 && monitor_index < self->n_monitors, NULL);

  monitor = &self->monitors[monitor_index];

  meta_display_get_monitor_geometry (self->display, monitor_index, &geometry);
  monitor_scale = meta_display_get_monitor_scale (self->display, monitor_index);
  monitor_area.x = geometry.x;
  monitor_area.y = geometry.y;
  monitor_area.width = geometry.width;
  monitor_area.height = geometry.height;

  texture1 = self->background_image1 ? meta_background_image_get_texture (self->background_image1) : NULL;
  texture2 = self->background_image2 ? meta_background_image_get_texture (self->background_image2) : NULL;

  if (texture1 == NULL && texture2 == NULL)
    {
      ensure_color_texture (self);
      if (texture_area)
        set_texture_area_from_monitor_area (&monitor_area, texture_area);
      if (wrap_mode)
        *wrap_mode = COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE;
      return self->color_texture;
    }

  if (texture2 == NULL && self->style == G_DESKTOP_BACKGROUND_STYLE_WALLPAPER &&
      self->shading_direction == G_DESKTOP_BACKGROUND_SHADING_SOLID &&
      ensure_wallpaper_texture (self, texture1))
    {
      if (texture_area)
        get_texture_area (self, &monitor_area, monitor_scale,
                          self->wallpaper_texture, texture_area);
      if (wrap_mode)
        *wrap_mode = COGL_PIPELINE_WRAP_MODE_REPEAT;
      return self->wallpaper_texture;
    }

  if (monitor->dirty)
    {
      GError *catch_error = NULL;
      gboolean bare_region_visible = FALSE;
      int texture_width, texture_height;

      if (meta_is_stage_views_scaled ())
        {
          texture_width = monitor_area.width * monitor_scale;
          texture_height = monitor_area.height * monitor_scale;
        }
      else
        {
          texture_width = monitor_area.width;
          texture_height = monitor_area.height;
        }

      if (monitor->texture == NULL)
        {
          CoglOffscreen *offscreen;

          monitor->texture = meta_create_texture (texture_width,
                                                  texture_height,
                                                  COGL_TEXTURE_COMPONENTS_RGBA,
                                                  META_TEXTURE_FLAGS_NONE);
          offscreen = cogl_offscreen_new_with_texture (monitor->texture);
          monitor->fbo = COGL_FRAMEBUFFER (offscreen);
        }

      if (self->style != G_DESKTOP_BACKGROUND_STYLE_WALLPAPER)
        {
          monitor_area.x *= monitor_scale;
          monitor_area.y *= monitor_scale;
          monitor_area.width *= monitor_scale;
          monitor_area.height *= monitor_scale;
        }

      if (!cogl_framebuffer_allocate (monitor->fbo, &catch_error))
        {
          /* Texture or framebuffer allocation failed; it's unclear why this happened;
           * we'll try again the next time this is called. (MetaBackgroundActor
           * caches the result, so user might be left without a background.)
           */
          cogl_object_unref (monitor->texture);
          monitor->texture = NULL;
          cogl_object_unref (monitor->fbo);
          monitor->fbo = NULL;

          g_error_free (catch_error);
          return NULL;
        }

      cogl_framebuffer_orthographic (monitor->fbo, 0, 0,
                                     monitor_area.width, monitor_area.height, -1., 1.);

      if (texture2 != NULL && self->blend_factor != 0.0)
        {
          CoglPipeline *pipeline = create_pipeline (PIPELINE_REPLACE);
          int mipmap_level;

          mipmap_level = get_best_mipmap_level (texture2,
                                                texture_width,
                                                texture_height);

          cogl_pipeline_set_color4f (pipeline,
                                      self->blend_factor, self->blend_factor, self->blend_factor, self->blend_factor);
          cogl_pipeline_set_layer_texture (pipeline, 0, texture2);
          cogl_pipeline_set_layer_wrap_mode (pipeline, 0, get_wrap_mode (self->style));
          cogl_pipeline_set_layer_max_mipmap_level (pipeline, 0, mipmap_level);

          bare_region_visible = draw_texture (self,
                                              monitor->fbo, pipeline,
                                              texture2, &monitor_area,
                                              monitor_scale);

          cogl_object_unref (pipeline);
        }
      else
        {
          cogl_framebuffer_clear4f (monitor->fbo,
                                    COGL_BUFFER_BIT_COLOR,
                                    0.0, 0.0, 0.0, 0.0);
        }

      if (texture1 != NULL && self->blend_factor != 1.0)
        {
          CoglPipeline *pipeline = create_pipeline (PIPELINE_ADD);
          int mipmap_level;

          mipmap_level = get_best_mipmap_level (texture1,
                                                texture_width,
                                                texture_height);

          cogl_pipeline_set_color4f (pipeline,
                                     (1 - self->blend_factor),
                                     (1 - self->blend_factor),
                                     (1 - self->blend_factor),
                                     (1 - self->blend_factor));;
          cogl_pipeline_set_layer_texture (pipeline, 0, texture1);
          cogl_pipeline_set_layer_wrap_mode (pipeline, 0, get_wrap_mode (self->style));
          cogl_pipeline_set_layer_max_mipmap_level (pipeline, 0, mipmap_level);

          bare_region_visible = bare_region_visible || draw_texture (self,
                                                                     monitor->fbo, pipeline,
                                                                     texture1, &monitor_area,
                                                                     monitor_scale);

          cogl_object_unref (pipeline);
        }

      if (bare_region_visible)
        {
          CoglPipeline *pipeline = create_pipeline (PIPELINE_OVER_REVERSE);

          ensure_color_texture (self);
          cogl_pipeline_set_layer_texture (pipeline, 0, self->color_texture);
          cogl_framebuffer_draw_rectangle (monitor->fbo,
                                           pipeline,
                                           0, 0,
                                           monitor_area.width, monitor_area.height);
          cogl_object_unref (pipeline);
        }

      monitor->dirty = FALSE;
    }

  if (texture_area)
    set_texture_area_from_monitor_area (&geometry, texture_area);

  if (wrap_mode)
    *wrap_mode = COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE;
  return monitor->texture;
}

MetaBackground *
meta_background_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_BACKGROUND,
                       "meta-display", display,
                       NULL);
}

void
meta_background_set_color (MetaBackground *self,
                           ClutterColor   *color)
{
  ClutterColor dummy = { 0 };

  g_return_if_fail (META_IS_BACKGROUND (self));
  g_return_if_fail (color != NULL);

  meta_background_set_gradient (self,
                                G_DESKTOP_BACKGROUND_SHADING_SOLID,
                                color, &dummy);
}

void
meta_background_set_gradient (MetaBackground            *self,
                              GDesktopBackgroundShading  shading_direction,
                              ClutterColor              *color,
                              ClutterColor              *second_color)
{
  g_return_if_fail (META_IS_BACKGROUND (self));
  g_return_if_fail (color != NULL);
  g_return_if_fail (second_color != NULL);

  self->shading_direction = shading_direction;
  self->color = *color;
  self->second_color = *second_color;

  free_color_texture (self);
  free_wallpaper_texture (self);
  mark_changed (self);
}

/**
 * meta_background_set_file:
 * @self: a #MetaBackground
 * @file: (nullable): a #GFile representing the background file
 * @style: the background style to apply
 *
 * Set the background to @file
 */
void
meta_background_set_file (MetaBackground            *self,
                          GFile                     *file,
                          GDesktopBackgroundStyle    style)
{
  g_return_if_fail (META_IS_BACKGROUND (self));

  meta_background_set_blend (self, file, NULL, 0.0, style);
}

void
meta_background_set_blend (MetaBackground          *self,
                           GFile                   *file1,
                           GFile                   *file2,
                           double                   blend_factor,
                           GDesktopBackgroundStyle  style)
{
  g_return_if_fail (META_IS_BACKGROUND (self));
  g_return_if_fail (blend_factor >= 0.0 && blend_factor <= 1.0);

  set_file (self, &self->file1, &self->background_image1, file1, FALSE);
  set_file (self, &self->file2, &self->background_image2, file2, FALSE);

  self->blend_factor = blend_factor;
  self->style = style;

  free_wallpaper_texture (self);
  mark_changed (self);
}

void
meta_background_refresh_all (void)
{
  GSList *l;

  for (l = all_backgrounds; l; l = l->next)
    mark_changed (l->data);
}
