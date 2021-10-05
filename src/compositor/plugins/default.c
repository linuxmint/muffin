/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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

#define DESTROY_TIMEOUT   100
#define MINIMIZE_TIMEOUT  250
#define MAP_TIMEOUT       250
#define SWITCH_TIMEOUT    500

#define ACTOR_DATA_KEY "MCCP-Default-actor-data"
#define DISPLAY_TILE_PREVIEW_DATA_KEY "MCCP-Default-display-tile-preview-data"

#define META_TYPE_DEFAULT_PLUGIN            (meta_default_plugin_get_type ())
#define META_DEFAULT_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEFAULT_PLUGIN, MetaDefaultPlugin))
#define META_DEFAULT_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))
#define META_IS_DEFAULT_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_DEFAULT_PLUGIN_TYPE))
#define META_IS_DEFAULT_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEFAULT_PLUGIN))
#define META_DEFAULT_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))

typedef struct _MetaDefaultPlugin        MetaDefaultPlugin;
typedef struct _MetaDefaultPluginClass   MetaDefaultPluginClass;
typedef struct _MetaDefaultPluginPrivate MetaDefaultPluginPrivate;

struct _MetaDefaultPlugin
{
  MetaPlugin parent;

  MetaDefaultPluginPrivate *priv;
};

struct _MetaDefaultPluginClass
{
  MetaPluginClass parent_class;
};

static GQuark actor_data_quark = 0;
static GQuark display_tile_preview_data_quark = 0;

