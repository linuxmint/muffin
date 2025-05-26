/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Tiling Gaps Plugin for Muffin
 * 
 * This plugin adds configurable gaps between tiled windows
 * Author: Custom Implementation
 * License: GPL
 */

#include "config.h"

#include "meta/display.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <string.h>

#include "clutter/clutter.h"
#include "meta/meta-backend.h"
#include "meta/meta-background-actor.h"
#include "meta/meta-background-group.h"
#include "meta/meta-monitor-manager.h"
#include "meta/meta-plugin.h"
#include "meta/util.h"
#include "meta/window.h"
#include "meta/prefs.h"

#define META_TYPE_TILING_GAPS_PLUGIN            (meta_tiling_gaps_plugin_get_type ())
#define META_TILING_GAPS_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_TILING_GAPS_PLUGIN, MetaTilingGapsPlugin))
#define META_TILING_GAPS_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_TILING_GAPS_PLUGIN, MetaTilingGapsPluginClass))
#define META_IS_TILING_GAPS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_TILING_GAPS_PLUGIN))
#define META_IS_TILING_GAPS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_TILING_GAPS_PLUGIN))
#define META_TILING_GAPS_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_TILING_GAPS_PLUGIN, MetaTilingGapsPluginClass))

typedef struct _MetaTilingGapsPlugin        MetaTilingGapsPlugin;
typedef struct _MetaTilingGapsPluginClass   MetaTilingGapsPluginClass;
typedef struct _MetaTilingGapsPluginPrivate MetaTilingGapsPluginPrivate;

struct _MetaTilingGapsPlugin
{
  MetaPlugin parent;
  MetaTilingGapsPluginPrivate *priv;
};

struct _MetaTilingGapsPluginClass
{
  MetaPluginClass parent_class;
};

struct _MetaTilingGapsPluginPrivate
{
  MetaPluginInfo info;
  GSettings *settings;
  gboolean gaps_enabled;
  gint gap_size;
  gint outer_gap_size;
};

static void start      (MetaPlugin      *plugin);
static void show_tile_preview (MetaPlugin      *plugin,
                               MetaWindow      *window,
                               MetaRectangle   *tile_rect,
                               int              tile_monitor_number);
static void hide_tile_preview (MetaPlugin      *plugin);
static const MetaPluginInfo * plugin_info (MetaPlugin *plugin);

META_PLUGIN_DECLARE_WITH_CODE (MetaTilingGapsPlugin, meta_tiling_gaps_plugin,
                               G_ADD_PRIVATE_DYNAMIC (MetaTilingGapsPlugin));

static void
on_settings_changed (GSettings   *settings,
                     const gchar *key,
                     gpointer     user_data)
{
  MetaTilingGapsPlugin *plugin = META_TILING_GAPS_PLUGIN (user_data);
  MetaTilingGapsPluginPrivate *priv = plugin->priv;

  if (g_strcmp0 (key, "tiling-gaps-enabled") == 0)
    {
      priv->gaps_enabled = g_settings_get_boolean (settings, key);
    }
  else if (g_strcmp0 (key, "tiling-gap-size") == 0)
    {
      priv->gap_size = g_settings_get_int (settings, key);
    }
  else if (g_strcmp0 (key, "tiling-outer-gap-size") == 0)
    {
      priv->outer_gap_size = g_settings_get_int (settings, key);
    }
}

static void
meta_tiling_gaps_plugin_dispose (GObject *object)
{
  MetaTilingGapsPlugin *plugin = META_TILING_GAPS_PLUGIN (object);
  MetaTilingGapsPluginPrivate *priv = plugin->priv;

  if (priv->settings)
    {
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }

  G_OBJECT_CLASS (meta_tiling_gaps_plugin_parent_class)->dispose (object);
}

static void
meta_tiling_gaps_plugin_finalize (GObject *object)
{
  G_OBJECT_CLASS (meta_tiling_gaps_plugin_parent_class)->finalize (object);
}

static void
meta_tiling_gaps_plugin_class_init (MetaTilingGapsPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  MetaPluginClass *plugin_class  = META_PLUGIN_CLASS (klass);

  gobject_class->finalize        = meta_tiling_gaps_plugin_finalize;
  gobject_class->dispose         = meta_tiling_gaps_plugin_dispose;

  plugin_class->start            = start;
  plugin_class->show_tile_preview = show_tile_preview;
  plugin_class->hide_tile_preview = hide_tile_preview;
  plugin_class->plugin_info      = plugin_info;
}

