/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin preferences */

/* 
 * Copyright (C) 2001 Havoc Pennington, Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2006 Elijah Newren
 * Copyright (C) 2008 Thomas Thurman
 * Copyright (C) 2010 Milan Bouchet-Valat, Copyright (C) 2011 Red Hat Inc.
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
 * SECTION:prefs
 * @title: Preferences
 * @short_description: Muffin preferences
 */

#include <config.h>
#include <meta/prefs.h>
#include "ui.h"
#include <meta/util.h>
#include "meta-plugin-manager.h"
#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include "keybindings-private.h"

/* If you add a key, it needs updating in init() and in the gsettings
 * notify listener and of course in the .schemas file.
 *
 * Keys which are handled by one of the unified handlers below are
 * not given a name here, because the purpose of the unified handlers
 * is that keys should be referred to exactly once.
 */
#define KEY_TITLEBAR_FONT "titlebar-font"
#define KEY_NUM_WORKSPACES "num-workspaces"
#define KEY_WORKSPACE_NAMES "workspace-names"
#define KEY_WORKSPACE_CYCLE "workspace-cycle"

/* Keys from "foreign" schemas */
#define KEY_GNOME_ANIMATIONS "enable-animations"
#define KEY_GNOME_CURSOR_THEME "cursor-theme"
#define KEY_GNOME_CURSOR_SIZE "cursor-size"

#define KEY_MIN_WINDOW_OPACITY "min-window-opacity"
#define KEY_WS_NAMES_GNOME "workspace-names"
#define KEY_WORKSPACES_ONLY_ON_PRIMARY "workspaces-only-on-primary"

#define KEY_MOUSEWHEEL_ZOOM_ENABLED "screen-magnifier-enabled"

/* These are the different schemas we are keeping
 * a GSettings instance for */
#define SCHEMA_GENERAL         "org.cinnamon.desktop.wm.preferences"
#define SCHEMA_MUFFIN          "org.cinnamon.muffin"
#define SCHEMA_INTERFACE       "org.cinnamon.desktop.interface"
#define SCHEMA_A11Y_APPLICATIONS "org.cinnamon.desktop.a11y.applications"

#define SETTINGS(s) g_hash_table_lookup (settings_schemas, (s))

static GList *changes = NULL;
static guint changed_idle;
static GList *listeners = NULL;
static GHashTable *settings_schemas;

static gboolean use_system_font = FALSE;
static PangoFontDescription *titlebar_font = NULL;
static MetaVirtualModifier mouse_button_mods = Mod1Mask;
static MetaVirtualModifier mouse_button_zoom_mods = Mod1Mask;
static gboolean mouse_zoom_enabled = FALSE;
static CDesktopFocusMode focus_mode = C_DESKTOP_FOCUS_MODE_CLICK;
static CDesktopFocusNewWindows focus_new_windows = C_DESKTOP_FOCUS_NEW_WINDOWS_SMART;
static gboolean raise_on_click = TRUE;
static gboolean attach_modal_dialogs = FALSE;
static char* current_theme = NULL;
static int num_workspaces = 4;
static gboolean workspace_cycle = FALSE;
static CDesktopTitlebarAction action_double_click_titlebar = C_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE;
static CDesktopTitlebarAction action_middle_click_titlebar = C_DESKTOP_TITLEBAR_ACTION_LOWER;
static CDesktopTitlebarAction action_right_click_titlebar = C_DESKTOP_TITLEBAR_ACTION_MENU;
static CDesktopTitlebarScrollAction action_scroll_titlebar = C_DESKTOP_TITLEBAR_SCROLL_ACTION_NONE;
static gboolean dynamic_workspaces = FALSE;
static gboolean unredirect_fullscreen_windows = FALSE;
static gboolean application_based = FALSE;
static gboolean disable_workarounds = FALSE;
static gboolean auto_raise = FALSE;
static gboolean auto_raise_delay = 500;
static gboolean gnome_animations = TRUE;
static char *cursor_theme = NULL;
static int   cursor_size = 24;
static int   draggable_border_width = 10;
static int tile_hud_threshold = 150;
static int resize_threshold = 24;
static int ui_scale = 1;
static int min_window_opacity = 0;
static gboolean resize_with_right_button = FALSE;
static gboolean edge_tiling = FALSE;
static gboolean edge_resistance_window = TRUE;
static gboolean force_fullscreen = TRUE;
static unsigned int snap_modifier[2];

static MetaButtonLayout button_layout;

/* NULL-terminated array */
static char **workspace_names = NULL;

static gboolean workspaces_only_on_primary = FALSE;

static gboolean legacy_snap = FALSE;
static gboolean invert_workspace_flip = FALSE;
static gboolean tile_maximize = FALSE;

static void handle_preference_update_enum (GSettings *settings,
                                           gchar     *key);
static gboolean update_binding         (MetaKeyPref *binding,
                                        gchar      **strokes);
static gboolean update_key_binding     (const char  *key,
                                        gchar      **strokes);
static gboolean update_workspace_names (void);
static void update_min_win_opacity (void);

static void settings_changed (GSettings      *settings,
                              gchar          *key,
                              gpointer        data);
static void bindings_changed (GSettings      *settings,
                              gchar          *key,
                              gpointer        data);

static void queue_changed (MetaPreference  pref);

static void maybe_give_disable_workarounds_warning (void);

static gboolean titlebar_handler (GVariant*, gpointer*, gpointer);
static gboolean theme_name_handler (GVariant*, gpointer*, gpointer);
static gboolean mouse_button_mods_handler (GVariant*, gpointer*, gpointer);
static gboolean mouse_button_zoom_mods_handler (GVariant*, gpointer*, gpointer);
static gboolean snap_modifier_handler (GVariant*, gpointer*, gpointer);
static gboolean button_layout_handler (GVariant*, gpointer*, gpointer);

static void     do_override               (char *key, char *schema);

static void     init_bindings             (void);
static void     init_workspace_names      (void);

static MetaPlacementMode placement_mode = META_PLACEMENT_MODE_AUTOMATIC;


typedef struct
{
  MetaPrefsChangedFunc func;
  gpointer data;
} MetaPrefsListener;

typedef struct
{
  char *key;
  char *schema;
  MetaPreference pref;
} MetaBasePreference;

typedef struct
{
  MetaBasePreference base;
  gpointer target;
} MetaEnumPreference;

typedef struct
{
  MetaBasePreference base;
  gboolean *target;
} MetaBoolPreference;

typedef struct
{
  MetaBasePreference base;

  /*
   * A handler.  Many of the string preferences aren't stored as
   * strings and need parsing; others of them have default values
   * which can't be solved in the general case.  If you include a
   * function pointer here, it will be called instead of writing
   * the string value out to the target variable.
   *
   * The function will be passed to g_settings_get_mapped() and should
   * return %TRUE if the mapping was successful and %FALSE otherwise.
   * In the former case the function is expected to handle the result
   * of the conversion itself and call queue_changed() appropriately;
   * in particular the @result (out) parameter as returned by
   * g_settings_get_mapped() will be ignored in all cases.
   *
   * This may be NULL.  If it is, see "target", below.
   */
  GSettingsGetMapping handler;

  /*
   * Where to write the incoming string.
   *
   * This must be NULL if the handler is non-NULL.
   * If the incoming string is NULL, no change will be made.
   */
  gchar **target;

} MetaStringPreference;

typedef struct
{
  MetaBasePreference base;
  gint *target;
} MetaIntPreference;


/* All preferences that are not keybindings must be listed here,
 * plus in the GSettings schemas and the MetaPreference enum.
 */

/* FIXMEs: */
/* @@@ Don't use NULL lines at the end; glib can tell you how big it is */

static MetaEnumPreference preferences_enum[] =
  {
    {
      { "focus-new-windows",
        SCHEMA_GENERAL,
        META_PREF_FOCUS_NEW_WINDOWS,
      },
      &focus_new_windows,
    },
    {
      { "focus-mode",
        SCHEMA_GENERAL,
        META_PREF_FOCUS_MODE,
      },
      &focus_mode,
    },
    {
      { "action-double-click-titlebar",
        SCHEMA_GENERAL,
        META_PREF_ACTION_DOUBLE_CLICK_TITLEBAR,
      },
      &action_double_click_titlebar,
    },
    {
      { "action-middle-click-titlebar",
        SCHEMA_GENERAL,
        META_PREF_ACTION_MIDDLE_CLICK_TITLEBAR,
      },
      &action_middle_click_titlebar,
    },
    {
      { "action-right-click-titlebar",
        SCHEMA_GENERAL,
        META_PREF_ACTION_RIGHT_CLICK_TITLEBAR,
      },
      &action_right_click_titlebar,
    },
    {
      { "action-scroll-titlebar",
        SCHEMA_GENERAL,
        META_PREF_ACTION_SCROLL_WHEEL_TITLEBAR,
      },
      &action_scroll_titlebar,
    },
    {
      { "placement-mode",
        SCHEMA_MUFFIN,
        META_PREF_PLACEMENT_MODE,
      },
      &placement_mode,
    },
    { { NULL, 0, 0 }, NULL },
  };