static void start      (MetaPlugin      *plugin);
static void minimize   (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void map        (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void destroy    (MetaPlugin      *plugin,
                        MetaWindowActor *actor);

static void switch_workspace (MetaPlugin          *plugin,
                              gint                 from,
                              gint                 to,
                              MetaMotionDirection  direction);

static void kill_window_effects   (MetaPlugin      *plugin,
                                   MetaWindowActor *actor);
static void kill_switch_workspace (MetaPlugin      *plugin);

static void show_tile_preview (MetaPlugin      *plugin,
                               MetaWindow      *window,
                               MetaRectangle   *tile_rect,
                               int              tile_monitor_number);
static void hide_tile_preview (MetaPlugin      *plugin);

static void confirm_display_change (MetaPlugin *plugin);

static const MetaPluginInfo * plugin_info (MetaPlugin *plugin);

/*
 * Plugin private data that we store in the .plugin_private member.
 */
struct _MetaDefaultPluginPrivate
{
  /* Valid only when switch_workspace effect is in progress */
  ClutterTimeline       *tml_switch_workspace1;
  ClutterTimeline       *tml_switch_workspace2;
  ClutterActor          *desktop1;
  ClutterActor          *desktop2;

  ClutterActor          *background_group;

  MetaPluginInfo         info;
};

META_PLUGIN_DECLARE_WITH_CODE (MetaDefaultPlugin, meta_default_plugin,
                               G_ADD_PRIVATE_DYNAMIC (MetaDefaultPlugin));

/*
 * Per actor private data we attach to each actor.
 */
typedef struct _ActorPrivate
{
  ClutterActor *orig_parent;

  ClutterTimeline *tml_minimize;
  ClutterTimeline *tml_destroy;
  ClutterTimeline *tml_map;
} ActorPrivate;

/* callback data for when animations complete */
typedef struct
{
  ClutterActor *actor;
  MetaPlugin *plugin;
} EffectCompleteData;


typedef struct _DisplayTilePreview
{
  ClutterActor   *actor;

  GdkRGBA        *preview_color;

  MetaRectangle   tile_rect;
} DisplayTilePreview;

static void
meta_default_plugin_dispose (GObject *object)
{
  /* MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (object)->priv;
  */
  G_OBJECT_CLASS (meta_default_plugin_parent_class)->dispose (object);
}

static void
meta_default_plugin_finalize (GObject *object)
{
  G_OBJECT_CLASS (meta_default_plugin_parent_class)->finalize (object);
}

static void
meta_default_plugin_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_default_plugin_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_default_plugin_class_init (MetaDefaultPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  MetaPluginClass *plugin_class  = META_PLUGIN_CLASS (klass);

  gobject_class->finalize        = meta_default_plugin_finalize;
  gobject_class->dispose         = meta_default_plugin_dispose;
  gobject_class->set_property    = meta_default_plugin_set_property;
  gobject_class->get_property    = meta_default_plugin_get_property;

  plugin_class->start            = start;
  plugin_class->map              = map;
  plugin_class->minimize         = minimize;
  plugin_class->destroy          = destroy;
  plugin_class->switch_workspace = switch_workspace;
  plugin_class->show_tile_preview = show_tile_preview;
  plugin_class->hide_tile_preview = hide_tile_preview;
  plugin_class->plugin_info      = plugin_info;
  plugin_class->kill_window_effects   = kill_window_effects;
  plugin_class->kill_switch_workspace = kill_switch_workspace;
  plugin_class->confirm_display_change = confirm_display_change;
}

static void
meta_default_plugin_init (MetaDefaultPlugin *self)
{
  MetaDefaultPluginPrivate *priv;

  self->priv = priv = meta_default_plugin_get_instance_private (self);

  priv->info.name        = "Default Effects";
  priv->info.version     = "0.1";
  priv->info.author      = "Intel Corp.";
  priv->info.license     = "GPL";
  priv->info.description = "This is an example of a plugin implementation.";
}

/*
 * Actor private data accessor
 */
static void
free_actor_private (gpointer data)
{
  if (G_LIKELY (data != NULL))
    g_slice_free (ActorPrivate, data);
}

static ActorPrivate *
get_actor_private (MetaWindowActor *actor)
{
  ActorPrivate *priv = g_object_get_qdata (G_OBJECT (actor), actor_data_quark);

  if (G_UNLIKELY (actor_data_quark == 0))
    actor_data_quark = g_quark_from_static_string (ACTOR_DATA_KEY);

  if (G_UNLIKELY (!priv))
    {
      priv = g_slice_new0 (ActorPrivate);

      g_object_set_qdata_full (G_OBJECT (actor),
                               actor_data_quark, priv,
                               free_actor_private);
    }

  return priv;
}

static ClutterTimeline *
actor_animate (ClutterActor         *actor,
               ClutterAnimationMode  mode,
               guint                 duration,
               const gchar          *first_property,
               ...)
{
  va_list args;
  ClutterTransition *transition;

  clutter_actor_save_easing_state (actor);
  clutter_actor_set_easing_mode (actor, mode);
  clutter_actor_set_easing_duration (actor, duration);

  va_start (args, first_property);
  g_object_set_valist (G_OBJECT (actor), first_property, args);
  va_end (args);

  transition = clutter_actor_get_transition (actor, first_property);

  clutter_actor_restore_easing_state (actor);

  return CLUTTER_TIMELINE (transition);
}

static void
on_switch_workspace_effect_complete (ClutterTimeline *timeline, gpointer data)
{
  MetaPlugin               *plugin  = META_PLUGIN (data);
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;
  MetaDisplay *display = meta_plugin_get_display (plugin);
  GList *l = meta_get_window_actors (display);

  while (l)
    {
      ClutterActor *a = l->data;
      MetaWindowActor *window_actor = META_WINDOW_ACTOR (a);
      ActorPrivate *apriv = get_actor_private (window_actor);

      if (apriv->orig_parent)
        {
          g_object_ref (a);
          clutter_actor_remove_child (clutter_actor_get_parent (a), a);
          clutter_actor_add_child (apriv->orig_parent, a);
          g_object_unref (a);
          apriv->orig_parent = NULL;
        }

      l = l->next;
    }

  clutter_actor_destroy (priv->desktop1);
  clutter_actor_destroy (priv->desktop2);

  priv->tml_switch_workspace1 = NULL;
  priv->tml_switch_workspace2 = NULL;
  priv->desktop1 = NULL;
  priv->desktop2 = NULL;

  meta_plugin_switch_workspace_completed (plugin);
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaPlugin         *plugin)
{
  MetaDefaultPlugin *self = META_DEFAULT_PLUGIN (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);

  int i, n;
  GRand *rand = g_rand_new_with_seed (123456);

  clutter_actor_destroy_all_children (self->priv->background_group);

  n = meta_display_get_n_monitors (display);
  for (i = 0; i < n; i++)
    {
      MetaRectangle rect;
      ClutterActor *background_actor;
      MetaBackground *background;
      ClutterColor color;

      meta_display_get_monitor_geometry (display, i, &rect);

      background_actor = meta_background_actor_new (display, i);

      clutter_actor_set_position (background_actor, rect.x, rect.y);
      clutter_actor_set_size (background_actor, rect.width, rect.height);

      /* Don't use rand() here, mesa calls srand() internally when
         parsing the driconf XML, but it's nice if the colors are
         reproducible.
      */
      clutter_color_init (&color,
                          g_rand_int_range (rand, 0, 255),
                          g_rand_int_range (rand, 0, 255),
                          g_rand_int_range (rand, 0, 255),
                          255);

      background = meta_background_new (display);
      meta_background_set_color (background, &color);
      meta_background_actor_set_background (META_BACKGROUND_ACTOR (background_actor), background);
      g_object_unref (background);

      meta_background_actor_set_vignette (META_BACKGROUND_ACTOR (background_actor),
                                          TRUE,
                                          0.5,
                                          0.5);

      clutter_actor_add_child (self->priv->background_group, background_actor);
    }

  g_rand_free (rand);
}

static void
init_keymap (MetaDefaultPlugin *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GVariant) props = NULL;
  g_autofree char *x11_layout = NULL;
  g_autofree char *x11_options = NULL;
  g_autofree char *x11_variant = NULL;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.locale1",
                                         "/org/freedesktop/locale1",
                                         "org.freedesktop.DBus.Properties",
                                         NULL,
                                         &error);
  if (!proxy)
    {
      g_message ("Failed to acquire org.freedesktop.locale1 proxy: %s, "
                 "probably running in CI",
                 error->message);
      return;
    }

  result = g_dbus_proxy_call_sync (proxy,
                                   "GetAll",
                                   g_variant_new ("(s)",
                                                  "org.freedesktop.locale1"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   100,
                                   NULL,
                                   &error);
  if (!result)
    {
      g_warning ("Failed to retrieve locale properties: %s", error->message);
      return;
    }

  props = g_variant_get_child_value (result, 0);
  if (!props)
    {
      g_warning ("No locale properties found");
      return;
    }

  if (!g_variant_lookup (props, "X11Layout", "s", &x11_layout))
    x11_layout = g_strdup ("us");

  if (!g_variant_lookup (props, "X11Options", "s", &x11_options))
    x11_options = g_strdup ("");

  if (!g_variant_lookup (props, "X11Variant", "s", &x11_variant))
    x11_variant = g_strdup ("");

  meta_backend_set_keymap (meta_get_backend (),
                           x11_layout, x11_variant, x11_options);
}

static void
start (MetaPlugin *plugin)
{
  MetaDefaultPlugin *self = META_DEFAULT_PLUGIN (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);
  MetaMonitorManager *monitor_manager = meta_monitor_manager_get ();

  self->priv->background_group = meta_background_group_new ();
  clutter_actor_insert_child_below (meta_get_window_group_for_display (display),
                                    self->priv->background_group, NULL);

  g_signal_connect (monitor_manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), plugin);

  on_monitors_changed (monitor_manager, plugin);

  if (meta_is_wayland_compositor ())
    init_keymap (self);

  clutter_actor_show (meta_get_stage_for_display (display));
}

