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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef META_PLUGIN_H_
#define META_PLUGIN_H_

#include <meta/types.h>
#include <meta/compositor.h>
#include <meta/compositor-muffin.h>

#include <clutter/clutter.h>
#include <X11/extensions/Xfixes.h>
#include <gmodule.h>

#define META_TYPE_PLUGIN            (meta_plugin_get_type ())
#define META_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_PLUGIN, MetaPlugin))
#define META_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_PLUGIN, MetaPluginClass))
#define META_IS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_PLUGIN))
#define META_IS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_PLUGIN))
#define META_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_PLUGIN, MetaPluginClass))

/**
 * MetaPlugin: (skip)
 *
 */
typedef struct _MetaPlugin        MetaPlugin;
/**
 * MetaPluginClass: (skip)
 *
 */
typedef struct _MetaPluginClass   MetaPluginClass;
typedef struct _MetaPluginVersion MetaPluginVersion;
typedef struct _MetaPluginInfo    MetaPluginInfo;
typedef struct _MetaPluginPrivate MetaPluginPrivate;

struct _MetaPlugin
{
  GObject parent;

  MetaPluginPrivate *priv;
};

struct _MetaPluginClass
{
  GObjectClass parent_class;

  void (*start)            (MetaPlugin         *plugin);

  void (*minimize)         (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  void (*maximize)         (MetaPlugin         *plugin,
                            MetaWindowActor    *actor,
                            gint                x,
                            gint                y,
                            gint                width,
                            gint                height);

  void (*unmaximize)       (MetaPlugin         *plugin,
                            MetaWindowActor    *actor,
                            gint                x,
                            gint                y,
                            gint                width,
                            gint                height);

  void (*tile)             (MetaPlugin         *plugin,
                            MetaWindowActor    *actor,
                            gint                x,
                            gint                y,
                            gint                width,
                            gint                height);

  void (*map)              (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  void (*destroy)          (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  void (*switch_workspace) (MetaPlugin         *plugin,
                            gint                from,
                            gint                to,
                            MetaMotionDirection direction);

  /*
   * Called if an effects should be killed prematurely; the plugin must
   * call the completed() callback as if the effect terminated naturally.
   */
  void (*kill_window_effects)      (MetaPlugin      *plugin,
                                    MetaWindowActor *actor);

  void (*kill_switch_workspace)    (MetaPlugin     *plugin);

  /* General XEvent filter. This is fired *before* meta itself handles
   * an event. Return TRUE to block any further processing.
   */
  gboolean (*xevent_filter) (MetaPlugin       *plugin,
                             XEvent           *event);

  const MetaPluginInfo * (*plugin_info) (MetaPlugin *plugin);
};

struct _MetaPluginInfo
{
  const gchar *name;
  const gchar *version;
  const gchar *author;
  const gchar *license;
  const gchar *description;
};

GType meta_plugin_get_type (void);

gulong        meta_plugin_features            (MetaPlugin *plugin);
gboolean      meta_plugin_disabled            (MetaPlugin *plugin);
gboolean      meta_plugin_running             (MetaPlugin *plugin);
gboolean      meta_plugin_debug_mode          (MetaPlugin *plugin);

const MetaPluginInfo * meta_plugin_get_info (MetaPlugin *plugin);

struct _MetaPluginVersion
{
  /*
   * Version information; the first three numbers match the Meta version
   * with which the plugin was compiled (see clutter-plugins/simple.c for sample
   * code).
   */
  guint version_major;
  guint version_minor;
  guint version_micro;

  /*
   * Version of the plugin API; this is unrelated to the matacity version
   * per se. The API version is checked by the plugin manager and must match
   * the one used by it (see clutter-plugins/default.c for sample code).
   */
  guint version_api;
};

/*
 * Convenience macro to set up the plugin type. Based on GEdit.
 */
#define META_PLUGIN_DECLARE(ObjectName, object_name)                    \
  G_MODULE_EXPORT MetaPluginVersion meta_plugin_version =               \
    {                                                                   \
      MUFFIN_MAJOR_VERSION,                                             \
      MUFFIN_MINOR_VERSION,                                             \
      MUFFIN_MICRO_VERSION,                                             \
      MUFFIN_PLUGIN_API_VERSION                                         \
    };                                                                  \
                                                                        \
  static GType g_define_type_id = 0;                                    \
                                                                        \
  /* Prototypes */                                                      \
  G_MODULE_EXPORT                                                       \
  GType object_name##_get_type (void);                                  \
                                                                        \
  G_MODULE_EXPORT                                                       \
  GType object_name##_register_type (GTypeModule *type_module);         \
                                                                        \
  G_MODULE_EXPORT                                                       \
  GType meta_plugin_register_type (GTypeModule *type_module);           \
                                                                        \
  GType                                                                 \
  object_name##_get_type ()                                             \
  {                                                                     \
    return g_define_type_id;                                            \
  }                                                                     \
                                                                        \
  static void object_name##_init (ObjectName *self);                    \
  static void object_name##_class_init (ObjectName##Class *klass);      \
  static gpointer object_name##_parent_class = NULL;                    \
  static void object_name##_class_intern_init (gpointer klass)          \
  {                                                                     \
    object_name##_parent_class = g_type_class_peek_parent (klass);      \
    object_name##_class_init ((ObjectName##Class *) klass);             \
  }                                                                     \
                                                                        \
  GType                                                                 \
  object_name##_register_type (GTypeModule *type_module)                \
  {                                                                     \
    static const GTypeInfo our_info =                                   \
      {                                                                 \
        sizeof (ObjectName##Class),                                     \
        NULL, /* base_init */                                           \
        NULL, /* base_finalize */                                       \
        (GClassInitFunc) object_name##_class_intern_init,               \
        NULL,                                                           \
        NULL, /* class_data */                                          \
        sizeof (ObjectName),                                            \
        0, /* n_preallocs */                                            \
        (GInstanceInitFunc) object_name##_init                          \
      };                                                                \
                                                                        \
    g_define_type_id = g_type_module_register_type (type_module,        \
                                                    META_TYPE_PLUGIN,   \
                                                    #ObjectName,        \
                                                    &our_info,          \
                                                    0);                 \
                                                                        \
                                                                        \
    return g_define_type_id;                                            \
  }                                                                     \
                                                                        \
  G_MODULE_EXPORT GType                                                 \
  meta_plugin_register_type (GTypeModule *type_module)                  \
  {                                                                     \
    return object_name##_register_type (type_module);                   \
  }                                                                     \

void
meta_plugin_type_register (GType plugin_type);

void
meta_plugin_switch_workspace_completed (MetaPlugin *plugin);

void
meta_plugin_minimize_completed (MetaPlugin      *plugin,
                                MetaWindowActor *actor);

void
meta_plugin_maximize_completed (MetaPlugin      *plugin,
                                MetaWindowActor *actor);

void
meta_plugin_unmaximize_completed (MetaPlugin      *plugin,
                                  MetaWindowActor *actor);

void
meta_plugin_tile_completed     (MetaPlugin      *plugin,
                                MetaWindowActor *actor);

void
meta_plugin_map_completed (MetaPlugin      *plugin,
                           MetaWindowActor *actor);

void
meta_plugin_destroy_completed (MetaPlugin      *plugin,
                               MetaWindowActor *actor);

/**
 * MetaModalOptions:
 * @META_MODAL_POINTER_ALREADY_GRABBED: if set the pointer is already
 *   grabbed by the plugin and should not be grabbed again.
 * @META_MODAL_KEYBOARD_ALREADY_GRABBED: if set the keyboard is already
 *   grabbed by the plugin and should not be grabbed again.
 *
 * Options that can be provided when calling meta_plugin_begin_modal().
 */
typedef enum {
  META_MODAL_POINTER_ALREADY_GRABBED = 1 << 0,
  META_MODAL_KEYBOARD_ALREADY_GRABBED = 1 << 1
} MetaModalOptions;

gboolean
meta_plugin_begin_modal (MetaPlugin      *plugin,
                         Window           grab_window,
                         Cursor           cursor,
                         MetaModalOptions options,
                         guint32          timestamp);

void
meta_plugin_end_modal (MetaPlugin *plugin,
                       guint32     timestamp);

MetaScreen *meta_plugin_get_screen        (MetaPlugin *plugin);

void
_meta_plugin_effect_started (MetaPlugin *plugin);

#endif /* META_PLUGIN_H_ */