static MetaBoolPreference preferences_bool[] =
  {
    {
      { "attach-modal-dialogs",
        SCHEMA_MUFFIN,
        META_PREF_ATTACH_MODAL_DIALOGS,
      },
      &attach_modal_dialogs,
    },
    {
      { "raise-on-click",
        SCHEMA_GENERAL,
        META_PREF_RAISE_ON_CLICK,
      },
      &raise_on_click,
    },
    {
      { "titlebar-uses-system-font",
        SCHEMA_GENERAL,
        META_PREF_TITLEBAR_FONT, /* note! shares a pref */
      },
      &use_system_font,
    },
    {
      { "workspace-cycle",
        SCHEMA_MUFFIN,
        META_PREF_WORKSPACE_CYCLE,
      },
      &workspace_cycle,
    },
    {
      { "dynamic-workspaces",
        SCHEMA_MUFFIN,
        META_PREF_DYNAMIC_WORKSPACES,
      },
      &dynamic_workspaces,
    },
    {
      { "unredirect-fullscreen-windows",
        SCHEMA_MUFFIN,
        META_PREF_UNREDIRECT_FULLSCREEN_WINDOWS,
      },
      &unredirect_fullscreen_windows,
    },
    {
      { "application-based",
        SCHEMA_GENERAL,
        META_PREF_APPLICATION_BASED,
      },
      NULL, /* feature is known but disabled */
    },
    {
      { "disable-workarounds",
        SCHEMA_GENERAL,
        META_PREF_DISABLE_WORKAROUNDS,
      },
      &disable_workarounds,
    },
    {
      { "auto-raise",
        SCHEMA_GENERAL,
        META_PREF_AUTO_RAISE,
      },
      &auto_raise,
    },
    {
      { KEY_MOUSEWHEEL_ZOOM_ENABLED,
        SCHEMA_A11Y_APPLICATIONS,
        META_PREF_MOUSE_ZOOM_ENABLED,
      },
      &mouse_zoom_enabled,
    },
    {
      { KEY_GNOME_ANIMATIONS,
        SCHEMA_INTERFACE,
        META_PREF_GNOME_ANIMATIONS,
      },
      &gnome_animations,
    },
    {
      { "resize-with-right-button",
        SCHEMA_GENERAL,
        META_PREF_RESIZE_WITH_RIGHT_BUTTON,
      },
      &resize_with_right_button,
    },
    {
      { "edge-tiling",
        SCHEMA_MUFFIN,
        META_PREF_EDGE_TILING,
      },
      &edge_tiling,
    },
    {
      { "edge-resistance-window",
        SCHEMA_MUFFIN,
        META_PREF_EDGE_RESISTANCE_WINDOW,
      },
      &edge_resistance_window,
    },
    {
      { "workspaces-only-on-primary",
        SCHEMA_MUFFIN,
        META_PREF_WORKSPACES_ONLY_ON_PRIMARY,
      },
      &workspaces_only_on_primary,
    },
    {
      { "legacy-snap",
        SCHEMA_MUFFIN,
        META_PREF_LEGACY_SNAP,
      },
      &legacy_snap,
    },
    {
      { "invert-workspace-flip-direction",
        SCHEMA_MUFFIN,
        META_PREF_INVERT_WORKSPACE_FLIP_DIRECTION,
      },
      &invert_workspace_flip,
    },
    {
      { "tile-maximize",
        SCHEMA_MUFFIN,
        META_PREF_TILE_MAXIMIZE,
      },
      &tile_maximize,
    },
    { { NULL, 0, 0 }, NULL },
  };

static MetaStringPreference preferences_string[] =
  {
    {
      { "mouse-button-modifier",
        SCHEMA_GENERAL,
        META_PREF_MOUSE_BUTTON_MODS,
      },
      mouse_button_mods_handler,
      NULL,
    },
    {
      { "mouse-button-zoom-modifier",
        SCHEMA_GENERAL,
        META_PREF_MOUSE_BUTTON_ZOOM_MODS,
      },
      mouse_button_zoom_mods_handler,
      NULL,
    },
    {
      { "theme",
        SCHEMA_GENERAL,
        META_PREF_THEME,
      },
      theme_name_handler,
      NULL,
    },
    {
      { KEY_TITLEBAR_FONT,
        SCHEMA_GENERAL,
        META_PREF_TITLEBAR_FONT,
      },
      titlebar_handler,
      NULL,
    },
    {
      { "button-layout",
        SCHEMA_MUFFIN,
        META_PREF_BUTTON_LAYOUT,
      },
      button_layout_handler,
      NULL,
    },
    {
      { "cursor-theme",
        SCHEMA_INTERFACE,
        META_PREF_CURSOR_THEME,
      },
      NULL,
      &cursor_theme,
    },
    {
      { "snap-modifier",
        SCHEMA_MUFFIN,
        META_PREF_SNAP_MODIFIER,
      },
      snap_modifier_handler,
      NULL,
    },

    { { NULL, 0, 0 }, NULL },
  };

static MetaIntPreference preferences_int[] =
  {
    {
      { KEY_NUM_WORKSPACES,
        SCHEMA_GENERAL,
        META_PREF_NUM_WORKSPACES,
      },
      &num_workspaces
    },
    {
      { "auto-raise-delay",
        SCHEMA_GENERAL,
        META_PREF_AUTO_RAISE_DELAY,
      },
      &auto_raise_delay
    },
    {
      { "cursor-size",
        SCHEMA_INTERFACE,
        META_PREF_CURSOR_SIZE,
      },
      &cursor_size
    },
    {
      { "draggable-border-width",
        SCHEMA_MUFFIN,
        META_PREF_DRAGGABLE_BORDER_WIDTH,
      },
      &draggable_border_width
    },
    {
      { "tile-hud-threshold",
        SCHEMA_MUFFIN,
        META_PREF_TILE_HUD_THRESHOLD,
      },
      &tile_hud_threshold
    },
    {
      { "resize-threshold",
        SCHEMA_MUFFIN,
        META_PREF_RESIZE_THRESHOLD,
      },
      &resize_threshold
    },
    { { NULL, 0, 0 }, NULL },
  };

/*
 * This is used to keep track of override schemas used to
 * override preferences from the "normal" metacity/muffin
 * schemas; we modify the preferences arrays directly, but
 * we also need to remember what we have done to handle
 * subsequent overrides correctly.
 */
typedef struct
{
  char *key;
  char *new_schema;
} MetaPrefsOverriddenKey;

static GSList *overridden_keys;

static void
handle_preference_init_enum (void)
{
  MetaEnumPreference *cursor = preferences_enum;

  while (cursor->base.key != NULL)
    {
      if (cursor->target==NULL)
        continue;

      *((gint *) cursor->target) =
        g_settings_get_enum (SETTINGS (cursor->base.schema), cursor->base.key);

      ++cursor;
    }
}

static void
handle_preference_init_bool (void)
{
  MetaBoolPreference *cursor = preferences_bool;

  while (cursor->base.key != NULL)
    {
      if (cursor->target!=NULL)
        *cursor->target =
          g_settings_get_boolean (SETTINGS (cursor->base.schema),
                                  cursor->base.key);

      ++cursor;
    }

  maybe_give_disable_workarounds_warning ();
}

static void
handle_preference_init_string (void)
{
  MetaStringPreference *cursor = preferences_string;

  while (cursor->base.key != NULL)
    {
      char *value;

      /* Complex keys have a mapping function to check validity */
      if (cursor->handler)
        {
          if (cursor->target)
            meta_bug ("%s has both a target and a handler\n", cursor->base.key);

          g_settings_get_mapped (SETTINGS (cursor->base.schema),
                                 cursor->base.key, cursor->handler, NULL);
        }
      else
        {
          if (!cursor->target)
            meta_bug ("%s must have handler or target\n", cursor->base.key);

          if (*(cursor->target))
            g_free (*(cursor->target));

          value = g_settings_get_string (SETTINGS (cursor->base.schema),
                                         cursor->base.key);

          *(cursor->target) = value;
        }

      ++cursor;
    }
}