static void
switch_workspace (MetaPlugin *plugin,
                  gint from, gint to,
                  MetaMotionDirection direction)
{
  MetaDisplay *display;
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;
  GList        *l;
  ClutterActor *workspace0  = clutter_actor_new ();
  ClutterActor *workspace1  = clutter_actor_new ();
  ClutterActor *stage;
  int           screen_width, screen_height;

  display = meta_plugin_get_display (plugin);
  stage = meta_get_stage_for_display (display);

  meta_display_get_size (display,
                         &screen_width,
                         &screen_height);

  clutter_actor_set_pivot_point (workspace1, 1.0, 1.0);
  clutter_actor_set_position (workspace1,
                              screen_width,
                              screen_height);

  clutter_actor_set_scale (workspace1, 0.0, 0.0);

  clutter_actor_add_child (stage, workspace1);
  clutter_actor_add_child (stage, workspace0);

  if (from == to)
    {
      meta_plugin_switch_workspace_completed (plugin);
      return;
    }

  l = g_list_last (meta_get_window_actors (display));

  while (l)
    {
      MetaWindowActor *window_actor = l->data;
      ActorPrivate    *apriv	    = get_actor_private (window_actor);
      ClutterActor    *actor	    = CLUTTER_ACTOR (window_actor);
      MetaWorkspace   *workspace;
      gint             win_workspace;

      workspace = meta_window_get_workspace (meta_window_actor_get_meta_window (window_actor));
      win_workspace = meta_workspace_index (workspace);

      if (win_workspace == to || win_workspace == from)
        {
          ClutterActor *parent = win_workspace == to ? workspace1 : workspace0;
          apriv->orig_parent = clutter_actor_get_parent (actor);

          g_object_ref (actor);
          clutter_actor_remove_child (clutter_actor_get_parent (actor), actor);
          clutter_actor_add_child (parent, actor);
          clutter_actor_show (actor);
          clutter_actor_set_child_below_sibling (parent, actor, NULL);
          g_object_unref (actor);
        }
      else if (win_workspace < 0)
        {
          /* Sticky window */
          apriv->orig_parent = NULL;
        }
      else
        {
          /* Window on some other desktop */
          clutter_actor_hide (actor);
          apriv->orig_parent = NULL;
        }

      l = l->prev;
    }

  priv->desktop1 = workspace0;
  priv->desktop2 = workspace1;

  priv->tml_switch_workspace1 = actor_animate (workspace0, CLUTTER_EASE_IN_SINE,
                                               SWITCH_TIMEOUT,
                                               "scale-x", 1.0,
                                               "scale-y", 1.0,
                                               NULL);
  g_signal_connect (priv->tml_switch_workspace1,
                    "completed",
                    G_CALLBACK (on_switch_workspace_effect_complete),
                    plugin);

  priv->tml_switch_workspace2 = actor_animate (workspace1, CLUTTER_EASE_IN_SINE,
                                               SWITCH_TIMEOUT,
                                               "scale-x", 0.0,
                                               "scale-y", 0.0,
                                               NULL);
}