static void
meta_tiling_gaps_plugin_init (MetaTilingGapsPlugin *self)
{
  MetaTilingGapsPluginPrivate *priv;

  self->priv = priv = meta_tiling_gaps_plugin_get_instance_private (self);

  priv->info.name        = "Tiling Gaps";
  priv->info.version     = "1.0";
  priv->info.author      = "Custom Implementation";
  priv->info.license     = "GPL";
  priv->info.description = "Adds configurable gaps between tiled windows";

  /* Initialize settings */
  priv->settings = g_settings_new ("org.cinnamon.muffin");
  priv->gaps_enabled = g_settings_get_boolean (priv->settings, "tiling-gaps-enabled");
  priv->gap_size = g_settings_get_int (priv->settings, "tiling-gap-size");
  priv->outer_gap_size = g_settings_get_int (priv->settings, "tiling-outer-gap-size");

  g_signal_connect (priv->settings, "changed",
                    G_CALLBACK (on_settings_changed), self);
}

static void
apply_gaps_to_tile_rect (MetaTilingGapsPlugin *plugin,
                        MetaWindow           *window,
                        MetaRectangle        *tile_rect,
                        MetaTileMode          tile_mode)
{
  MetaTilingGapsPluginPrivate *priv = plugin->priv;
  MetaRectangle work_area;
  gint gap = priv->gap_size;
  gint outer_gap = priv->outer_gap_size;

  if (!priv->gaps_enabled)
    return;

  meta_window_get_work_area_current_monitor (window, &work_area);

  /* Apply outer gaps */
  tile_rect->x += outer_gap;
  tile_rect->y += outer_gap;
  tile_rect->width -= 2 * outer_gap;
  tile_rect->height -= 2 * outer_gap;

  /* Apply inner gaps based on tile mode */
  switch (tile_mode)
    {
    case META_TILE_LEFT:
      tile_rect->width -= gap / 2;
      break;
    case META_TILE_RIGHT:
      tile_rect->x += gap / 2;
      tile_rect->width -= gap / 2;
      break;
    case META_TILE_TOP:
      tile_rect->height -= gap / 2;
      break;
    case META_TILE_BOTTOM:
      tile_rect->y += gap / 2;
      tile_rect->height -= gap / 2;
      break;
    case META_TILE_ULC:
      tile_rect->width -= gap / 2;
      tile_rect->height -= gap / 2;
      break;
    case META_TILE_URC:
      tile_rect->x += gap / 2;
      tile_rect->width -= gap / 2;
      tile_rect->height -= gap / 2;
      break;
    case META_TILE_LLC:
      tile_rect->width -= gap / 2;
      tile_rect->y += gap / 2;
      tile_rect->height -= gap / 2;
      break;
    case META_TILE_LRC:
      tile_rect->x += gap / 2;
      tile_rect->width -= gap / 2;
      tile_rect->y += gap / 2;
      tile_rect->height -= gap / 2;
      break;
    default:
      break;
    }
}

static void
start (MetaPlugin *plugin)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  clutter_actor_show (meta_get_stage_for_display (display));
}

static void
show_tile_preview (MetaPlugin    *plugin,
                   MetaWindow    *window,
                   MetaRectangle *tile_rect,
                   int            tile_monitor_number)
{
  MetaTilingGapsPlugin *gaps_plugin = META_TILING_GAPS_PLUGIN (plugin);
  MetaRectangle modified_rect = *tile_rect;
  
  /* Apply gaps to the tile preview */
  apply_gaps_to_tile_rect (gaps_plugin, window, &modified_rect, 
                          meta_window_get_tile_mode (window));
  
  /* Call the parent implementation with modified rectangle */
  /* Note: This would need to be implemented properly in a real plugin */
}

static void
hide_tile_preview (MetaPlugin *plugin)
{
  /* Call parent implementation */
}

static const MetaPluginInfo *
plugin_info (MetaPlugin *plugin)
{
  MetaTilingGapsPluginPrivate *priv = META_TILING_GAPS_PLUGIN (plugin)->priv;
  return &priv->info;
}