static void
handle_preference_init_int (void)
{
  MetaIntPreference *cursor = preferences_int;

  
  while (cursor->base.key != NULL)
    {
      if (cursor->target)
        *cursor->target = g_settings_get_int (SETTINGS (cursor->base.schema),
                                              cursor->base.key);

      ++cursor;
    }
}

static void
handle_preference_update_enum (GSettings *settings,
                               gchar *key)
{
  MetaEnumPreference *cursor = preferences_enum;
  gint old_value;

  while (cursor->base.key != NULL && strcmp (key, cursor->base.key) != 0)
    ++cursor;

  if (cursor->base.key == NULL)
    /* Didn't recognise that key. */
    return;

  /* We need to know whether the value changes, so
   * store the current value away.
   */

  old_value = * ((gint *)cursor->target);
  *((gint *)cursor->target) =
    g_settings_get_enum (SETTINGS (cursor->base.schema), key);

  /* Did it change?  If so, tell the listeners about it. */
  if (old_value != *((gint *)cursor->target))
    queue_changed (cursor->base.pref);
}

static void
handle_preference_update_bool (GSettings *settings,
                               gchar *key)
{
  MetaBoolPreference *cursor = preferences_bool;
  gboolean old_value;

  while (cursor->base.key != NULL && strcmp (key, cursor->base.key) != 0)
    ++cursor;

  if (cursor->base.key == NULL || cursor->target == NULL)
    /* Unknown key or no work for us to do. */
    return;

  /* We need to know whether the value changes, so
   * store the current value away.
   */
  old_value = *((gboolean *) cursor->target);
  
  /* Now look it up... */
  *((gboolean *) cursor->target) =
    g_settings_get_boolean (SETTINGS (cursor->base.schema), key);

  /* Did it change?  If so, tell the listeners about it. */
  if (old_value != *((gboolean *)cursor->target))
    queue_changed (cursor->base.pref);

  if (cursor->base.pref==META_PREF_DISABLE_WORKAROUNDS)
    maybe_give_disable_workarounds_warning ();
}

static void
handle_preference_update_string (GSettings *settings,
                                 gchar *key)
{
  MetaStringPreference *cursor = preferences_string;
  char *value;
  gboolean inform_listeners = FALSE;

  while (cursor->base.key != NULL && strcmp (key, cursor->base.key) != 0)
    ++cursor;

  if (cursor->base.key==NULL)
    /* Didn't recognise that key. */
    return;

  /* Complex keys have a mapping function to check validity */
  if (cursor->handler)
    {
      if (cursor->target)
        meta_bug ("%s has both a target and a handler\n", cursor->base.key);

      g_settings_get_mapped (SETTINGS (cursor->base.schema),
                             cursor->base.key, cursor->handler, NULL);
    }
  else
    {
      if (!cursor->target)
        meta_bug ("%s must have handler or target\n", cursor->base.key);

      value = g_settings_get_string (SETTINGS (cursor->base.schema),
                                     cursor->base.key);

      inform_listeners = (g_strcmp0 (value, *(cursor->target)) != 0);

      if (*(cursor->target))
        g_free(*(cursor->target));

      *(cursor->target) = value;
    }

  if (inform_listeners)
    queue_changed (cursor->base.pref);
}

static void
handle_preference_update_int (GSettings *settings,
                              gchar *key)
{
  MetaIntPreference *cursor = preferences_int;
  gint new_value;

  while (cursor->base.key != NULL && strcmp (key, cursor->base.key) != 0)
    ++cursor;

  if (cursor->base.key == NULL || cursor->target == NULL)
    /* Unknown key or no work for us to do. */
    return;

  new_value = g_settings_get_int (SETTINGS (cursor->base.schema), key);

  /* Did it change?  If so, tell the listeners about it. */
  if (*cursor->target != new_value)
    {
      *cursor->target = new_value;
      queue_changed (cursor->base.pref);
    }
}


/****************************************************************************/
/* Listeners.                                                               */
/****************************************************************************/

/**
 * meta_prefs_add_listener: (skip)
 *
 */
void
meta_prefs_add_listener (MetaPrefsChangedFunc func,
                         gpointer             data)
{
  MetaPrefsListener *l;

  l = g_new (MetaPrefsListener, 1);
  l->func = func;
  l->data = data;

  listeners = g_list_prepend (listeners, l);
}

/**
 * meta_prefs_remove_listener: (skip)
 *
 */
void
meta_prefs_remove_listener (MetaPrefsChangedFunc func,
                            gpointer             data)
{
  GList *tmp;

  tmp = listeners;
  while (tmp != NULL)
    {
      MetaPrefsListener *l = tmp->data;

      if (l->func == func &&
          l->data == data)
        {
          g_free (l);
          listeners = g_list_delete_link (listeners, tmp);

          return;
        }
      
      tmp = tmp->next;
    }

  meta_bug ("Did not find listener to remove\n");
}

static void
emit_changed (MetaPreference pref)
{
  GList *tmp;
  GList *copy;

  meta_topic (META_DEBUG_PREFS, "Notifying listeners that pref %s changed\n",
              meta_preference_to_string (pref));
  
  copy = g_list_copy (listeners);
  
  tmp = copy;

  while (tmp != NULL)
    {
      MetaPrefsListener *l = tmp->data;

      (* l->func) (pref, l->data);

      tmp = tmp->next;
    }

  g_list_free (copy);
}

static gboolean
changed_idle_handler (gpointer data)
{
  GList *tmp;
  GList *copy;

  changed_idle = 0;
  
  copy = g_list_copy (changes); /* reentrancy paranoia */

  g_list_free (changes);
  changes = NULL;
  
  tmp = copy;
  while (tmp != NULL)
    {
      MetaPreference pref = GPOINTER_TO_INT (tmp->data);

      emit_changed (pref);
      
      tmp = tmp->next;
    }

  g_list_free (copy);
  
  return FALSE;
}

static void
queue_changed (MetaPreference pref)
{
  meta_topic (META_DEBUG_PREFS, "Queueing change of pref %s\n",
              meta_preference_to_string (pref));  

  if (g_list_find (changes, GINT_TO_POINTER (pref)) == NULL)
    changes = g_list_prepend (changes, GINT_TO_POINTER (pref));
  else
    meta_topic (META_DEBUG_PREFS, "Change of pref %s was already pending\n",
                meta_preference_to_string (pref));

  if (changed_idle == 0)
    changed_idle = g_idle_add_full (META_PRIORITY_PREFS_NOTIFY,
                                    changed_idle_handler, NULL, NULL);
}

static void
update_ui_scale (GdkScreen *screen, gpointer data)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);

  gdk_screen_get_setting (screen, "gdk-window-scaling-factor", &value);
  ui_scale = MAX (g_value_get_int (&value), 1); // Never let it be 0;
}


/****************************************************************************/
/* Initialisation.                                                          */
/****************************************************************************/

void
meta_prefs_init (void)
{
  GSettings *settings;
  GSList *tmp;

  settings_schemas = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_object_unref);

  settings = g_settings_new (SCHEMA_GENERAL);
  g_signal_connect (settings, "changed", G_CALLBACK (settings_changed), NULL);
  g_hash_table_insert (settings_schemas, g_strdup (SCHEMA_GENERAL), settings);

  settings = g_settings_new (SCHEMA_MUFFIN);
  g_signal_connect (settings, "changed", G_CALLBACK (settings_changed), NULL);
  g_hash_table_insert (settings_schemas, g_strdup (SCHEMA_MUFFIN), settings);

  /* Individual keys we watch outside of our schemas */
  settings = g_settings_new (SCHEMA_INTERFACE);
  g_signal_connect (settings, "changed::" KEY_GNOME_ANIMATIONS,
                    G_CALLBACK (settings_changed), NULL);
  g_signal_connect (settings, "changed::" KEY_GNOME_CURSOR_THEME,
                    G_CALLBACK (settings_changed), NULL);
  g_signal_connect (settings, "changed::" KEY_GNOME_CURSOR_SIZE,
                    G_CALLBACK (settings_changed), NULL);
  g_hash_table_insert (settings_schemas, g_strdup (SCHEMA_INTERFACE), settings);

  settings = g_settings_new (SCHEMA_A11Y_APPLICATIONS);
  g_signal_connect (settings, "changed::" KEY_MOUSEWHEEL_ZOOM_ENABLED,
                    G_CALLBACK (settings_changed), NULL);
  g_hash_table_insert (settings_schemas, g_strdup (SCHEMA_A11Y_APPLICATIONS), settings);

  for (tmp = overridden_keys; tmp; tmp = tmp->next)
    {
      MetaPrefsOverriddenKey *override = tmp->data;
      do_override (override->key, override->new_schema);
    }

  /* Pick up initial values. */

  handle_preference_init_enum ();
  handle_preference_init_bool ();
  handle_preference_init_string ();
  handle_preference_init_int ();

  GdkDisplay *display = gdk_display_get_default();

  g_signal_connect (gdk_display_get_default_screen (display), "monitors-changed",
                    G_CALLBACK (update_ui_scale), NULL);
  g_signal_connect (gdk_display_get_default_screen (display), "size-changed",
                    G_CALLBACK (update_ui_scale), NULL);

  update_ui_scale (gdk_display_get_default_screen (display), NULL);

  init_bindings ();
  init_workspace_names ();
  update_min_win_opacity ();
}