/*
 * Minimize effect completion callback; this function restores actor state, and
 * calls the manager callback function.
 */
static void
on_minimize_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect; must hide it first to ensure
   * that the restoration will not be visible.
   */
  MetaPlugin *plugin = data->plugin;
  ActorPrivate *apriv;
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);

  apriv = get_actor_private (META_WINDOW_ACTOR (data->actor));
  apriv->tml_minimize = NULL;

  clutter_actor_hide (data->actor);

  /* FIXME - we shouldn't assume the original scale, it should be saved
   * at the start of the effect */
  clutter_actor_set_scale (data->actor, 1.0, 1.0);

  /* Now notify the manager that we are done with this effect */
  meta_plugin_minimize_completed (plugin, window_actor);

  g_free (data);
}

/*
 * Simple minimize handler: it applies a scale effect (which must be reversed on
 * completion).
 */
static void
minimize (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  MetaRectangle icon_geometry;
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);
  ClutterTimeline *timeline = NULL;
  ClutterActor *actor  = CLUTTER_ACTOR (window_actor);


  type = meta_window_get_window_type (meta_window);

  if (!meta_window_get_icon_geometry(meta_window, &icon_geometry))
    {
      icon_geometry.x = 0;
      icon_geometry.y = 0;
    }

  if (type == META_WINDOW_NORMAL)
    {
      timeline = actor_animate (actor,
                                CLUTTER_EASE_IN_SINE,
                                MINIMIZE_TIMEOUT,
                                "scale-x", 0.0,
                                "scale-y", 0.0,
                                "x", (double)icon_geometry.x,
                                "y", (double)icon_geometry.y,
                                NULL);
    }

  if (timeline)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      apriv->tml_minimize = timeline;
      data->plugin = plugin;
      data->actor = actor;
      g_signal_connect (apriv->tml_minimize, "completed",
                        G_CALLBACK (on_minimize_effect_complete),
                        data);
    }
  else
    meta_plugin_minimize_completed (plugin, window_actor);
}

static void
on_map_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  /*
   * Must reverse the effect of the effect.
   */
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor  *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate  *apriv = get_actor_private (window_actor);

  apriv->tml_map = NULL;

  /* Now notify the manager that we are done with this effect */
  meta_plugin_map_completed (plugin, window_actor);

  g_free (data);
}

/*
 * Simple map handler: it applies a scale effect which must be reversed on
 * completion).
 */
static void
map (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      clutter_actor_set_pivot_point (actor, 0.5, 0.5);
      clutter_actor_set_opacity (actor, 0);
      clutter_actor_set_scale (actor, 0.5, 0.5);
      clutter_actor_show (actor);

      apriv->tml_map = actor_animate (actor,
                                      CLUTTER_EASE_OUT_QUAD,
                                      MAP_TIMEOUT,
                                      "opacity", 255,
                                      "scale-x", 1.0,
                                      "scale-y", 1.0,
                                      NULL);
      data->actor = actor;
      data->plugin = plugin;
      g_signal_connect (apriv->tml_map, "completed",
                        G_CALLBACK (on_map_effect_complete),
                        data);
    }
  else
    meta_plugin_map_completed (plugin, window_actor);
}

/*
 * Destroy effect completion callback; this is a simple effect that requires no
 * further action than notifying the manager that the effect is completed.
 */
static void
on_destroy_effect_complete (ClutterTimeline *timeline, EffectCompleteData *data)
{
  MetaPlugin *plugin = data->plugin;
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (data->actor);
  ActorPrivate *apriv = get_actor_private (window_actor);

  apriv->tml_destroy = NULL;

  meta_plugin_destroy_completed (plugin, window_actor);
}

/*
 * Simple TV-out like effect.
 */
static void
destroy (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);
  ClutterTimeline *timeline = NULL;

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      timeline = actor_animate (actor,
                                CLUTTER_EASE_OUT_QUAD,
                                DESTROY_TIMEOUT,
                                "opacity", 0,
                                "scale-x", 0.8,
                                "scale-y", 0.8,
                                NULL);
    }

  if (timeline)
    {
      EffectCompleteData *data = g_new0 (EffectCompleteData, 1);
      ActorPrivate *apriv = get_actor_private (window_actor);

      apriv->tml_destroy = timeline;
      data->plugin = plugin;
      data->actor = actor;
      g_signal_connect (apriv->tml_destroy, "completed",
                        G_CALLBACK (on_destroy_effect_complete),
                        data);
    }
  else
    meta_plugin_destroy_completed (plugin, window_actor);
}

/*
 * Tile preview private data accessor
 */
static void
free_display_tile_preview (DisplayTilePreview *preview)
{

  if (G_LIKELY (preview != NULL)) {
    clutter_actor_destroy (preview->actor);
    g_slice_free (DisplayTilePreview, preview);
  }
}

static void
on_display_closing (MetaDisplay        *display,
                    DisplayTilePreview *preview)
{
  free_display_tile_preview (preview);
}