static gboolean
find_pref (void                *prefs,
           size_t               pref_size,
           const char          *search_key,
           MetaBasePreference **pref)
{
  void *p = prefs;

  while (TRUE)
    {
      char **key = p;
      if (*key == NULL)
        break;

      if (strcmp (*key, search_key) == 0)
        {
          *pref = p;
          return TRUE;
        }

      p = (guchar *)p + pref_size;
    }

  return FALSE;
}


static void
do_override (char *key,
             char *schema)
{
  MetaBasePreference *pref;
  GSettings *settings;
  char *detailed_signal;
  gpointer data;
  guint handler_id;

  g_return_if_fail (settings_schemas != NULL);

  if (!find_pref (preferences_enum, sizeof(MetaEnumPreference), key, &pref) &&
      !find_pref (preferences_bool, sizeof(MetaBoolPreference), key, &pref) &&
      !find_pref (preferences_string, sizeof(MetaStringPreference), key, &pref) &&
      !find_pref (preferences_int, sizeof(MetaIntPreference), key, &pref))
    {
      meta_warning ("Can't override preference key, \"%s\" not found\n", key);
      return;
    }

  settings = SETTINGS (pref->schema);
  data = g_object_get_data (G_OBJECT (settings), key);
  if (data)
    {
      handler_id = GPOINTER_TO_UINT (data);
      g_signal_handler_disconnect (settings, handler_id);
    }

  pref->schema = schema;
  settings = SETTINGS (pref->schema);
  if (!settings)
    {
      settings = g_settings_new (pref->schema);
      g_hash_table_insert (settings_schemas, g_strdup (pref->schema), settings);
    }

  detailed_signal = g_strdup_printf ("changed::%s", key);
  handler_id = g_signal_connect (settings, detailed_signal,
                                 G_CALLBACK (settings_changed), NULL);
  g_free (detailed_signal);

  g_object_set_data (G_OBJECT (settings), key, GUINT_TO_POINTER (handler_id));

  settings_changed (settings, key, NULL);
}


/**
 * meta_prefs_override_preference_schema:
 * @key: the preference name
 * @schema: new schema for preference %key
 *
 * Specify a schema whose keys are used to override the standard Metacity
 * keys. This might be used if a plugin expected a different value for
 * some preference than the Metacity default. While this function can be
 * called at any point, this function should generally be called in a
 * plugin's constructor, rather than in its start() method so the preference
 * isn't first loaded with one value then changed to another value.
 */
void
meta_prefs_override_preference_schema (const char *key, const char *schema)
{
  MetaPrefsOverriddenKey *overridden;
  GSList *tmp;

  /* Merge identical overrides, this isn't an error */
  for (tmp = overridden_keys; tmp; tmp = tmp->next)
    {
      MetaPrefsOverriddenKey *tmp_overridden = tmp->data;
      if (strcmp (tmp_overridden->key, key) == 0 &&
          strcmp (tmp_overridden->new_schema, schema) == 0)
        return;
    }

  overridden = NULL;

  for (tmp = overridden_keys; tmp; tmp = tmp->next)
    {
      MetaPrefsOverriddenKey *tmp_overridden = tmp->data;
      if (strcmp (tmp_overridden->key, key) == 0)
        overridden = tmp_overridden;
    }

  if (overridden)
    {
      g_free (overridden->new_schema);
      overridden->new_schema = g_strdup (schema);
    }
  else
    {
      overridden = g_slice_new (MetaPrefsOverriddenKey);
      overridden->key = g_strdup (key);
      overridden->new_schema = g_strdup (schema);

      overridden_keys = g_slist_prepend (overridden_keys, overridden);
    }

  if (settings_schemas != NULL)
    do_override (overridden->key, overridden->new_schema);
}


/****************************************************************************/
/* Updates.                                                                 */
/****************************************************************************/


static void
settings_changed (GSettings *settings,
                  gchar *key,
                  gpointer data)
{
  GVariant *value;
  const GVariantType *type;
  MetaEnumPreference *cursor;
  gboolean found_enum;
  gchar *schema;
  
  g_object_get(settings, "schema", &schema, NULL);

  /* String array, handled separately */
  if (strcmp (key, KEY_WORKSPACE_NAMES) == 0)
    {
      if (update_workspace_names ())
        queue_changed (META_PREF_WORKSPACE_NAMES);
      return;
    }
  
  if (strcmp (key, KEY_MIN_WINDOW_OPACITY) == 0)
    {
      update_min_win_opacity ();
      queue_changed (META_PREF_MIN_WIN_OPACITY);
      return;
    }

  value = g_settings_get_value (settings, key);
  type = g_variant_get_type (value);

  if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
    handle_preference_update_bool (settings, key);
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT32))
    handle_preference_update_int (settings, key);
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING))
    {
      cursor = preferences_enum;
      found_enum = FALSE;

      while (cursor->base.key != NULL)
        {

          if (strcmp (key, cursor->base.key) == 0)
            found_enum = TRUE;

          cursor++;
        }

      if (found_enum)
        handle_preference_update_enum (settings, key);
      else
        handle_preference_update_string (settings, key);
    }
  else if (g_str_equal (key, KEY_WS_NAMES_GNOME))
    {
      return;
    }
  else
    {
      /* Someone added a preference of an unhandled type */
      g_assert_not_reached ();
    }

  g_variant_unref (value);
}

static void
bindings_changed (GSettings *settings,
                  gchar *key,
                  gpointer data)
{
  gchar **strokes;
  strokes = g_settings_get_strv (settings, key);

  if (update_key_binding (key, strokes))
    queue_changed (META_PREF_KEYBINDINGS);

  g_strfreev (strokes);
}

/*
 * Special case: give a warning the first time disable_workarounds
 * is turned on.
 */
static void
maybe_give_disable_workarounds_warning (void)
{
  static gboolean first_disable = TRUE;
    
  if (first_disable && disable_workarounds)
    {
      first_disable = FALSE;

      meta_warning (_("Workarounds for broken applications disabled. "
                      "Some applications may not behave properly.\n"));
    }
}

MetaVirtualModifier
meta_prefs_get_mouse_button_mods  (void)
{
  return mouse_button_mods;
}

MetaVirtualModifier
meta_prefs_get_mouse_button_zoom_mods  (void)
{
  return mouse_button_zoom_mods;
}

gboolean
meta_prefs_get_mouse_zoom_enabled (void)
{
  return mouse_zoom_enabled;
}

CDesktopFocusMode
meta_prefs_get_focus_mode (void)
{
  return focus_mode;
}

CDesktopFocusNewWindows
meta_prefs_get_focus_new_windows (void)
{
  return focus_new_windows;
}

gboolean
meta_prefs_get_attach_modal_dialogs (void)
{
  return attach_modal_dialogs;
}

gboolean
meta_prefs_get_raise_on_click (void)
{
  /* Force raise_on_click on for click-to-focus, as requested by Havoc
   * in #326156.
   */
  return raise_on_click || focus_mode == C_DESKTOP_FOCUS_MODE_CLICK;
}

const char*
meta_prefs_get_theme (void)
{
  return current_theme;
}

const char*
meta_prefs_get_cursor_theme (void)
{
  return cursor_theme;
}

int
meta_prefs_get_cursor_size (void)
{
  return cursor_size * ui_scale;
}