static DisplayTilePreview *
get_display_tile_preview (MetaDisplay *display)
{
  DisplayTilePreview *preview;

  if (!display_tile_preview_data_quark)
    {
      display_tile_preview_data_quark =
        g_quark_from_static_string (DISPLAY_TILE_PREVIEW_DATA_KEY);
    }

  preview = g_object_get_qdata (G_OBJECT (display),
                                display_tile_preview_data_quark);
  if (!preview)
    {
      preview = g_slice_new0 (DisplayTilePreview);

      preview->actor = clutter_actor_new ();
      clutter_actor_set_background_color (preview->actor, CLUTTER_COLOR_Blue);
      clutter_actor_set_opacity (preview->actor, 100);

      clutter_actor_add_child (meta_get_window_group_for_display (display), preview->actor);
      g_signal_connect (display,
                        "closing",
                        G_CALLBACK (on_display_closing),
                        preview);
      g_object_set_qdata (G_OBJECT (display),
                          display_tile_preview_data_quark,
                          preview);
    }

  return preview;
}

static void
show_tile_preview (MetaPlugin    *plugin,
                   MetaWindow    *window,
                   MetaRectangle *tile_rect,
                   int            tile_monitor_number)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  DisplayTilePreview *preview = get_display_tile_preview (display);
  ClutterActor *window_actor;

  if (clutter_actor_is_visible (preview->actor)
      && preview->tile_rect.x == tile_rect->x
      && preview->tile_rect.y == tile_rect->y
      && preview->tile_rect.width == tile_rect->width
      && preview->tile_rect.height == tile_rect->height)
    return; /* nothing to do */

  clutter_actor_set_position (preview->actor, tile_rect->x, tile_rect->y);
  clutter_actor_set_size (preview->actor, tile_rect->width, tile_rect->height);

  clutter_actor_show (preview->actor);

  window_actor = CLUTTER_ACTOR (meta_window_get_compositor_private (window));
  clutter_actor_set_child_below_sibling (clutter_actor_get_parent (preview->actor),
                                         preview->actor,
                                         window_actor);

  preview->tile_rect = *tile_rect;
}

static void
hide_tile_preview (MetaPlugin *plugin)
{
  MetaDisplay *display = meta_plugin_get_display (plugin);
  DisplayTilePreview *preview = get_display_tile_preview (display);

  clutter_actor_hide (preview->actor);
}

static void
finish_timeline (ClutterTimeline *timeline)
{
  g_object_ref (timeline);
  clutter_timeline_stop (timeline);
  g_signal_emit_by_name (timeline, "completed", NULL);
  g_object_unref (timeline);
}

static void
kill_switch_workspace (MetaPlugin     *plugin)
{
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;

  if (priv->tml_switch_workspace1)
    {
      g_object_ref (priv->tml_switch_workspace1);
      clutter_timeline_stop (priv->tml_switch_workspace1);
      clutter_timeline_stop (priv->tml_switch_workspace2);
      g_signal_emit_by_name (priv->tml_switch_workspace1, "completed", NULL);
      g_object_unref (priv->tml_switch_workspace1);
    }
}

static void
kill_window_effects (MetaPlugin      *plugin,
                     MetaWindowActor *window_actor)
{
  ActorPrivate *apriv;

  apriv = get_actor_private (window_actor);

  if (apriv->tml_minimize)
    finish_timeline (apriv->tml_minimize);

  if (apriv->tml_map)
    finish_timeline (apriv->tml_map);

  if (apriv->tml_destroy)
    finish_timeline (apriv->tml_destroy);
}

static const MetaPluginInfo *
plugin_info (MetaPlugin *plugin)
{
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;

  return &priv->info;
}

static void
on_dialog_closed (GPid     pid,
                  gint     status,
                  gpointer user_data)
{
  MetaPlugin *plugin = user_data;
  gboolean ok;

  ok = g_spawn_check_exit_status (status, NULL);
  meta_plugin_complete_display_change (plugin, ok);
}

static void
confirm_display_change (MetaPlugin *plugin)
{
  GPid pid;

  pid = meta_show_dialog ("--question",
                          "Does the display look OK?",
                          "20",
                          NULL,
                          "_Keep This Configuration",
                          "_Restore Previous Configuration",
                          "preferences-desktop-display",
                          0,
                          NULL, NULL);

  g_child_watch_add (pid, on_dialog_closed, plugin);
}