/****************************************************************************/
/* Handlers for string preferences.                                         */
/****************************************************************************/

static gboolean
titlebar_handler (GVariant *value,
                  gpointer *result,
                  gpointer data)
{
  PangoFontDescription *desc;
  const gchar *string_value;

  *result = NULL; /* ignored */
  string_value = g_variant_get_string (value, NULL);
  desc = pango_font_description_from_string (string_value);

  if (desc == NULL)
    {
      meta_warning (_("Could not parse font description "
                      "\"%s\" from GSettings key %s\n"),
                    string_value ? string_value : "(null)",
                    KEY_TITLEBAR_FONT);
      return FALSE;
    }

  /* Is the new description the same as the old? */
  if (titlebar_font &&
      pango_font_description_equal (desc, titlebar_font))
    {
      pango_font_description_free (desc);
    }
  else
    {
      if (titlebar_font)
        pango_font_description_free (titlebar_font);

      titlebar_font = desc;
      queue_changed (META_PREF_TITLEBAR_FONT);
    }

  return TRUE;
}

static gboolean
theme_name_handler (GVariant *value,
                    gpointer *result,
                    gpointer  data)
{
  const gchar *string_value;

  *result = NULL; /* ignored */
  string_value = g_variant_get_string (value, NULL);

  if (!string_value || !*string_value)
    return FALSE;

  if (g_strcmp0 (current_theme, string_value) != 0)
    {
      if (current_theme)
        g_free (current_theme);

      current_theme = g_strdup (string_value);
      queue_changed (META_PREF_THEME);
    }

  return TRUE;
}

static gboolean
mouse_button_mods_handler (GVariant *value,
                           gpointer *result,
                           gpointer  data)
{
  MetaVirtualModifier mods;
  const gchar *string_value;

  *result = NULL; /* ignored */
  string_value = g_variant_get_string (value, NULL);

  if (!string_value || !meta_ui_parse_modifier (string_value, &mods))
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Failed to parse new GSettings value\n");
          
      meta_warning (_("\"%s\" found in configuration database is "
                      "not a valid value for mouse button modifier\n"),
                    string_value);

      return FALSE;
    }

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Mouse button modifier has new GSettings value \"%s\"\n",
              string_value);

  if (mods != mouse_button_mods)
    {
      mouse_button_mods = mods;
      queue_changed (META_PREF_MOUSE_BUTTON_MODS);
    }

  return TRUE;
}

static gboolean
mouse_button_zoom_mods_handler (GVariant *value,
                                gpointer *result,
                                gpointer  data)
{
  MetaVirtualModifier mods;
  const gchar *string_value;

  *result = NULL; /* ignored */
  string_value = g_variant_get_string (value, NULL);

  if (!string_value || !meta_ui_parse_modifier (string_value, &mods))
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Failed to parse new GSettings value\n");

      meta_warning (_("\"%s\" found in configuration database is "
                      "not a valid value for mouse button zoom modifier\n"),
                    string_value);

      return FALSE;
    }

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Mouse zoom modifier has new GSettings value \"%s\"\n",
              string_value);

  if (mods != mouse_button_zoom_mods)
    {
      mouse_button_zoom_mods = mods;
      queue_changed (META_PREF_MOUSE_BUTTON_ZOOM_MODS);
    }

  return TRUE;
}

static gboolean
snap_modifier_handler (GVariant *value,
                       gpointer *result,
                       gpointer  data)
{

  const gchar *string_value;

  *result = NULL; /* ignored */
  string_value = g_variant_get_string (value, NULL);

  if (g_strcmp0 (string_value, "Super") == 0)
  {
    snap_modifier[0] = XStringToKeysym("Super_L");
    snap_modifier[1] = XStringToKeysym("Super_R");
  }
  else if (g_strcmp0 (string_value, "Alt") == 0)
  {
    snap_modifier[0] = XStringToKeysym("Alt_L");
    snap_modifier[1] = XStringToKeysym("Alt_R");
  }
  else if (g_strcmp0 (string_value, "Shift") == 0)
  {
    snap_modifier[0] = XStringToKeysym("Shift_L");
    snap_modifier[1] = XStringToKeysym("Shift_R");
  }
  else if (g_strcmp0 (string_value, "Control") == 0)
  {
    snap_modifier[0] = XStringToKeysym("Control_L");
    snap_modifier[1] = XStringToKeysym("Control_R");
  } else
  {
    snap_modifier[0] = 0;
    snap_modifier[1] = 0;
  }
  return TRUE;
}

static gboolean
button_layout_equal (const MetaButtonLayout *a,
                     const MetaButtonLayout *b)
{  
  int i;

  i = 0;
  while (i < MAX_BUTTONS_PER_CORNER)
    {
      if (a->left_buttons[i] != b->left_buttons[i])
        return FALSE;
      if (a->right_buttons[i] != b->right_buttons[i])
        return FALSE;
      if (a->left_buttons_has_spacer[i] != b->left_buttons_has_spacer[i])
        return FALSE;
      if (a->right_buttons_has_spacer[i] != b->right_buttons_has_spacer[i])
        return FALSE;
      ++i;
    }

  return TRUE;
}

/*
 * This conversion cannot be handled by GSettings since
 * several values are stored in the same key (as a string).
 */
static MetaButtonFunction
button_function_from_string (const char *str)
{
  if (strcmp (str, "menu") == 0)
    return META_BUTTON_FUNCTION_MENU;
  else if (strcmp (str, "minimize") == 0)
    return META_BUTTON_FUNCTION_MINIMIZE;
  else if (strcmp (str, "maximize") == 0)
    return META_BUTTON_FUNCTION_MAXIMIZE;
  else if (strcmp (str, "close") == 0)
    return META_BUTTON_FUNCTION_CLOSE;
  else if (strcmp (str, "shade") == 0)
    return META_BUTTON_FUNCTION_SHADE;
  else if (strcmp (str, "above") == 0)
    return META_BUTTON_FUNCTION_ABOVE;
  else if (strcmp (str, "stick") == 0)
    return META_BUTTON_FUNCTION_STICK;
  else 
    /* don't know; give up */
    return META_BUTTON_FUNCTION_LAST;
}

static MetaButtonFunction
button_opposite_function (MetaButtonFunction ofwhat)
{
  switch (ofwhat)
    {
    case META_BUTTON_FUNCTION_SHADE:
      return META_BUTTON_FUNCTION_UNSHADE;
    case META_BUTTON_FUNCTION_UNSHADE:
      return META_BUTTON_FUNCTION_SHADE;

    case META_BUTTON_FUNCTION_ABOVE:
      return META_BUTTON_FUNCTION_UNABOVE;
    case META_BUTTON_FUNCTION_UNABOVE:
      return META_BUTTON_FUNCTION_ABOVE;

    case META_BUTTON_FUNCTION_STICK:
      return META_BUTTON_FUNCTION_UNSTICK;
    case META_BUTTON_FUNCTION_UNSTICK:
      return META_BUTTON_FUNCTION_STICK;

    default:
      return META_BUTTON_FUNCTION_LAST;
    }
}

static gboolean
button_layout_handler (GVariant *value,
                       gpointer *result,
                       gpointer  data)
{
  MetaButtonLayout new_layout;
  const gchar *string_value;
  char **sides = NULL;
  int i;

  /* We need to ignore unknown button functions, for
   * compat with future versions
   */

  *result = NULL; /* ignored */
  string_value = g_variant_get_string (value, NULL);

  if (string_value)
    sides = g_strsplit (string_value, ":", 2);

  i = 0;
  if (sides != NULL && sides[0] != NULL)
    {
      char **buttons;
      int b;
      gboolean used[META_BUTTON_FUNCTION_LAST];

      while (i < META_BUTTON_FUNCTION_LAST)
        {
          used[i] = FALSE;
          new_layout.left_buttons_has_spacer[i] = FALSE;
          ++i;
        }

      buttons = g_strsplit (sides[0], ",", -1);
      i = 0;
      b = 0;
      while (buttons[b] != NULL)
        {
          MetaButtonFunction f = button_function_from_string (buttons[b]);
          if (i > 0 && strcmp("spacer", buttons[b]) == 0)
            {
              new_layout.left_buttons_has_spacer[i-1] = TRUE;
              f = button_opposite_function (f);

              if (f != META_BUTTON_FUNCTION_LAST)
                {
                  new_layout.left_buttons_has_spacer[i-2] = TRUE;
                }
            }
          else
            {
              if (f != META_BUTTON_FUNCTION_LAST && !used[f])
                {
                  new_layout.left_buttons[i] = f;
                  used[f] = TRUE;
                  ++i;

                  f = button_opposite_function (f);

                  if (f != META_BUTTON_FUNCTION_LAST)
                      new_layout.left_buttons[i++] = f;
                }
              else
                {
                  meta_topic (META_DEBUG_PREFS, "Ignoring unknown or already-used button name \"%s\"\n",
                              buttons[b]);
                }
            }

          ++b;
        }

      g_strfreev (buttons);
    }

  for (; i < MAX_BUTTONS_PER_CORNER; i++)
    {
      new_layout.left_buttons[i] = META_BUTTON_FUNCTION_LAST;
      new_layout.left_buttons_has_spacer[i] = FALSE;
    }

  i = 0;
  if (sides != NULL && sides[0] != NULL && sides[1] != NULL)
    {
      char **buttons;
      int b;
      gboolean used[META_BUTTON_FUNCTION_LAST];

      while (i < META_BUTTON_FUNCTION_LAST)
        {
          used[i] = FALSE;
          new_layout.right_buttons_has_spacer[i] = FALSE;
          ++i;
        }
      
      buttons = g_strsplit (sides[1], ",", -1);
      i = 0;
      b = 0;
      while (buttons[b] != NULL)
        {
          MetaButtonFunction f = button_function_from_string (buttons[b]);
          if (i > 0 && strcmp("spacer", buttons[b]) == 0)
            {
              new_layout.right_buttons_has_spacer[i-1] = TRUE;
              f = button_opposite_function (f);
              if (f != META_BUTTON_FUNCTION_LAST)
                {
                  new_layout.right_buttons_has_spacer[i-2] = TRUE;
                }
            }
          else
            {
              if (f != META_BUTTON_FUNCTION_LAST && !used[f])
                {
                  new_layout.right_buttons[i] = f;
                  used[f] = TRUE;
                  ++i;

                  f = button_opposite_function (f);

                  if (f != META_BUTTON_FUNCTION_LAST)
                      new_layout.right_buttons[i++] = f;

                }
              else
                {
                  meta_topic (META_DEBUG_PREFS, "Ignoring unknown or already-used button name \"%s\"\n",
                              buttons[b]);
                }
            }
          
          ++b;
        }

      g_strfreev (buttons);
    }

  for (; i < MAX_BUTTONS_PER_CORNER; i++)
    {
      new_layout.right_buttons[i] = META_BUTTON_FUNCTION_LAST;
      new_layout.right_buttons_has_spacer[i] = FALSE;
    }

  g_strfreev (sides);
  
  /* Invert the button layout for RTL languages */
  if (meta_ui_get_direction() == META_UI_DIRECTION_RTL)
  {
    MetaButtonLayout rtl_layout;
    int j;
    
    for (i = 0; new_layout.left_buttons[i] != META_BUTTON_FUNCTION_LAST; i++);
    for (j = 0; j < i; j++)
      {
        rtl_layout.right_buttons[j] = new_layout.left_buttons[i - j - 1];
        if (j == 0)
          rtl_layout.right_buttons_has_spacer[i - 1] = new_layout.left_buttons_has_spacer[i - j - 1];
        else
          rtl_layout.right_buttons_has_spacer[j - 1] = new_layout.left_buttons_has_spacer[i - j - 1];
      }
    for (; j < MAX_BUTTONS_PER_CORNER; j++)
      {
        rtl_layout.right_buttons[j] = META_BUTTON_FUNCTION_LAST;
        rtl_layout.right_buttons_has_spacer[j] = FALSE;
      }
      
    for (i = 0; new_layout.right_buttons[i] != META_BUTTON_FUNCTION_LAST; i++);
    for (j = 0; j < i; j++)
      {
        rtl_layout.left_buttons[j] = new_layout.right_buttons[i - j - 1];
        if (j == 0)
          rtl_layout.left_buttons_has_spacer[i - 1] = new_layout.right_buttons_has_spacer[i - j - 1];
        else
          rtl_layout.left_buttons_has_spacer[j - 1] = new_layout.right_buttons_has_spacer[i - j - 1];
      }
    for (; j < MAX_BUTTONS_PER_CORNER; j++)
      {
        rtl_layout.left_buttons[j] = META_BUTTON_FUNCTION_LAST;
        rtl_layout.left_buttons_has_spacer[j] = FALSE;
      }

    new_layout = rtl_layout;
  }

  if (!button_layout_equal (&button_layout, &new_layout))
    {
      button_layout = new_layout;
      emit_changed (META_PREF_BUTTON_LAYOUT);
    }

  return TRUE;
}

const PangoFontDescription*
meta_prefs_get_titlebar_font (void)
{
  if (use_system_font)
    return NULL;
  else
    return titlebar_font;
}

int
meta_prefs_get_num_workspaces (void)
{
  return num_workspaces;
}

gboolean
meta_prefs_get_workspace_cycle (void)
{
  return workspace_cycle;
}

gboolean
meta_prefs_get_dynamic_workspaces (void)
{
  return dynamic_workspaces;
}

gboolean
meta_prefs_get_unredirect_fullscreen_windows (void)
{
  return unredirect_fullscreen_windows;
}

gboolean
meta_prefs_get_application_based (void)
{
  return FALSE; /* For now, we never want this to do anything */
  
  return application_based;
}

gboolean
meta_prefs_get_disable_workarounds (void)
{
  return disable_workarounds;
}

#ifdef WITH_VERBOSE_MODE
const char*
meta_preference_to_string (MetaPreference pref)
{
  /* TODO: better handled via GLib enum nicknames */
  switch (pref)
    {
    case META_PREF_MOUSE_BUTTON_MODS:
      return "MOUSE_BUTTON_MODS";

    case META_PREF_MOUSE_BUTTON_ZOOM_MODS:
      return "MOUSE_BUTTON_ZOOM_MODS";

    case META_PREF_MOUSE_ZOOM_ENABLED:
      return "MOUSE_ZOOM_ENABLED";

    case META_PREF_FOCUS_MODE:
      return "FOCUS_MODE";

    case META_PREF_FOCUS_NEW_WINDOWS:
      return "FOCUS_NEW_WINDOWS";

    case META_PREF_ATTACH_MODAL_DIALOGS:
      return "ATTACH_MODAL_DIALOGS";

    case META_PREF_RAISE_ON_CLICK:
      return "RAISE_ON_CLICK";
      
    case META_PREF_THEME:
      return "THEME";

    case META_PREF_TITLEBAR_FONT:
      return "TITLEBAR_FONT";

    case META_PREF_NUM_WORKSPACES:
      return "NUM_WORKSPACES";

    case META_PREF_APPLICATION_BASED:
      return "APPLICATION_BASED";

    case META_PREF_KEYBINDINGS:
      return "KEYBINDINGS";

    case META_PREF_DISABLE_WORKAROUNDS:
      return "DISABLE_WORKAROUNDS";

    case META_PREF_ACTION_DOUBLE_CLICK_TITLEBAR:
      return "ACTION_DOUBLE_CLICK_TITLEBAR";

    case META_PREF_ACTION_MIDDLE_CLICK_TITLEBAR:
      return "ACTION_MIDDLE_CLICK_TITLEBAR";

    case META_PREF_ACTION_RIGHT_CLICK_TITLEBAR:
      return "ACTION_RIGHT_CLICK_TITLEBAR";

    case META_PREF_ACTION_SCROLL_WHEEL_TITLEBAR:
      return "ACTION_SCROLL_WHEEL_TITLEBAR";

    case META_PREF_AUTO_RAISE:
      return "AUTO_RAISE";
      
    case META_PREF_AUTO_RAISE_DELAY:
      return "AUTO_RAISE_DELAY";

    case META_PREF_BUTTON_LAYOUT:
      return "BUTTON_LAYOUT";

    case META_PREF_WORKSPACE_NAMES:
      return "WORKSPACE_NAMES";

    case META_PREF_GNOME_ANIMATIONS:
      return "GNOME_ANIMATIONS";

    case META_PREF_CURSOR_THEME:
      return "CURSOR_THEME";

    case META_PREF_CURSOR_SIZE:
      return "CURSOR_SIZE";

    case META_PREF_RESIZE_WITH_RIGHT_BUTTON:
      return "RESIZE_WITH_RIGHT_BUTTON";

    case META_PREF_EDGE_TILING:
      return "EDGE_TILING";

    case META_PREF_EDGE_RESISTANCE_WINDOW:
      return "EDGE_RESISTANCE_WINDOW";

    case META_PREF_FORCE_FULLSCREEN:
      return "FORCE_FULLSCREEN";

    case META_PREF_WORKSPACES_ONLY_ON_PRIMARY:
      return "WORKSPACES_ONLY_ON_PRIMARY";

    case META_PREF_WORKSPACE_CYCLE:
      return "WORKSPACE_CYCLE";

    case META_PREF_VISUAL_BELL:
      return "VISUAL_BELL";

    case META_PREF_AUDIBLE_BELL:
      return "AUDIBLE_BELL";

    case META_PREF_VISUAL_BELL_TYPE:
      return "VISUAL_BELL_TYPE";

    case META_PREF_DRAGGABLE_BORDER_WIDTH:
      return "DRAGGABLE_BORDER_WIDTH";

    case META_PREF_TILE_HUD_THRESHOLD:
      return "TILE_HUD_THRESHOLD";

    case META_PREF_RESIZE_THRESHOLD:
      return "RESIZE_THRESHOLD";

    case META_PREF_DYNAMIC_WORKSPACES:
      return "DYNAMIC_WORKSPACES";

    case META_PREF_UNREDIRECT_FULLSCREEN_WINDOWS:
      return "UNREDIRECT_FULLSCREEN_WINDOWS";

    case META_PREF_SNAP_MODIFIER:
      return "SNAP_MODIFIER";

    case META_PREF_LEGACY_SNAP:
      return "LEGACY_SNAP";

    case META_PREF_INVERT_WORKSPACE_FLIP_DIRECTION:
      return "INVERT_WORKSPACE_FLIP_DIRECTION";

    case META_PREF_TILE_MAXIMIZE:
      return "TILE_MAXIMIZE";

    case META_PREF_PLACEMENT_MODE:
      return "PLACEMENT_MODE";

    case META_PREF_MIN_WIN_OPACITY:
      return "MIN_WIN_OPACITY";
    }

  return "(unknown)";
}
#endif /* WITH_VERBOSE_MODE */

void
meta_prefs_set_num_workspaces (int n_workspaces)
{
  MetaBasePreference *pref = NULL;

  find_pref (preferences_int, sizeof(MetaIntPreference),
             KEY_NUM_WORKSPACES, &pref);

  g_settings_set_int (SETTINGS (pref->schema),
                      KEY_NUM_WORKSPACES,
                      n_workspaces);
}

static GHashTable *key_bindings;

static void
meta_key_pref_free (MetaKeyPref *pref)
{
  update_binding (pref, NULL);

  g_free (pref->name);
  g_free (pref->schema);

  g_free (pref);
}

static void
init_bindings (void)
{
  key_bindings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify)meta_key_pref_free);
}

static void
init_workspace_names (void)
{
  update_workspace_names ();
}

static gboolean
update_binding (MetaKeyPref *binding,
                gchar      **strokes)
{
  unsigned int keysym;
  unsigned int keycode;
  MetaVirtualModifier mods;
  MetaKeyCombo *combo;
  int i;

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Binding \"%s\" has new GSettings value\n",
              binding->name);

  /* Okay, so, we're about to provide a new list of key combos for this
   * action. Delete any pre-existing list.
   */
  g_slist_foreach (binding->bindings, (GFunc) g_free, NULL);
  g_slist_free (binding->bindings);
  binding->bindings = NULL;
  
  for (i = 0; strokes && strokes[i]; i++)
    {
      keysym = 0;
      keycode = 0;
      mods = 0;

      if (!meta_ui_parse_accelerator (strokes[i], &keysym, &keycode, &mods))
        {
          meta_topic (META_DEBUG_KEYBINDINGS,
                      "Failed to parse new GSettings value\n");
          meta_warning (_("\"%s\" found in configuration database is not a valid value for keybinding \"%s\"\n"),
                        strokes[i], binding->name);

          /* Value is kept and will thus be removed next time we save the key.
           * Changing the key in response to a modification could lead to cyclic calls. */
          continue;
        }

      /* Bug 329676: Bindings which can be shifted must not have no modifiers,
       * nor only SHIFT as a modifier.
       */

      if (binding->add_shift &&
          0 != keysym &&
          (META_VIRTUAL_SHIFT_MASK == mods || 0 == mods))
        {
          meta_warning ("Cannot bind \"%s\" to %s: it needs a modifier "
                        "such as Ctrl or Alt.\n",
                        binding->name, strokes[i]);

          /* Value is kept and will thus be removed next time we save the key.
           * Changing the key in response to a modification could lead to cyclic calls. */
          continue;
        }

      combo = g_malloc0 (sizeof (MetaKeyCombo));
      combo->keysym = keysym;
      combo->keycode = keycode;
      combo->modifiers = mods;
      binding->bindings = g_slist_prepend (binding->bindings, combo);

      meta_topic (META_DEBUG_KEYBINDINGS,
                      "New keybinding for \"%s\" is keysym = 0x%x keycode = 0x%x mods = 0x%x\n",
                      binding->name, keysym, keycode, mods);
    }
  return TRUE;
}

static gboolean
update_key_binding (const char *key,
                    gchar     **strokes)
{
  MetaKeyPref *pref = g_hash_table_lookup (key_bindings, key);

  if (pref)
    return update_binding (pref, strokes);
  else
    return FALSE;
}

static gboolean
update_workspace_names (void)
{
  int i;
  char **names;
  int n_workspace_names, n_names;
  gboolean changed = FALSE;

  names = g_settings_get_strv (SETTINGS (SCHEMA_GENERAL), KEY_WORKSPACE_NAMES);
  n_names = g_strv_length (names);
  n_workspace_names = workspace_names ? g_strv_length (workspace_names) : 0;

  for (i = 0; i < n_names; i++)
    if (n_workspace_names < i + 1 || !workspace_names[i] ||
        g_strcmp0 (names[i], workspace_names[i]) != 0)
      {
        changed = TRUE;
        break;
      }

  if (n_workspace_names != n_names)
    changed = TRUE;

  if (changed)
    {
      if (workspace_names)
        g_strfreev (workspace_names);
      workspace_names = names;
    }
  else
    g_strfreev (names);

  return changed;
}

static void
update_min_win_opacity (void)
{
  int pct;
  int mapped;

  pct = g_settings_get_int (SETTINGS (SCHEMA_GENERAL), KEY_MIN_WINDOW_OPACITY);
  mapped = (int)(((double)pct / 100.0) * 255.0);

  min_window_opacity = CLAMP (mapped, 0, 255);
}

const char*
meta_prefs_get_workspace_name (int i)
{
  const char *name;

  if (!workspace_names ||
      g_strv_length (workspace_names) < (guint)i + 1 ||
      !*workspace_names[i])
    {
      char *generated_name = g_strdup_printf (_("Workspace %d"), i + 1);
      name = g_intern_string (generated_name);
      g_free (generated_name);
    }
  else
    name = workspace_names[i];

  meta_topic (META_DEBUG_PREFS,
              "Getting name of workspace %d: \"%s\"\n", i, name);

  return name;
}

void
meta_prefs_change_workspace_name (int         num,
                                  const char *name)
{
  GVariantBuilder builder;
  int n_workspace_names, i;
  
  g_return_if_fail (num >= 0);

  meta_topic (META_DEBUG_PREFS,
              "Changing name of workspace %d to %s\n",
              num, name ? name : "none");

  /* NULL and empty string both mean "default" here,
   * and we also need to match the name against its default value
   * to avoid saving it literally. */
  if (g_strcmp0 (name, meta_prefs_get_workspace_name (num)) == 0)
    {
      if (!name || !*name)
        meta_topic (META_DEBUG_PREFS,
                    "Workspace %d already uses default name\n", num);
      else
        meta_topic (META_DEBUG_PREFS,
                    "Workspace %d already has name %s\n", num, name);
      return;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_STRING_ARRAY);
  n_workspace_names = workspace_names ? g_strv_length (workspace_names) : 0;

  for (i = 0; i < MAX (num + 1, n_workspace_names); i++)
    {
      const char *value;

      if (i == num)
        value = name ? name : "";
      else if (i < n_workspace_names)
        value = workspace_names[i] ? workspace_names[i] : "";
      else
        value = "";

      g_variant_builder_add (&builder, "s", value);
    }

  g_settings_set_value (SETTINGS (SCHEMA_GENERAL), KEY_WORKSPACE_NAMES,
                        g_variant_builder_end (&builder));
}

/**
 * meta_prefs_get_button_layout:
 * @button_layout: (out):
 */
void
meta_prefs_get_button_layout (MetaButtonLayout *button_layout_p)
{
  *button_layout_p = button_layout;
}

LOCAL_SYMBOL gboolean
meta_prefs_add_keybinding (const char           *name,
                           const char           *schema,
                           MetaKeyBindingAction  action,
                           MetaKeyBindingFlags   flags)
{
  MetaKeyPref  *pref;
  GSettings    *settings;
  char        **strokes;

  if (g_hash_table_lookup (key_bindings, name))
    {
      meta_warning ("Trying to re-add keybinding \"%s\".\n", name);
      return FALSE;
    }

  settings = SETTINGS (schema);
  if (settings == NULL)
    {
      settings = g_settings_new (schema);
      if ((flags & META_KEY_BINDING_BUILTIN) != 0)
        g_signal_connect (settings, "changed",
                          G_CALLBACK (bindings_changed), NULL);
      g_hash_table_insert (settings_schemas, g_strdup (schema), settings);
    }

  pref = g_new0 (MetaKeyPref, 1);
  pref->name = g_strdup (name);
  pref->schema = g_strdup (schema);
  pref->action = action;
  pref->bindings = NULL;
  pref->add_shift = (flags & META_KEY_BINDING_REVERSES) != 0;
  pref->per_window = (flags & META_KEY_BINDING_PER_WINDOW) != 0;
  pref->builtin = (flags & META_KEY_BINDING_BUILTIN) != 0;

  strokes = g_settings_get_strv (settings, name);
  update_binding (pref, strokes);
  g_strfreev (strokes);

  g_hash_table_insert (key_bindings, g_strdup (name), pref);

  if (!pref->builtin)
    {
      guint id;
      char *changed_signal = g_strdup_printf ("changed::%s", name);
      id = g_signal_connect (settings, changed_signal,
                             G_CALLBACK (bindings_changed), NULL);
      g_free (changed_signal);

      g_object_set_data (G_OBJECT (settings), name, GUINT_TO_POINTER (id));

      queue_changed (META_PREF_KEYBINDINGS);
    }

  return TRUE;
}

LOCAL_SYMBOL gboolean
meta_prefs_remove_keybinding (const char *name)
{
  MetaKeyPref *pref;
  GSettings   *settings;
  guint        id;

  pref = g_hash_table_lookup (key_bindings, name);
  if (!pref)
    {
      meta_warning ("Trying to remove non-existent keybinding \"%s\".\n", name);
      return FALSE;
    }

  if (pref->builtin)
    {
      meta_warning ("Trying to remove builtin keybinding \"%s\".\n", name);
      return FALSE;
    }

  settings = SETTINGS (pref->schema);
  id = GPOINTER_TO_UINT (g_object_steal_data (G_OBJECT (settings), name));
  g_signal_handler_disconnect (settings, id);

  g_hash_table_remove (key_bindings, name);

  queue_changed (META_PREF_KEYBINDINGS);

  return TRUE;
}

LOCAL_SYMBOL gboolean
meta_prefs_add_custom_keybinding (const char           *name,
                                  const char          **bindings,
                                  MetaKeyBindingAction  action,
                                  MetaKeyBindingFlags   flags)
{
  MetaKeyPref  *pref;


  if (g_hash_table_lookup (key_bindings, name))
    {
      meta_warning ("Trying to re-add custom keybinding \"%s\".\n", name);
      return FALSE;
    }

  pref = g_new0 (MetaKeyPref, 1);
  pref->name = g_strdup (name);
  pref->schema = NULL;
  pref->action = action;
  pref->bindings = NULL;
  pref->add_shift = (flags & META_KEY_BINDING_REVERSES) != 0;
  pref->per_window = (flags & META_KEY_BINDING_PER_WINDOW) != 0;
  pref->builtin = (flags & META_KEY_BINDING_BUILTIN) != 0;

  update_binding (pref, (gchar **)bindings);

  g_hash_table_insert (key_bindings, g_strdup (name), pref);

  return TRUE;
}

LOCAL_SYMBOL gboolean
meta_prefs_remove_custom_keybinding (const char *name)
{
  MetaKeyPref *pref;

  pref = g_hash_table_lookup (key_bindings, name);
  if (!pref)
    {
      meta_warning ("Trying to remove non-existent custom keybinding \"%s\".\n", name);
      return FALSE;
    }

  g_hash_table_remove (key_bindings, name);

  queue_changed (META_PREF_KEYBINDINGS);

  return TRUE;
}

/**
 * meta_prefs_get_keybindings:
 * 
 * Returns: (element-type MetaKeyPref) (transfer container):
 */
GList *
meta_prefs_get_keybindings ()
{
  return g_hash_table_get_values (key_bindings);
}

CDesktopTitlebarAction
meta_prefs_get_action_double_click_titlebar (void)
{
  return action_double_click_titlebar;
}

CDesktopTitlebarAction
meta_prefs_get_action_middle_click_titlebar (void)
{
  return action_middle_click_titlebar;
}

CDesktopTitlebarAction
meta_prefs_get_action_right_click_titlebar (void)
{
  return action_right_click_titlebar;
}

CDesktopTitlebarScrollAction
meta_prefs_get_action_scroll_wheel_titlebar (void)
{
  return action_scroll_titlebar;
}

gboolean
meta_prefs_get_auto_raise (void)
{
  return auto_raise;
}

int
meta_prefs_get_auto_raise_delay (void)
{
  return auto_raise_delay;
}

gboolean
meta_prefs_get_gnome_animations ()
{
  return gnome_animations;
}

gboolean
meta_prefs_get_edge_tiling ()
{
  return edge_tiling;
}

gboolean
meta_prefs_get_edge_resistance_window ()
{
  return edge_resistance_window;
}

MetaKeyBindingAction
meta_prefs_get_keybinding_action (const char *name)
{
  MetaKeyPref *pref = g_hash_table_lookup (key_bindings, name);

  return pref ? pref->action
              : META_KEYBINDING_ACTION_NONE;
}

/* This is used by the menu system to decide what key binding
 * to display next to an option. We return the first non-disabled
 * binding, if any.
 */
void
meta_prefs_get_window_binding (const char          *name,
                               unsigned int        *keysym,
                               MetaVirtualModifier *modifiers)
{
  MetaKeyPref *pref = g_hash_table_lookup (key_bindings, name);

  if (pref->per_window)
    {
      GSList *s = pref->bindings;

      while (s)
        {
          MetaKeyCombo *c = s->data;

          if (c->keysym != 0 || c->modifiers != 0)
            {
              *keysym = c->keysym;
              *modifiers = c->modifiers;
              return;
            }

          s = s->next;
        }

      /* Not found; return the disabled value */
      *keysym = *modifiers = 0;
      return;
    }

  g_assert_not_reached ();
}

guint
meta_prefs_get_mouse_button_resize (void)
{
  return resize_with_right_button ? 3: 2;
}

guint
meta_prefs_get_mouse_button_menu (void)
{
  return resize_with_right_button ? 2: 3;
}

gboolean
meta_prefs_get_force_fullscreen (void)
{
  return force_fullscreen;
}

gboolean
meta_prefs_get_workspaces_only_on_primary (void)
{
  return workspaces_only_on_primary;
}

gboolean
meta_prefs_get_legacy_snap (void)
{
  return legacy_snap;
}

int
meta_prefs_get_draggable_border_width (void)
{
  return draggable_border_width * ui_scale;
}

int
meta_prefs_get_tile_hud_threshold (void)
{
  return tile_hud_threshold * ui_scale;
}

int
meta_prefs_get_resize_threshold (void)
{
  return resize_threshold * ui_scale;
}

void
meta_prefs_set_force_fullscreen (gboolean whether)
{
  force_fullscreen = whether;
}

unsigned int *
meta_prefs_get_snap_modifier (void)
{
    return snap_modifier;
}

gboolean
meta_prefs_get_invert_flip_direction (void)
{
    return invert_workspace_flip;
}

gboolean
meta_prefs_get_tile_maximize (void)
{
    return tile_maximize;
}

MetaPlacementMode
meta_prefs_get_placement_mode (void)
{
  return placement_mode;
}

gint
meta_prefs_get_min_win_opacity (void)
{
  return min_window_opacity;
}

gint
meta_prefs_get_ui_scale (void)
{
  return ui_scale;
}
