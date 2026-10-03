/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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
 */

/**
 * SECTION:clutter-main
 * @short_description: Various 'global' Clutter functions.
 *
 * Functions to retrieve various global Clutter resources and other utility
 * functions for mainloops, events and threads
 *
 * ## The Clutter Threading Model
 *
 * Clutter is *thread-aware*: all operations performed by Clutter are assumed
 * to be under the Big Clutter Lock, which is created when the threading is
 * initialized through clutter_init(), and entered when calling user-related
 * code during event handling and actor drawing.
 *
 * The only safe and portable way to use the Clutter API in a multi-threaded
 * environment is to only access the Clutter API from a thread that did called
 * clutter_init() and clutter_main().
 *
 * The common pattern for using threads with Clutter is to use worker threads
 * to perform blocking operations and then install idle or timeout sources with
 * the result when the thread finishes, and update the UI from those callbacks.
 *
 * For a working example of how to use a worker thread to update the UI, see
 * [threads.c](https://git.gnome.org/browse/clutter/tree/examples/threads.c?h=clutter-1.18)
 */

#include "clutter-build-config.h"

#include <stdlib.h>

#include "clutter-actor-private.h"
#include "clutter-backend-private.h"
#include "clutter-config.h"
#include "clutter-debug.h"
#include "clutter-event-private.h"
#include "clutter-feature.h"
#include "clutter-input-device-private.h"
#include "clutter-input-pointer-a11y-private.h"
#include "clutter-graphene.h"
#include "clutter-main.h"
#include "clutter-master-clock.h"
#include "clutter-muffin.h"
#include "clutter-paint-node-private.h"
#include "clutter-private.h"
#include "clutter-settings-private.h"
#include "clutter-stage-manager.h"
#include "clutter-stage-private.h"

#ifdef CLUTTER_WINDOWING_X11
#include "x11/clutter-backend-x11.h"
#endif
#ifdef CLUTTER_WINDOWING_EGL
#include "egl/clutter-backend-eglnative.h"
#endif

#include <cogl/cogl.h>
#include <cogl-pango/cogl-pango.h>

#include "cally/cally.h" /* For accessibility support */

/* main context */
static ClutterMainContext *ClutterCntx       = NULL;
G_LOCK_DEFINE_STATIC (ClutterCntx);

/* main lock and locking/unlocking functions */
static GMutex clutter_threads_mutex;

/* command line options */
static gboolean clutter_is_initialized       = FALSE;
static gboolean clutter_show_fps             = FALSE;
static gboolean clutter_fatal_warnings       = FALSE;
static gboolean clutter_disable_mipmap_text  = FALSE;
static gboolean clutter_use_fuzzy_picking    = FALSE;
static gboolean clutter_enable_accessibility = TRUE;
static gboolean clutter_sync_to_vblank       = TRUE;

static guint clutter_default_fps             = 60;

static ClutterTextDirection clutter_text_direction = CLUTTER_TEXT_DIRECTION_LTR;

static guint clutter_main_loop_level         = 0;
static GSList *main_loops                    = NULL;

/* debug flags */
guint clutter_debug_flags       = 0;
guint clutter_paint_debug_flags = 0;
guint clutter_pick_debug_flags  = 0;

#ifdef CLUTTER_ENABLE_DEBUG
static const GDebugKey clutter_debug_keys[] = {
  { "misc", CLUTTER_DEBUG_MISC },
  { "actor", CLUTTER_DEBUG_ACTOR },
  { "texture", CLUTTER_DEBUG_TEXTURE },
  { "event", CLUTTER_DEBUG_EVENT },
  { "paint", CLUTTER_DEBUG_PAINT },
  { "pick", CLUTTER_DEBUG_PICK },
  { "pango", CLUTTER_DEBUG_PANGO },
  { "backend", CLUTTER_DEBUG_BACKEND },
  { "scheduler", CLUTTER_DEBUG_SCHEDULER },
  { "script", CLUTTER_DEBUG_SCRIPT },
  { "shader", CLUTTER_DEBUG_SHADER },
  { "animation", CLUTTER_DEBUG_ANIMATION },
  { "layout", CLUTTER_DEBUG_LAYOUT },
  { "clipping", CLUTTER_DEBUG_CLIPPING },
  { "oob-transforms", CLUTTER_DEBUG_OOB_TRANSFORMS },
};
#endif /* CLUTTER_ENABLE_DEBUG */

static const GDebugKey clutter_pick_debug_keys[] = {
  { "nop-picking", CLUTTER_DEBUG_NOP_PICKING },
};

static const GDebugKey clutter_paint_debug_keys[] = {
  { "disable-swap-events", CLUTTER_DEBUG_DISABLE_SWAP_EVENTS },
  { "disable-clipped-redraws", CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS },
  { "redraws", CLUTTER_DEBUG_REDRAWS },
  { "paint-volumes", CLUTTER_DEBUG_PAINT_VOLUMES },
  { "disable-culling", CLUTTER_DEBUG_DISABLE_CULLING },
  { "disable-offscreen-redirect", CLUTTER_DEBUG_DISABLE_OFFSCREEN_REDIRECT },
  { "continuous-redraw", CLUTTER_DEBUG_CONTINUOUS_REDRAW },
  { "paint-deform-tiles", CLUTTER_DEBUG_PAINT_DEFORM_TILES },
  { "damage-region", CLUTTER_DEBUG_PAINT_DAMAGE_REGION },
};

static inline void
clutter_threads_init_default (void)
{
  g_mutex_init (&clutter_threads_mutex);
}

#define ENVIRONMENT_GROUP       "Environment"
#define DEBUG_GROUP             "Debug"

static void
clutter_config_read_from_key_file (GKeyFile *keyfile)
{
  GError *key_error = NULL;
  gboolean bool_value;
  gint int_value;
  gchar *str_value;

  if (!g_key_file_has_group (keyfile, ENVIRONMENT_GROUP))
    return;

  str_value =
    g_key_file_get_string (keyfile, ENVIRONMENT_GROUP,
                           "Drivers",
                           &key_error);
  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    clutter_set_allowed_drivers (str_value);

  g_free (str_value);

  bool_value =
    g_key_file_get_boolean (keyfile, ENVIRONMENT_GROUP,
                            "ShowFps",
                            &key_error);

  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    clutter_show_fps = bool_value;

  bool_value =
    g_key_file_get_boolean (keyfile, ENVIRONMENT_GROUP,
                            "DisableMipmappedText",
                            &key_error);

  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    clutter_disable_mipmap_text = bool_value;

  bool_value =
    g_key_file_get_boolean (keyfile, ENVIRONMENT_GROUP,
                            "UseFuzzyPicking",
                            &key_error);

  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    clutter_use_fuzzy_picking = bool_value;

  bool_value =
    g_key_file_get_boolean (keyfile, ENVIRONMENT_GROUP,
                            "EnableAccessibility",
                            &key_error);

  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    clutter_enable_accessibility = bool_value;

  int_value =
    g_key_file_get_integer (keyfile, ENVIRONMENT_GROUP,
                            "DefaultFps",
                            &key_error);

  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    clutter_default_fps = int_value;

  str_value =
    g_key_file_get_string (keyfile, ENVIRONMENT_GROUP,
                           "TextDirection",
                           &key_error);

  if (key_error != NULL)
    g_clear_error (&key_error);
  else
    {
      if (g_strcmp0 (str_value, "rtl") == 0)
        clutter_text_direction = CLUTTER_TEXT_DIRECTION_RTL;
      else
        clutter_text_direction = CLUTTER_TEXT_DIRECTION_LTR;
    }

  g_free (str_value);
}

#ifdef CLUTTER_ENABLE_DEBUG
static void
clutter_debug_read_from_key_file (GKeyFile *keyfile)
{
  GError *key_error = NULL;
  gchar *value;

  if (!g_key_file_has_group (keyfile, DEBUG_GROUP))
    return;

  value = g_key_file_get_value (keyfile, DEBUG_GROUP,
                                "Debug",
                                &key_error);
  if (key_error == NULL)
    {
      clutter_debug_flags |=
        g_parse_debug_string (value,
                              clutter_debug_keys,
                              G_N_ELEMENTS (clutter_debug_keys));
    }
  else
    g_clear_error (&key_error);

  g_free (value);

  value = g_key_file_get_value (keyfile, DEBUG_GROUP,
                                "PaintDebug",
                                &key_error);
  if (key_error == NULL)
    {
      clutter_paint_debug_flags |=
        g_parse_debug_string (value,
                              clutter_paint_debug_keys,
                              G_N_ELEMENTS (clutter_paint_debug_keys));
    }
  else
    g_clear_error (&key_error);

  g_free (value);

  value = g_key_file_get_value (keyfile, DEBUG_GROUP,
                                "PickDebug",
                                &key_error);
  if (key_error == NULL)
    {
      clutter_pick_debug_flags |=
        g_parse_debug_string (value,
                              clutter_pick_debug_keys,
                              G_N_ELEMENTS (clutter_pick_debug_keys));
    }
  else
    g_clear_error (&key_error);

  g_free (value);
}
#endif

static void
clutter_config_read_from_file (const gchar *config_path)
{
  ClutterSettings *settings = clutter_settings_get_default ();
  GKeyFile *key_file = g_key_file_new ();
  GError *error = NULL;

  g_key_file_load_from_file (key_file, config_path, G_KEY_FILE_NONE, &error);
  if (error == NULL)
    {
      CLUTTER_NOTE (MISC, "Reading configuration from '%s'", config_path);

      clutter_config_read_from_key_file (key_file);
#ifdef CLUTTER_ENABLE_DEBUG
      clutter_debug_read_from_key_file (key_file);
#endif
      _clutter_settings_read_from_key_file (settings, key_file);
    }
  else
    {
      g_warning ("Unable to read configuration settings from '%s': %s",
                 config_path,
                 error->message);
      g_error_free (error);
    }

  g_key_file_free (key_file);
}

static void
clutter_config_read (void)
{
  gchar *config_path;

  config_path = g_build_filename (CLUTTER_SYSCONFDIR,
                                  "clutter-1.0",
                                  "settings.ini",
                                  NULL);
  if (g_file_test (config_path, G_FILE_TEST_EXISTS))
    clutter_config_read_from_file (config_path);

  g_free (config_path);

  config_path = g_build_filename (g_get_user_config_dir (),
                                  "clutter-1.0",
                                  "settings.ini",
                                  NULL);
  if (g_file_test (config_path, G_FILE_TEST_EXISTS))
    clutter_config_read_from_file (config_path);

  g_free (config_path);
}

gboolean
_clutter_context_get_show_fps (void)
{
  ClutterMainContext *context = _clutter_context_get_default ();

  return context->show_fps;
}

/**
 * clutter_get_accessibility_enabled:
 *
 * Returns whether Clutter has accessibility support enabled.  As
 * least, a value of TRUE means that there are a proper AtkUtil
 * implementation available
 *
 * Return value: %TRUE if Clutter has accessibility support enabled
 *
 * Since: 1.4
 */
gboolean
clutter_get_accessibility_enabled (void)
{
  return cally_get_cally_initialized ();
}

/**
 * clutter_disable_accessibility:
 *
 * Disable loading the accessibility support. It has the same effect
 * as setting the environment variable
 * CLUTTER_DISABLE_ACCESSIBILITY. For the same reason, this method
 * should be called before clutter_init().
 *
 * Since: 1.14
 */
void
clutter_disable_accessibility (void)
{
  if (clutter_is_initialized)
    {
      g_warning ("clutter_disable_accessibility() can only be called before "
                 "initializing Clutter.");
      return;
    }

  clutter_enable_accessibility = FALSE;
}

static CoglPangoFontMap *
clutter_context_get_pango_fontmap (void)
{
  ClutterMainContext *self;
  CoglPangoFontMap *font_map;
  gdouble resolution;
  gboolean use_mipmapping;

  self = _clutter_context_get_default ();
  if (G_LIKELY (self->font_map != NULL))
    return self->font_map;

  font_map = COGL_PANGO_FONT_MAP (cogl_pango_font_map_new ());

  resolution = clutter_backend_get_resolution (self->backend);
  cogl_pango_font_map_set_resolution (font_map, resolution);

  use_mipmapping = !clutter_disable_mipmap_text;
  cogl_pango_font_map_set_use_mipmapping (font_map, use_mipmapping);

  self->font_map = font_map;

  return self->font_map;
}

static ClutterTextDirection
clutter_get_text_direction (void)
{
  ClutterTextDirection dir = CLUTTER_TEXT_DIRECTION_LTR;
  const gchar *direction;

  direction = g_getenv ("CLUTTER_TEXT_DIRECTION");
  if (direction && *direction != '\0')
    {
      if (strcmp (direction, "rtl") == 0)
        dir = CLUTTER_TEXT_DIRECTION_RTL;
      else if (strcmp (direction, "ltr") == 0)
        dir = CLUTTER_TEXT_DIRECTION_LTR;
    }
  else
    {
      /* Re-use GTK+'s LTR/RTL handling */
      const char *e = g_dgettext ("gtk30", "default:LTR");

      if (strcmp (e, "default:RTL") == 0)
        dir = CLUTTER_TEXT_DIRECTION_RTL;
      else if (strcmp (e, "default:LTR") == 0)
        dir = CLUTTER_TEXT_DIRECTION_LTR;
      else
        g_warning ("Whoever translated default:LTR did so wrongly.");
    }

  CLUTTER_NOTE (MISC, "Text direction: %s",
                dir == CLUTTER_TEXT_DIRECTION_RTL ? "rtl" : "ltr");

  return dir;
}

/**
 * clutter_main_quit:
 *
 * Terminates the Clutter mainloop.
 */
void
clutter_main_quit (void)
{
  if (main_loops == NULL)
    {
      g_critical ("Calling clutter_main_quit() without calling clutter_main() "
                  "is not allowed. If you are using another main loop, use the "
                  "appropriate API to terminate it.");
      return;
    }

  CLUTTER_NOTE (MISC, "Terminating main loop level %d", clutter_main_loop_level);

  g_main_loop_quit (main_loops->data);
}

/**
 * clutter_main_level:
 *
 * Retrieves the depth of the Clutter mainloop.
 *
 * Return value: The level of the mainloop.
 */
gint
clutter_main_level (void)
{
  return clutter_main_loop_level;
}

/**
 * clutter_main:
 *
 * Starts the Clutter mainloop.
 */
void
clutter_main (void)
{
  GMainLoop *loop;

  if (!_clutter_context_is_initialized ())
    {
      g_warning ("Called clutter_main() but Clutter wasn't initialised. "
		 "You must call clutter_init() first.");
      return;
    }

  clutter_main_loop_level++;

  CLUTTER_NOTE (MISC, "Entering main loop level %d", clutter_main_loop_level);

  loop = g_main_loop_new (NULL, TRUE);
  main_loops = g_slist_prepend (main_loops, loop);

  if (g_main_loop_is_running (main_loops->data))
    {
      _clutter_threads_release_lock ();
      g_main_loop_run (loop);
      _clutter_threads_acquire_lock ();
    }

  main_loops = g_slist_remove (main_loops, loop);

  g_main_loop_unref (loop);

  CLUTTER_NOTE (MISC, "Leaving main loop level %d", clutter_main_loop_level);

  clutter_main_loop_level--;
}

gboolean
_clutter_threads_dispatch (gpointer data)
{
  ClutterThreadsDispatch *dispatch = data;
  gboolean ret = FALSE;

  _clutter_threads_acquire_lock ();

  if (!g_source_is_destroyed (g_main_current_source ()))
    ret = dispatch->func (dispatch->data);

  _clutter_threads_release_lock ();

  return ret;
}

void
_clutter_threads_dispatch_free (gpointer data)
{
  ClutterThreadsDispatch *dispatch = data;

  /* XXX - we cannot hold the thread lock here because the main loop
   * might destroy a source while still in the dispatcher function; so
   * knowing whether the lock is being held or not is not known a priori.
   *
   * see bug: http://bugzilla.gnome.org/show_bug.cgi?id=459555
   */
  if (dispatch->notify)
    dispatch->notify (dispatch->data);

  g_slice_free (ClutterThreadsDispatch, dispatch);
}

/**
 * clutter_threads_add_idle_full: (rename-to clutter_threads_add_idle)
 * @priority: the priority of the timeout source. Typically this will be in the
 *    range between #G_PRIORITY_DEFAULT_IDLE and #G_PRIORITY_HIGH_IDLE
 * @func: function to call
 * @data: data to pass to the function
 * @notify: functio to call when the idle source is removed
 *
 * Adds a function to be called whenever there are no higher priority
 * events pending. If the function returns %FALSE it is automatically
 * removed from the list of event sources and will not be called again.
 *
 * This function can be considered a thread-safe variant of g_idle_add_full():
 * it will call @function while holding the Clutter lock. It is logically
 * equivalent to the following implementation:
 *
 * |[
 * static gboolean
 * idle_safe_callback (gpointer data)
 * {
 *    SafeClosure *closure = data;
 *    gboolean res = FALSE;
 *
 *    // the callback does not need to acquire the Clutter
 *     / lock itself, as it is held by the this proxy handler
 *     //
 *    res = closure->callback (closure->data);
 *
 *    return res;
 * }
 * static gulong
 * add_safe_idle (GSourceFunc callback,
 *                gpointer    data)
 * {
 *   SafeClosure *closure = g_new0 (SafeClosure, 1);
 *
 *   closure->callback = callback;
 *   closure->data = data;
 *
 *   return g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
 *                           idle_safe_callback,
 *                           closure,
 *                           g_free)
 * }
 *]|
 *
 * This function should be used by threaded applications to make sure
 * that @func is emitted under the Clutter threads lock and invoked
 * from the same thread that started the Clutter main loop. For instance,
 * it can be used to update the UI using the results from a worker
 * thread:
 *
 * |[
 * static gboolean
 * update_ui (gpointer data)
 * {
 *   SomeClosure *closure = data;
 *
 *   // it is safe to call Clutter API from this function because
 *    / it is invoked from the same thread that started the main
 *    / loop and under the Clutter thread lock
 *    //
 *   clutter_label_set_text (CLUTTER_LABEL (closure->label),
 *                           closure->text);
 *
 *   g_object_unref (closure->label);
 *   g_free (closure);
 *
 *   return FALSE;
 * }
 *
 *   // within another thread //
 *   closure = g_new0 (SomeClosure, 1);
 *   // always take a reference on GObject instances //
 *   closure->label = g_object_ref (my_application->label);
 *   closure->text = g_strdup (processed_text_to_update_the_label);
 *
 *   clutter_threads_add_idle_full (G_PRIORITY_HIGH_IDLE,
 *                                  update_ui,
 *                                  closure,
 *                                  NULL);
 * ]|
 *
 * Return value: the ID (greater than 0) of the event source.
 *
 * Since: 0.4
 */
guint
clutter_threads_add_idle_full (gint           priority,
                               GSourceFunc    func,
                               gpointer       data,
                               GDestroyNotify notify)
{
  ClutterThreadsDispatch *dispatch;

  g_return_val_if_fail (func != NULL, 0);

  dispatch = g_slice_new (ClutterThreadsDispatch);
  dispatch->func = func;
  dispatch->data = data;
  dispatch->notify = notify;

  return g_idle_add_full (priority,
                          _clutter_threads_dispatch, dispatch,
                          _clutter_threads_dispatch_free);
}

/**
 * clutter_threads_add_idle: (skip)
 * @func: function to call
 * @data: data to pass to the function
 *
 * Simple wrapper around clutter_threads_add_idle_full() using the
 * default priority.
 *
 * Return value: the ID (greater than 0) of the event source.
 *
 * Since: 0.4
 */
guint
clutter_threads_add_idle (GSourceFunc func,
                          gpointer    data)
{
  g_return_val_if_fail (func != NULL, 0);

  return clutter_threads_add_idle_full (G_PRIORITY_DEFAULT_IDLE,
                                        func, data,
                                        NULL);
}

/**
 * clutter_threads_add_timeout_full: (rename-to clutter_threads_add_timeout)
 * @priority: the priority of the timeout source. Typically this will be in the
 *            range between #G_PRIORITY_DEFAULT and #G_PRIORITY_HIGH.
 * @interval: the time between calls to the function, in milliseconds
 * @func: function to call
 * @data: data to pass to the function
 * @notify: function to call when the timeout source is removed
 *
 * Sets a function to be called at regular intervals holding the Clutter
 * threads lock, with the given priority. The function is called repeatedly
 * until it returns %FALSE, at which point the timeout is automatically
 * removed and the function will not be called again. The @notify function
 * is called when the timeout is removed.
 *
 * The first call to the function will be at the end of the first @interval.
 *
 * It is important to note that, due to how the Clutter main loop is
 * implemented, the timing will not be accurate and it will not try to
 * "keep up" with the interval.
 *
 * See also clutter_threads_add_idle_full().
 *
 * Return value: the ID (greater than 0) of the event source.
 *
 * Since: 0.4
 */
guint
clutter_threads_add_timeout_full (gint           priority,
                                  guint          interval,
                                  GSourceFunc    func,
                                  gpointer       data,
                                  GDestroyNotify notify)
{
  ClutterThreadsDispatch *dispatch;

  g_return_val_if_fail (func != NULL, 0);

  dispatch = g_slice_new (ClutterThreadsDispatch);
  dispatch->func = func;
  dispatch->data = data;
  dispatch->notify = notify;

  return g_timeout_add_full (priority,
                             interval,
                             _clutter_threads_dispatch, dispatch,
                             _clutter_threads_dispatch_free);
}

/**
 * clutter_threads_add_timeout: (skip)
 * @interval: the time between calls to the function, in milliseconds
 * @func: function to call
 * @data: data to pass to the function
 *
 * Simple wrapper around clutter_threads_add_timeout_full().
 *
 * Return value: the ID (greater than 0) of the event source.
 *
 * Since: 0.4
 */
guint
clutter_threads_add_timeout (guint       interval,
                             GSourceFunc func,
                             gpointer    data)
{
  g_return_val_if_fail (func != NULL, 0);

  return clutter_threads_add_timeout_full (G_PRIORITY_DEFAULT,
                                           interval,
                                           func, data,
                                           NULL);
}

void
_clutter_threads_acquire_lock (void)
{
  g_mutex_lock (&clutter_threads_mutex);
}

void
_clutter_threads_release_lock (void)
{
  /* we need to trylock here, in case the lock hasn't been acquired; on
   * various systems trying to release a mutex that hasn't been acquired
   * will cause a run-time error. trylock() will either fail, in which
   * case we can release the lock we own; or it will succeeds, in which
   * case we need to release the lock we just acquired. so we ignore the
   * returned value.
   *
   * see: https://bugs.gnome.org/679439
   */
  g_mutex_trylock (&clutter_threads_mutex);
  g_mutex_unlock (&clutter_threads_mutex);
}

void
_clutter_context_lock (void)
{
  G_LOCK (ClutterCntx);
}

void
_clutter_context_unlock (void)
{
  G_UNLOCK (ClutterCntx);
}

gboolean
_clutter_context_is_initialized (void)
{
  if (ClutterCntx == NULL)
    return FALSE;

  return ClutterCntx->is_initialized;
}

static ClutterMainContext *
clutter_context_get_default_unlocked (void)
{
  if (G_UNLIKELY (ClutterCntx == NULL))
    {
      ClutterMainContext *ctx;

      /* Read the configuration file, if any, before we set up the
       * whole thing, so that we can override things like the backend
       * and the driver
       */
      clutter_config_read ();

      ClutterCntx = ctx = g_new0 (ClutterMainContext, 1);

      ctx->is_initialized = FALSE;

      /* create the windowing system backend */
      ctx->backend = _clutter_create_backend ();

      /* create the default settings object, and store a back pointer to
       * the backend singleton
       */
      ctx->settings = clutter_settings_get_default ();
      _clutter_settings_set_backend (ctx->settings, ctx->backend);

      ctx->last_repaint_id = 1;
    }

  return ClutterCntx;
}

ClutterMainContext *
_clutter_context_get_default (void)
{
  ClutterMainContext *retval;

  _clutter_context_lock ();

  retval = clutter_context_get_default_unlocked ();

  _clutter_context_unlock ();

  return retval;
}

static gboolean
clutter_arg_direction_cb (const char *key,
                          const char *value,
                          gpointer    user_data)
{
  clutter_text_direction =
    (strcmp (value, "rtl") == 0) ? CLUTTER_TEXT_DIRECTION_RTL
                                 : CLUTTER_TEXT_DIRECTION_LTR;

  return TRUE;
}

#ifdef CLUTTER_ENABLE_DEBUG
static gboolean
clutter_arg_debug_cb (const char *key,
                      const char *value,
                      gpointer    user_data)
{
  clutter_debug_flags |=
    g_parse_debug_string (value,
                          clutter_debug_keys,
                          G_N_ELEMENTS (clutter_debug_keys));
  return TRUE;
}

static gboolean
clutter_arg_no_debug_cb (const char *key,
                         const char *value,
                         gpointer    user_data)
{
  clutter_debug_flags &=
    ~g_parse_debug_string (value,
                           clutter_debug_keys,
                           G_N_ELEMENTS (clutter_debug_keys));
  return TRUE;
}
#endif /* CLUTTER_ENABLE_DEBUG */

GQuark
clutter_init_error_quark (void)
{
  return g_quark_from_static_string ("clutter-init-error-quark");
}

static ClutterInitError
clutter_init_real (GError **error)
{
  ClutterMainContext *ctx;
  ClutterBackend *backend;

  /* Note, creates backend if not already existing, though parse args will
   * have likely created it
   */
  ctx = _clutter_context_get_default ();
  backend = ctx->backend;

  if (!ctx->options_parsed)
    {
      if (error)
        g_set_error (error, CLUTTER_INIT_ERROR,
                     CLUTTER_INIT_ERROR_INTERNAL,
                     "When using clutter_get_option_group_without_init() "
		     "you must parse options before calling clutter_init()");
      else
        g_critical ("When using clutter_get_option_group_without_init() "
		    "you must parse options before calling clutter_init()");

      return CLUTTER_INIT_ERROR_INTERNAL;
    }

  /*
   * Call backend post parse hooks.
   */
  if (!_clutter_backend_post_parse (backend, error))
    return CLUTTER_INIT_ERROR_BACKEND;

  /* If we are displaying the regions that would get redrawn with clipped
   * redraws enabled we actually have to disable the clipped redrawing
   * because otherwise we end up with nasty trails of rectangles everywhere.
   */
  if (clutter_paint_debug_flags & CLUTTER_DEBUG_REDRAWS)
    clutter_paint_debug_flags |= CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS;

  /* The same is true when drawing the outlines of paint volumes... */
  if (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_VOLUMES)
    {
      clutter_paint_debug_flags |=
        CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS | CLUTTER_DEBUG_DISABLE_CULLING;
    }

  if (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION)
    g_message ("Enabling damaged region");

  /* this will take care of initializing Cogl's state and
   * query the GL machinery for features
   */
  if (!_clutter_feature_init (error))
    return CLUTTER_INIT_ERROR_BACKEND;

  clutter_text_direction = clutter_get_text_direction ();

  /* Initiate event collection */
  _clutter_backend_init_events (ctx->backend);

  clutter_is_initialized = TRUE;
  ctx->is_initialized = TRUE;

  /* Initialize a11y */
  if (clutter_enable_accessibility)
    cally_accessibility_init ();

  /* Initialize types required for paint nodes */
  _clutter_paint_node_init_types ();

  return CLUTTER_INIT_SUCCESS;
}

static GOptionEntry clutter_args[] = {
  { "clutter-show-fps", 0, 0, G_OPTION_ARG_NONE, &clutter_show_fps,
    N_("Show frames per second"), NULL },
  { "clutter-default-fps", 0, 0, G_OPTION_ARG_INT, &clutter_default_fps,
    N_("Default frame rate"), "FPS" },
  { "g-fatal-warnings", 0, 0, G_OPTION_ARG_NONE, &clutter_fatal_warnings,
    N_("Make all warnings fatal"), NULL },
  { "clutter-text-direction", 0, 0, G_OPTION_ARG_CALLBACK,
    clutter_arg_direction_cb,
    N_("Direction for the text"), "DIRECTION" },
  { "clutter-disable-mipmapped-text", 0, 0, G_OPTION_ARG_NONE,
    &clutter_disable_mipmap_text,
    N_("Disable mipmapping on text"), NULL },
  { "clutter-use-fuzzy-picking", 0, 0, G_OPTION_ARG_NONE,
    &clutter_use_fuzzy_picking,
    N_("Use 'fuzzy' picking"), NULL },
#ifdef CLUTTER_ENABLE_DEBUG
  { "clutter-debug", 0, 0, G_OPTION_ARG_CALLBACK, clutter_arg_debug_cb,
    N_("Clutter debugging flags to set"), "FLAGS" },
  { "clutter-no-debug", 0, 0, G_OPTION_ARG_CALLBACK, clutter_arg_no_debug_cb,
    N_("Clutter debugging flags to unset"), "FLAGS" },
#endif /* CLUTTER_ENABLE_DEBUG */
  { "clutter-enable-accessibility", 0, 0, G_OPTION_ARG_NONE, &clutter_enable_accessibility,
    N_("Enable accessibility"), NULL },
  { NULL, },
};

/* pre_parse_hook: initialise variables depending on environment
 * variables; these variables might be overridden by the command
 * line arguments that are going to be parsed after.
 */
static gboolean
pre_parse_hook (GOptionContext  *context,
                GOptionGroup    *group,
                gpointer         data,
                GError         **error)
{
  ClutterMainContext *clutter_context;
  ClutterBackend *backend;
  const char *env_string;

  if (clutter_is_initialized)
    return TRUE;

  clutter_context = _clutter_context_get_default ();

  backend = clutter_context->backend;
  g_assert (CLUTTER_IS_BACKEND (backend));

#ifdef CLUTTER_ENABLE_DEBUG
  env_string = g_getenv ("CLUTTER_DEBUG");
  if (env_string != NULL)
    {
      clutter_debug_flags =
        g_parse_debug_string (env_string,
                              clutter_debug_keys,
                              G_N_ELEMENTS (clutter_debug_keys));
      env_string = NULL;
    }
#endif /* CLUTTER_ENABLE_DEBUG */

  env_string = g_getenv ("CLUTTER_PICK");
  if (env_string != NULL)
    {
      clutter_pick_debug_flags =
        g_parse_debug_string (env_string,
                              clutter_pick_debug_keys,
                              G_N_ELEMENTS (clutter_pick_debug_keys));
      env_string = NULL;
    }

  env_string = g_getenv ("CLUTTER_PAINT");
  if (env_string != NULL)
    {
      clutter_paint_debug_flags =
        g_parse_debug_string (env_string,
                              clutter_paint_debug_keys,
                              G_N_ELEMENTS (clutter_paint_debug_keys));
      env_string = NULL;
    }

  env_string = g_getenv ("CLUTTER_SHOW_FPS");
  if (env_string)
    clutter_show_fps = TRUE;

  env_string = g_getenv ("CLUTTER_DEFAULT_FPS");
  if (env_string)
    {
      gint default_fps = g_ascii_strtoll (env_string, NULL, 10);

      clutter_default_fps = CLAMP (default_fps, 1, 1000);
    }

  env_string = g_getenv ("CLUTTER_DISABLE_MIPMAPPED_TEXT");
  if (env_string)
    clutter_disable_mipmap_text = TRUE;

  env_string = g_getenv ("CLUTTER_FUZZY_PICK");
  if (env_string)
    clutter_use_fuzzy_picking = TRUE;

  return _clutter_backend_pre_parse (backend, error);
}

/* post_parse_hook: initialise the context and data structures
 * and opens the X display
 */
static gboolean
post_parse_hook (GOptionContext  *context,
                 GOptionGroup    *group,
                 gpointer         data,
                 GError         **error)
{
  ClutterMainContext *clutter_context;
  ClutterBackend *backend;

  if (clutter_is_initialized)
    return TRUE;

  clutter_context = _clutter_context_get_default ();
  backend = clutter_context->backend;
  g_assert (CLUTTER_IS_BACKEND (backend));

  if (clutter_fatal_warnings)
    {
      GLogLevelFlags fatal_mask;

      fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
      fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
      g_log_set_always_fatal (fatal_mask);
    }

  clutter_context->frame_rate = clutter_default_fps;
  clutter_context->show_fps = clutter_show_fps;
  clutter_context->options_parsed = TRUE;

  /* If not asked to defer display setup, call clutter_init_real(),
   * which in turn calls the backend post parse hooks.
   */
  if (!clutter_context->defer_display_setup)
    return clutter_init_real (error) == CLUTTER_INIT_SUCCESS;

  return TRUE;
}

/**
 * clutter_get_option_group: (skip)
 *
 * Returns a #GOptionGroup for the command line arguments recognized
 * by Clutter. You should add this group to your #GOptionContext with
 * g_option_context_add_group(), if you are using g_option_context_parse()
 * to parse your commandline arguments.
 *
 * Calling g_option_context_parse() with Clutter's #GOptionGroup will result
 * in Clutter's initialization. That is, the following code:
 *
 * |[
 *   g_option_context_set_main_group (context, clutter_get_option_group ());
 *   res = g_option_context_parse (context, &argc, &argc, NULL);
 * ]|
 *
 * is functionally equivalent to:
 *
 * |[
 *   clutter_init (&argc, &argv);
 * ]|
 *
 * After g_option_context_parse() on a #GOptionContext containing the
 * Clutter #GOptionGroup has returned %TRUE, Clutter is guaranteed to be
 * initialized.
 *
 * Return value: (transfer full): a #GOptionGroup for the commandline arguments
 *   recognized by Clutter
 *
 * Since: 0.2
 */
GOptionGroup *
clutter_get_option_group (void)
{
  ClutterMainContext *context;
  GOptionGroup *group;

  clutter_base_init ();

  context = _clutter_context_get_default ();

  group = g_option_group_new ("clutter",
                              "Clutter Options",
                              "Show Clutter Options",
                              NULL,
                              NULL);

  g_option_group_set_parse_hooks (group, pre_parse_hook, post_parse_hook);
  g_option_group_add_entries (group, clutter_args);

  /* add backend-specific options */
  _clutter_backend_add_options (context->backend, group);

  return group;
}

/**
 * clutter_get_option_group_without_init: (skip)
 *
 * Returns a #GOptionGroup for the command line arguments recognized
 * by Clutter. You should add this group to your #GOptionContext with
 * g_option_context_add_group(), if you are using g_option_context_parse()
 * to parse your commandline arguments.
 *
 * Unlike clutter_get_option_group(), calling g_option_context_parse() with
 * the #GOptionGroup returned by this function requires a subsequent explicit
 * call to clutter_init(); use this function when needing to set foreign
 * display connection with clutter_x11_set_display(), or with
 * `gtk_clutter_init()`.
 *
 * Return value: (transfer full): a #GOptionGroup for the commandline arguments
 *   recognized by Clutter
 *
 * Since: 0.8
 */
GOptionGroup *
clutter_get_option_group_without_init (void)
{
  ClutterMainContext *context;
  GOptionGroup *group;

  clutter_base_init ();

  context = _clutter_context_get_default ();
  context->defer_display_setup = TRUE;

  group = clutter_get_option_group ();

  return group;
}

/* Note that the gobject-introspection annotations for the argc/argv
 * parameters do not produce the right result; however, they do
 * allow the common case of argc=NULL, argv=NULL to work.
 */

/**
 * clutter_init_with_args:
 * @argc: (inout): a pointer to the number of command line arguments
 * @argv: (array length=argc) (inout) (allow-none): a pointer to the array
 *   of command line arguments
 * @parameter_string: (allow-none): a string which is displayed in the
 *   first line of <option>--help</option> output, after
 *   <literal><replaceable>programname</replaceable> [OPTION...]</literal>
 * @entries: (array) (allow-none): a %NULL terminated array of
 *   #GOptionEntry<!-- -->s describing the options of your program
 * @translation_domain: (allow-none): a translation domain to use for
 *   translating the <option>--help</option> output for the options in
 *   @entries with gettext(), or %NULL
 * @error: (allow-none): a return location for a #GError
 *
 * This function does the same work as clutter_init(). Additionally,
 * it allows you to add your own command line options, and it
 * automatically generates nicely formatted <option>--help</option>
 * output. Note that your program will be terminated after writing
 * out the help output. Also note that, in case of error, the
 * error message will be placed inside @error instead of being
 * printed on the display.
 *
 * Just like clutter_init(), if this function returns an error code then
 * any subsequent call to any other Clutter API will result in undefined
 * behaviour - including segmentation faults.
 *
 * Return value: %CLUTTER_INIT_SUCCESS if Clutter has been successfully
 *   initialised, or other values or #ClutterInitError in case of
 *   error.
 *
 * Since: 0.2
 */
ClutterInitError
clutter_init_with_args (int            *argc,
                        char         ***argv,
                        const char     *parameter_string,
                        GOptionEntry   *entries,
                        const char     *translation_domain,
                        GError        **error)
{
  GOptionContext *context;
  GOptionGroup *group;
  gboolean res;
  ClutterMainContext *ctx;

  if (clutter_is_initialized)
    return CLUTTER_INIT_SUCCESS;

  clutter_base_init ();

  ctx = _clutter_context_get_default ();

  if (!ctx->defer_display_setup)
    {
#if 0
      if (argc && *argc > 0 && *argv)
	g_set_prgname ((*argv)[0]);
#endif

      context = g_option_context_new (parameter_string);

      group = clutter_get_option_group ();
      g_option_context_add_group (context, group);

      group = cogl_get_option_group ();
      g_option_context_add_group (context, group);

      if (entries)
	g_option_context_add_main_entries (context, entries, translation_domain);

      res = g_option_context_parse (context, argc, argv, error);
      g_option_context_free (context);

      /* if res is FALSE, the error is filled for
       * us by g_option_context_parse()
       */
      if (!res)
	{
	  /* if there has been an error in the initialization, the
	   * error id will be preserved inside the GError code
	   */
	  if (error && *error)
	    return (*error)->code;
	  else
	    return CLUTTER_INIT_ERROR_INTERNAL;
	}

      return CLUTTER_INIT_SUCCESS;
    }
  else
    return clutter_init_real (error);
}

static gboolean
clutter_parse_args (int      *argc,
                    char   ***argv,
                    GError  **error)
{
  GOptionContext *option_context;
  GOptionGroup *clutter_group, *cogl_group;
  GError *internal_error = NULL;
  gboolean ret = TRUE;

  if (clutter_is_initialized)
    return TRUE;

  option_context = g_option_context_new (NULL);
  g_option_context_set_ignore_unknown_options (option_context, TRUE);
  g_option_context_set_help_enabled (option_context, FALSE);

  /* Initiate any command line options from the backend */
  clutter_group = clutter_get_option_group ();
  g_option_context_set_main_group (option_context, clutter_group);

  cogl_group = cogl_get_option_group ();
  g_option_context_add_group (option_context, cogl_group);

  if (!g_option_context_parse (option_context, argc, argv, &internal_error))
    {
      g_propagate_error (error, internal_error);
      ret = FALSE;
    }

  g_option_context_free (option_context);

  return ret;
}

/**
 * clutter_init:
 * @argc: (inout): The number of arguments in @argv
 * @argv: (array length=argc) (inout) (allow-none): A pointer to an array
 *   of arguments.
 *
 * Initialises everything needed to operate with Clutter and parses some
 * standard command line options; @argc and @argv are adjusted accordingly
 * so your own code will never see those standard arguments.
 *
 * It is safe to call this function multiple times.
 *
 * This function will not abort in case of errors during
 * initialization; clutter_init() will print out the error message on
 * stderr, and will return an error code. It is up to the application
 * code to handle this case. If you need to display the error message
 * yourself, you can use clutter_init_with_args(), which takes a #GError
 * pointer.
 *
 * If this function fails, and returns an error code, any subsequent
 * Clutter API will have undefined behaviour - including segmentation
 * faults and assertion failures. Make sure to handle the returned
 * #ClutterInitError enumeration value.
 *
 * Return value: a #ClutterInitError value
 */
ClutterInitError
clutter_init (int    *argc,
              char ***argv)
{
  ClutterMainContext *ctx;
  GError *error = NULL;
  ClutterInitError res;

  if (clutter_is_initialized)
    return CLUTTER_INIT_SUCCESS;

  clutter_base_init ();

  ctx = _clutter_context_get_default ();

  if (!ctx->defer_display_setup)
    {
#if 0
      if (argc && *argc > 0 && *argv)
	g_set_prgname ((*argv)[0]);
#endif

      /* parse_args will trigger backend creation and things like
       * DISPLAY connection etc.
       */
      if (!clutter_parse_args (argc, argv, &error))
	{
          g_critical ("Unable to initialize Clutter: %s", error->message);
          g_error_free (error);

          res = CLUTTER_INIT_ERROR_INTERNAL;
	}
      else
        res = CLUTTER_INIT_SUCCESS;
    }
  else
    {
      res = clutter_init_real (&error);
      if (error != NULL)
        {
          g_critical ("Unable to initialize Clutter: %s", error->message);
          g_error_free (error);
        }
    }

  return res;
}

gboolean
_clutter_boolean_handled_accumulator (GSignalInvocationHint *ihint,
                                      GValue                *return_accu,
                                      const GValue          *handler_return,
                                      gpointer               dummy)
{
  gboolean continue_emission;
  gboolean signal_handled;

  signal_handled = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, signal_handled);
  continue_emission = !signal_handled;

  return continue_emission;
}

gboolean
_clutter_boolean_continue_accumulator (GSignalInvocationHint *ihint,
                                       GValue                *return_accu,
                                       const GValue          *handler_return,
                                       gpointer               dummy)
{
  gboolean continue_emission;

  continue_emission = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, continue_emission);

  return continue_emission;
}

static void
event_click_count_generate (ClutterEvent *event)
{
  /* multiple button click detection */
  static gint    click_count            = 0;
  static gint    previous_x             = -1;
  static gint    previous_y             = -1;
  static guint32 previous_time          = 0;
  static gint    previous_button_number = -1;

  ClutterInputDevice *device = NULL;
  ClutterSettings *settings;
  guint double_click_time;
  guint double_click_distance;

  settings = clutter_settings_get_default ();

  g_object_get (settings,
                "double-click-distance", &double_click_distance,
                "double-click-time", &double_click_time,
                NULL);

  device = clutter_event_get_device (event);
  if (device != NULL)
    {
      click_count = device->click_count;
      previous_x = device->previous_x;
      previous_y = device->previous_y;
      previous_time = device->previous_time;
      previous_button_number = device->previous_button_number;

      CLUTTER_NOTE (EVENT,
                    "Restoring previous click count:%d (device:%d, time:%u)",
                    click_count,
                    clutter_input_device_get_device_id (device),
                    previous_time);
    }
  else
    {
      CLUTTER_NOTE (EVENT,
                    "Restoring previous click count:%d (time:%u)",
                    click_count,
                    previous_time);
    }

  switch (clutter_event_type (event))
    {
      case CLUTTER_BUTTON_PRESS:
        /* check if we are in time and within distance to increment an
         * existing click count
         */
        if (event->button.button == previous_button_number &&
            event->button.time < (previous_time + double_click_time) &&
            (ABS (event->button.x - previous_x) <= double_click_distance) &&
            (ABS (event->button.y - previous_y) <= double_click_distance))
          {
            CLUTTER_NOTE (EVENT, "Increase click count (button: %d, time: %u)",
                          event->button.button,
                          event->button.time);

            click_count += 1;
          }
        else /* start a new click count*/
          {
            CLUTTER_NOTE (EVENT, "Reset click count (button: %d, time: %u)",
                          event->button.button,
                          event->button.time);

            click_count = 1;
            previous_button_number = event->button.button;
          }

        previous_x = event->button.x;
        previous_y = event->button.y;
        previous_time = event->button.time;

        G_GNUC_FALLTHROUGH;
      case CLUTTER_BUTTON_RELEASE:
        event->button.click_count = click_count;
        break;

      default:
        g_assert_not_reached ();
        break;
    }

  if (event->type == CLUTTER_BUTTON_PRESS && device != NULL)
    {
      CLUTTER_NOTE (EVENT, "Storing click count: %d (device:%d, time:%u)",
                    click_count,
                    clutter_input_device_get_device_id (device),
                    previous_time);

      device->click_count = click_count;
      device->previous_x = previous_x;
      device->previous_y = previous_y;
      device->previous_time = previous_time;
      device->previous_button_number = previous_button_number;
    }
}

static inline void
emit_event_chain (ClutterEvent *event)
{
  static gboolean lock = FALSE;

  if (event->any.source == NULL)
    {
      CLUTTER_NOTE (EVENT, "No source set, discarding event");
      return;
    }

  /* reentrancy check */
  if (lock != FALSE)
    {
      g_warning ("Tried emitting event during event delivery, bailing out.");
      return;
    }

  lock = TRUE;

  _clutter_actor_handle_event (event->any.source, event);

  lock = FALSE;
}

/*
 * Emits a pointer event after having prepared the event for delivery (setting
 * source, computing click_count, generating enter/leave etc.).
 */

static inline void
emit_pointer_event (ClutterEvent       *event,
                    ClutterInputDevice *device)
{
  if (_clutter_event_process_filters (event))
    return;

  if (device != NULL && device->pointer_grab_actor != NULL)
    clutter_actor_event (device->pointer_grab_actor, event, FALSE);
  else
    emit_event_chain (event);
}

static inline void
emit_crossing_event (ClutterEvent       *event,
                     ClutterInputDevice *device)
{
  ClutterEventSequence *sequence = clutter_event_get_event_sequence (event);
  ClutterActor *grab_actor = NULL;

  if (_clutter_event_process_filters (event))
    return;

  if (sequence)
    {
      if (device->sequence_grab_actors != NULL)
        grab_actor = g_hash_table_lookup (device->sequence_grab_actors, sequence);
    }
  else
    {
      if (device != NULL && device->pointer_grab_actor != NULL)
        grab_actor = device->pointer_grab_actor;
    }

  if (grab_actor != NULL)
    clutter_actor_event (grab_actor, event, FALSE);
  else
    emit_event_chain (event);
}

static inline void
emit_touch_event (ClutterEvent       *event,
                  ClutterInputDevice *device)
{
  ClutterActor *grab_actor = NULL;

  if (_clutter_event_process_filters (event))
    return;

  if (device->sequence_grab_actors != NULL)
    {
      grab_actor = g_hash_table_lookup (device->sequence_grab_actors,
                                        event->touch.sequence);
    }

  if (grab_actor != NULL)
    {
      /* per-device sequence grab */
      clutter_actor_event (grab_actor, event, FALSE);
    }
  else
    {
      /* no grab, time to capture and bubble */
      emit_event_chain (event);
    }
}

static inline void
emit_keyboard_event (ClutterEvent       *event,
                     ClutterInputDevice *device)
{
  if (_clutter_event_process_filters (event))
    return;

  if (device != NULL && device->keyboard_grab_actor != NULL)
    clutter_actor_event (device->keyboard_grab_actor, event, FALSE);
  else
    emit_event_chain (event);
}

static inline void
process_key_event (ClutterEvent       *event,
                   ClutterInputDevice *device)
{
  ClutterInputDeviceClass *device_class = CLUTTER_INPUT_DEVICE_GET_CLASS (device);

  if (device_class->process_kbd_a11y_event)
    {
      device_class->process_kbd_a11y_event (event, device, emit_keyboard_event);
      return;
    }

  emit_keyboard_event (event, device);
}

static gboolean
is_off_stage (ClutterActor *stage,
              gfloat        x,
              gfloat        y)
{
  gfloat width, height;

  clutter_actor_get_size (stage, &width, &height);

  return (x < 0 ||
          y < 0 ||
          x >= width ||
          y >= height);
}

/**
 * clutter_do_event:
 * @event: a #ClutterEvent.
 *
 * Processes an event.
 *
 * The @event must be a valid #ClutterEvent and have a #ClutterStage
 * associated to it.
 *
 * This function is only useful when embedding Clutter inside another
 * toolkit, and it should never be called by applications.
 *
 * Since: 0.4
 */
void
clutter_do_event (ClutterEvent *event)
{
  /* we need the stage for the event */
  if (event->any.stage == NULL)
    {
      g_warning ("%s: Event does not have a stage: discarding.", G_STRFUNC);
      return;
    }

  /* stages in destruction do not process events */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (event->any.stage))
    return;

  /* Instead of processing events when received, we queue them up to
   * handle per-frame before animations, layout, and drawing.
   *
   * This gives us the chance to reliably compress motion events
   * because we've "looked ahead" and know all motion events that
   * will occur before drawing the frame.
   */
  _clutter_stage_queue_event (event->any.stage, event, TRUE);
}

static void
_clutter_process_event_details (ClutterActor        *stage,
                                ClutterMainContext  *context,
                                ClutterEvent        *event)
{
  ClutterInputDevice *device = clutter_event_get_device (event);

  switch (event->type)
    {
      case CLUTTER_NOTHING:
        event->any.source = stage;
        break;

      case CLUTTER_KEY_PRESS:
      case CLUTTER_KEY_RELEASE:
      case CLUTTER_PAD_BUTTON_PRESS:
      case CLUTTER_PAD_BUTTON_RELEASE:
      case CLUTTER_PAD_STRIP:
      case CLUTTER_PAD_RING:
      case CLUTTER_IM_COMMIT:
      case CLUTTER_IM_DELETE:
      case CLUTTER_IM_PREEDIT:
        {
          ClutterActor *actor = NULL;

          /* check that we're not a synthetic event with source set */
          if (event->any.source == NULL)
            {
              actor = clutter_stage_get_key_focus (CLUTTER_STAGE (stage));
              event->any.source = actor;
              if (G_UNLIKELY (actor == NULL))
                {
                  g_warning ("No key focus set, discarding");
                  return;
                }
            }

          process_key_event (event, device);
        }
        break;

      case CLUTTER_ENTER:
        /* if we're entering from outside the stage we need
         * to check whether the pointer is actually on another
         * actor, and emit an additional pointer event
         */
        if (event->any.source == stage &&
            event->crossing.related == NULL)
          {
            ClutterActor *actor = NULL;

            emit_crossing_event (event, device);

            actor = clutter_input_device_update (device, NULL, FALSE);
            if (actor != stage)
              {
                ClutterEvent *crossing;

                /* we emit the exact same event on the actor */
                crossing = clutter_event_copy (event);
                crossing->crossing.related = stage;
                crossing->crossing.source = actor;

                emit_crossing_event (crossing, device);
                clutter_event_free (crossing);
              }
          }
        else
          emit_crossing_event (event, device);
        break;

      case CLUTTER_LEAVE:
        /* same as CLUTTER_ENTER above: when leaving the stage
         * we need to also emit a CLUTTER_LEAVE event on the
         * actor currently underneath the device, unless it's the
         * stage
         */
        if (event->any.source == stage &&
            event->crossing.related == NULL &&
            device->cursor_actor != stage)
          {
            ClutterEvent *crossing;

            crossing = clutter_event_copy (event);
            crossing->crossing.related = stage;
            crossing->crossing.source = device->cursor_actor;

            emit_crossing_event (crossing, device);
            clutter_event_free (crossing);
          }
        emit_crossing_event (event, device);
        break;

      case CLUTTER_DESTROY_NOTIFY:
      case CLUTTER_DELETE:
        event->any.source = stage;

        if (_clutter_event_process_filters (event))
          break;

        /* the stage did not handle the event, so we just quit */
        clutter_stage_event (CLUTTER_STAGE (stage), event);
        break;

      case CLUTTER_MOTION:
#ifdef CLUTTER_WINDOWING_X11
        if (!clutter_check_windowing_backend (CLUTTER_WINDOWING_X11) &&
            !(event->any.flags & CLUTTER_EVENT_FLAG_SYNTHETIC))
          {
            if (_clutter_is_input_pointer_a11y_enabled (device))
              {
                gfloat x, y;

                clutter_event_get_coords (event, &x, &y);
                _clutter_input_pointer_a11y_on_motion_event (device, x, y);
              }
          }
#endif /* CLUTTER_WINDOWING_X11 */
        /* only the stage gets motion events if they are enabled */
        if (!clutter_stage_get_motion_events_enabled (CLUTTER_STAGE (stage)) &&
            event->any.source == NULL)
          {
            /* Only stage gets motion events */
            event->any.source = stage;

            if (_clutter_event_process_filters (event))
              break;

            if (device != NULL && device->pointer_grab_actor != NULL)
              {
                clutter_actor_event (device->pointer_grab_actor,
                                     event,
                                     FALSE);
                break;
              }

            /* Trigger handlers on stage in both capture .. */
            if (!clutter_actor_event (stage, event, TRUE))
              {
                /* and bubbling phase */
                clutter_actor_event (stage, event, FALSE);
              }
            break;
          }

        G_GNUC_FALLTHROUGH;
      case CLUTTER_BUTTON_PRESS:
      case CLUTTER_BUTTON_RELEASE:
#ifdef CLUTTER_WINDOWING_X11
        if (!clutter_check_windowing_backend (CLUTTER_WINDOWING_X11))
          {
            if (_clutter_is_input_pointer_a11y_enabled (device) && (event->type != CLUTTER_MOTION))
              {
                _clutter_input_pointer_a11y_on_button_event (device,
                                                             event->button.button,
                                                             event->type == CLUTTER_BUTTON_PRESS);
              }
          }
#endif /* CLUTTER_WINDOWING_X11 */
      case CLUTTER_SCROLL:
      case CLUTTER_TOUCHPAD_PINCH:
      case CLUTTER_TOUCHPAD_SWIPE:
      case CLUTTER_TOUCHPAD_HOLD:
        {
          ClutterActor *actor;
          gfloat x, y;

          clutter_event_get_coords (event, &x, &y);

          /* Only do a pick to find the source if source is not already set
           * (as it could be in a synthetic event)
           */
          if (event->any.source == NULL)
            {
              /* emulate X11 the implicit soft grab; the implicit soft grab
               * keeps relaying motion events when the stage is left with a
               * pointer button pressed. since this is what happens when we
               * disable per-actor motion events we need to maintain the same
               * behaviour when the per-actor motion events are enabled as
               * well
               */
              if (is_off_stage (stage, x, y))
                {
                  if (event->type == CLUTTER_BUTTON_RELEASE)
                    {
                      CLUTTER_NOTE (EVENT,
                                    "Release off stage received at %.2f, %.2f",
                                    x, y);

                      event->button.source = stage;
                      event->button.click_count = 1;

                      emit_pointer_event (event, device);
                    }
                  else if (event->type == CLUTTER_MOTION)
                    {
                      CLUTTER_NOTE (EVENT,
                                    "Motion off stage received at %.2f, %2.f",
                                    x, y);

                      event->motion.source = stage;

                      emit_pointer_event (event, device);
                    }

                  break;
                }

              /* if the backend provides a device then we should
               * already have everything we need to update it and
               * get the actor underneath
               */
              if (device != NULL)
                actor = clutter_input_device_update (device, NULL, TRUE);
              else
                {
                  CLUTTER_NOTE (EVENT, "No device found: picking");

                  actor = _clutter_stage_do_pick (CLUTTER_STAGE (stage),
                                                  x, y,
                                                  CLUTTER_PICK_REACTIVE);
                }

              if (actor == NULL)
                break;

              event->any.source = actor;
            }
          else
            {
              /* use the source already set in the synthetic event */
              actor = event->any.source;
            }

          CLUTTER_NOTE (EVENT,
                        "Reactive event received at %.2f, %.2f - actor: %p",
                        x, y,
                        actor);

          /* button presses and releases need a click count */
          if (event->type == CLUTTER_BUTTON_PRESS ||
              event->type == CLUTTER_BUTTON_RELEASE)
            {
              /* Generate click count */
              event_click_count_generate (event);
            }

          emit_pointer_event (event, device);
          break;
        }

      case CLUTTER_TOUCH_UPDATE:
        /* only the stage gets motion events if they are enabled */
        if (!clutter_stage_get_motion_events_enabled (CLUTTER_STAGE (stage)) &&
            event->any.source == NULL)
          {
            ClutterActor *grab_actor = NULL;

            /* Only stage gets motion events */
            event->any.source = stage;

            if (_clutter_event_process_filters (event))
              break;

            /* global grabs */
            if (device->sequence_grab_actors != NULL)
              {
                grab_actor = g_hash_table_lookup (device->sequence_grab_actors,
                                                  event->touch.sequence);
              }

            if (grab_actor != NULL)
              {
                clutter_actor_event (grab_actor, event, FALSE);
                break;
              }

            /* Trigger handlers on stage in both capture .. */
            if (!clutter_actor_event (stage, event, TRUE))
              {
                /* and bubbling phase */
                clutter_actor_event (stage, event, FALSE);
              }
            break;
          }

        G_GNUC_FALLTHROUGH;
      case CLUTTER_TOUCH_BEGIN:
      case CLUTTER_TOUCH_CANCEL:
      case CLUTTER_TOUCH_END:
        {
          ClutterActor *actor;
          ClutterEventSequence *sequence;
          gfloat x, y;

          sequence =
            clutter_event_get_event_sequence (event);

          if (event->type == CLUTTER_TOUCH_BEGIN)
            _clutter_input_device_add_event_sequence (device, event);

          clutter_event_get_coords (event, &x, &y);

          /* Only do a pick to find the source if source is not already set
           * (as it could be in a synthetic event)
           */
          if (event->any.source == NULL)
            {
              /* same as the mouse events above, emulate the X11 implicit
               * soft grab */
              if (is_off_stage (stage, x, y))
                {
                  CLUTTER_NOTE (EVENT,
                                "Touch %s off stage received at %.2f, %.2f",
                                event->type == CLUTTER_TOUCH_UPDATE ? "update" :
                                event->type == CLUTTER_TOUCH_END ? "end" :
                                event->type == CLUTTER_TOUCH_CANCEL ? "cancel" :
                                "?", x, y);

                  event->touch.source = stage;

                  emit_touch_event (event, device);

                  if (event->type == CLUTTER_TOUCH_END)
                    _clutter_input_device_remove_event_sequence (device, event);

                  break;
                }

              if (device != NULL)
                actor = clutter_input_device_update (device, sequence, TRUE);
              else
                {
                  CLUTTER_NOTE (EVENT, "No device found: picking");

                  actor = _clutter_stage_do_pick (CLUTTER_STAGE (stage),
                                                  x, y,
                                                  CLUTTER_PICK_REACTIVE);
                }

              if (actor == NULL)
                break;

              event->any.source = actor;
            }
          else
            {
              /* use the source already set in the synthetic event */
              actor = event->any.source;
            }

          CLUTTER_NOTE (EVENT,
                        "Reactive event received at %.2f, %.2f - actor: %p",
                        x, y,
                        actor);

          emit_touch_event (event, device);

          if (event->type == CLUTTER_TOUCH_END)
            _clutter_input_device_remove_event_sequence (device, event);

          break;
        }

      case CLUTTER_PROXIMITY_IN:
      case CLUTTER_PROXIMITY_OUT:
        clutter_input_device_update_from_tool (clutter_event_get_source_device (event),
                                               clutter_event_get_device_tool (event));

        if (_clutter_event_process_filters (event))
          break;

        if (!clutter_actor_event (stage, event, TRUE))
          {
            /* and bubbling phase */
            clutter_actor_event (stage, event, FALSE);
          }

        break;

      case CLUTTER_STAGE_STATE:
        /* focus - forward to stage */
        event->any.source = stage;
        if (!_clutter_event_process_filters (event))
          clutter_stage_event (CLUTTER_STAGE (stage), event);
        break;

      case CLUTTER_CLIENT_MESSAGE:
        break;

      case CLUTTER_DEVICE_ADDED:
      case CLUTTER_DEVICE_REMOVED:
        if (!_clutter_event_process_filters (event))
          {
            ClutterSeat *seat;

            seat = clutter_backend_get_default_seat (context->backend);
            clutter_seat_handle_device_event (seat, event);
          }
        break;

      case CLUTTER_EVENT_LAST:
        break;
    }
}

/*
 * _clutter_process_event
 * @event: a #ClutterEvent.
 *
 * Does the actual work of processing an event that was queued earlier
 * out of clutter_do_event().
 */
void
_clutter_process_event (ClutterEvent *event)
{
  ClutterMainContext *context;
  ClutterActor *stage;

  context = _clutter_context_get_default ();

  stage = CLUTTER_ACTOR (event->any.stage);
  if (stage == NULL)
    {
      CLUTTER_NOTE (EVENT, "Discarding event without a stage set");
      return;
    }

  /* push events on a stack, so that we don't need to
   * add an event parameter to all signals that can be emitted within
   * an event chain
   */
  context->current_event = g_slist_prepend (context->current_event, event);

  _clutter_process_event_details (stage, context, event);

  context->current_event = g_slist_delete_link (context->current_event, context->current_event);
}

/**
 * clutter_get_actor_by_gid:
 * @id_: a #ClutterActor unique id.
 *
 * Retrieves the #ClutterActor with @id_.
 *
 * Return value: (transfer none): the actor with the passed id or %NULL.
 *   The returned actor does not have its reference count increased.
 *
 * Since: 0.6
 *
 * Deprecated: 1.8: The id is deprecated, and this function always returns
 *   %NULL. Use the proper scene graph API in #ClutterActor to find a child
 *   of the stage.
 */
ClutterActor *
clutter_get_actor_by_gid (guint32 id_)
{
  return NULL;
}

void
clutter_base_init (void)
{
  static gboolean initialised = FALSE;

  if (!initialised)
    {
      initialised = TRUE;

#if !GLIB_CHECK_VERSION (2, 35, 1)
      /* initialise GLib type system */
      g_type_init ();
#endif

      /* initialise the Big Clutter Lock™ if necessary */
      clutter_threads_init_default ();

      clutter_graphene_init ();
    }
}

/**
 * clutter_get_default_frame_rate:
 *
 * Retrieves the default frame rate. See clutter_set_default_frame_rate().
 *
 * Return value: the default frame rate
 *
 * Since: 0.6
 */
guint
clutter_get_default_frame_rate (void)
{
  ClutterMainContext *context;

  context = _clutter_context_get_default ();

  return context->frame_rate;
}

/**
 * clutter_get_font_map:
 *
 * Retrieves the #PangoFontMap instance used by Clutter.
 * You can use the global font map object with the COGL
 * Pango API.
 *
 * Return value: (transfer none): the #PangoFontMap instance. The returned
 *   value is owned by Clutter and it should never be unreferenced.
 *
 * Since: 1.0
 */
PangoFontMap *
clutter_get_font_map (void)
{
  return PANGO_FONT_MAP (clutter_context_get_pango_fontmap ());
}

typedef struct _ClutterRepaintFunction
{
  guint id;
  ClutterRepaintFlags flags;
  GSourceFunc func;
  gpointer data;
  GDestroyNotify notify;
} ClutterRepaintFunction;

/**
 * clutter_threads_remove_repaint_func:
 * @handle_id: an unsigned integer greater than zero
 *
 * Removes the repaint function with @handle_id as its id
 *
 * Since: 1.0
 */
void
clutter_threads_remove_repaint_func (guint handle_id)
{
  ClutterRepaintFunction *repaint_func;
  ClutterMainContext *context;
  GList *l;

  g_return_if_fail (handle_id > 0);

  _clutter_context_lock ();

  context = clutter_context_get_default_unlocked ();
  l = context->repaint_funcs;
  while (l != NULL)
    {
      repaint_func = l->data;

      if (repaint_func->id == handle_id)
        {
          context->repaint_funcs =
            g_list_remove_link (context->repaint_funcs, l);

          g_list_free (l);

          if (repaint_func->notify)
            repaint_func->notify (repaint_func->data);

          g_slice_free (ClutterRepaintFunction, repaint_func);

          break;
        }

      l = l->next;
    }

  _clutter_context_unlock ();
}

/**
 * clutter_threads_add_repaint_func:
 * @func: the function to be called within the paint cycle
 * @data: data to be passed to the function, or %NULL
 * @notify: function to be called when removing the repaint
 *    function, or %NULL
 *
 * Adds a function to be called whenever Clutter is processing a new
 * frame.
 *
 * If the function returns %FALSE it is automatically removed from the
 * list of repaint functions and will not be called again.
 *
 * This function is guaranteed to be called from within the same thread
 * that called clutter_main(), and while the Clutter lock is being held;
 * the function will be called within the main loop, so it is imperative
 * that it does not block, otherwise the frame time budget may be lost.
 *
 * A repaint function is useful to ensure that an update of the scenegraph
 * is performed before the scenegraph is repainted. By default, a repaint
 * function added using this function will be invoked prior to the frame
 * being processed.
 *
 * Adding a repaint function does not automatically ensure that a new
 * frame will be queued.
 *
 * When the repaint function is removed (either because it returned %FALSE
 * or because clutter_threads_remove_repaint_func() has been called) the
 * @notify function will be called, if any is set.
 *
 * See also: clutter_threads_add_repaint_func_full()
 *
 * Return value: the ID (greater than 0) of the repaint function. You
 *   can use the returned integer to remove the repaint function by
 *   calling clutter_threads_remove_repaint_func().
 *
 * Since: 1.0
 */
guint
clutter_threads_add_repaint_func (GSourceFunc    func,
                                  gpointer       data,
                                  GDestroyNotify notify)
{
  return clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT,
                                                func,
                                                data, notify);
}

/**
 * clutter_threads_add_repaint_func_full:
 * @flags: flags for the repaint function
 * @func: the function to be called within the paint cycle
 * @data: data to be passed to the function, or %NULL
 * @notify: function to be called when removing the repaint
 *    function, or %NULL
 *
 * Adds a function to be called whenever Clutter is processing a new
 * frame.
 *
 * If the function returns %FALSE it is automatically removed from the
 * list of repaint functions and will not be called again.
 *
 * This function is guaranteed to be called from within the same thread
 * that called clutter_main(), and while the Clutter lock is being held;
 * the function will be called within the main loop, so it is imperative
 * that it does not block, otherwise the frame time budget may be lost.
 *
 * A repaint function is useful to ensure that an update of the scenegraph
 * is performed before the scenegraph is repainted. The @flags passed to this
 * function will determine the section of the frame processing that will
 * result in @func being called.
 *
 * Adding a repaint function does not automatically ensure that a new
 * frame will be queued.
 *
 * When the repaint function is removed (either because it returned %FALSE
 * or because clutter_threads_remove_repaint_func() has been called) the
 * @notify function will be called, if any is set.
 *
 * Return value: the ID (greater than 0) of the repaint function. You
 *   can use the returned integer to remove the repaint function by
 *   calling clutter_threads_remove_repaint_func().
 *
 * Since: 1.10
 */
guint
clutter_threads_add_repaint_func_full (ClutterRepaintFlags flags,
                                       GSourceFunc         func,
                                       gpointer            data,
                                       GDestroyNotify      notify)
{
  ClutterMainContext *context;
  ClutterRepaintFunction *repaint_func;

  g_return_val_if_fail (func != NULL, 0);

  _clutter_context_lock ();

  context = clutter_context_get_default_unlocked ();

  repaint_func = g_slice_new (ClutterRepaintFunction);

  repaint_func->id = context->last_repaint_id++;

  repaint_func->flags = flags;
  repaint_func->func = func;
  repaint_func->data = data;
  repaint_func->notify = notify;

  context->repaint_funcs = g_list_prepend (context->repaint_funcs,
                                           repaint_func);

  _clutter_context_unlock ();

  return repaint_func->id;
}

/*
 * _clutter_run_repaint_functions:
 * @flags: only run the repaint functions matching the passed flags
 *
 * Executes the repaint functions added using the
 * clutter_threads_add_repaint_func() function.
 *
 * Must be called with the Clutter thread lock held.
 */
void
_clutter_run_repaint_functions (ClutterRepaintFlags flags)
{
  ClutterMainContext *context = _clutter_context_get_default ();
  ClutterRepaintFunction *repaint_func;
  GList *invoke_list, *reinvoke_list, *l;

  if (context->repaint_funcs == NULL)
    return;

  /* steal the list */
  invoke_list = context->repaint_funcs;
  context->repaint_funcs = NULL;

  reinvoke_list = NULL;

  /* consume the whole list while we execute the functions */
  while (invoke_list != NULL)
    {
      gboolean res = FALSE;

      repaint_func = invoke_list->data;

      l = invoke_list;
      invoke_list = g_list_remove_link (invoke_list, invoke_list);

      g_list_free (l);

      if ((repaint_func->flags & flags) != 0)
        res = repaint_func->func (repaint_func->data);
      else
        res = TRUE;

      if (res)
        reinvoke_list = g_list_prepend (reinvoke_list, repaint_func);
      else
        {
          if (repaint_func->notify != NULL)
            repaint_func->notify (repaint_func->data);

          g_slice_free (ClutterRepaintFunction, repaint_func);
        }
    }

  if (context->repaint_funcs != NULL)
    {
      context->repaint_funcs = g_list_concat (context->repaint_funcs,
                                              g_list_reverse (reinvoke_list));
    }
  else
    context->repaint_funcs = g_list_reverse (reinvoke_list);
}

/**
 * clutter_get_default_text_direction:
 *
 * Retrieves the default direction for the text. The text direction is
 * determined by the locale and/or by the `CLUTTER_TEXT_DIRECTION`
 * environment variable.
 *
 * The default text direction can be overridden on a per-actor basis by using
 * clutter_actor_set_text_direction().
 *
 * Return value: the default text direction
 *
 * Since: 1.2
 */
ClutterTextDirection
clutter_get_default_text_direction (void)
{
  return clutter_text_direction;
}

/*< private >
 * clutter_clear_events_queue:
 *
 * Clears the events queue stored in the main context.
 */
void
_clutter_clear_events_queue (void)
{
  ClutterMainContext *context = _clutter_context_get_default ();

  if (context->events_queue != NULL)
    {
      g_queue_foreach (context->events_queue,
                       (GFunc) clutter_event_free,
                       NULL);
      g_queue_free (context->events_queue);
      context->events_queue = NULL;
    }
}

void
_clutter_clear_events_queue_for_stage (ClutterStage *stage)
{
  ClutterMainContext *context = _clutter_context_get_default ();
  GList *l, *next;

  if (context->events_queue == NULL)
    return;

  /* Remove any pending events for this stage from the event queue */
  for (l = context->events_queue->head; l; l = next)
    {
      ClutterEvent *event = l->data;

      next = l->next;

      if (event->any.stage == stage)
        {
          g_queue_delete_link (context->events_queue, l);
          clutter_event_free (event);
        }
    }
}

ClutterPickMode
_clutter_context_get_pick_mode (void)
{
  ClutterMainContext *context = _clutter_context_get_default ();

  return context->pick_mode;
}

/**
 * clutter_check_windowing_backend:
 * @backend_type: the name of the backend to check
 *
 * Checks the run-time name of the Clutter windowing system backend, using
 * the symbolic macros like %CLUTTER_WINDOWING_X11.
 *
 * This function should be used in conjuction with the compile-time macros
 * inside applications and libraries that are using the platform-specific
 * windowing system API, to ensure that they are running on the correct
 * windowing system; for instance:
 *
 * |[
 * #ifdef CLUTTER_WINDOWING_X11
 *   if (clutter_check_windowing_backend (CLUTTER_WINDOWING_X11))
 *     {
 *       // it is safe to use the clutter_x11_* API
 *     }
 *   else
 * #endif
 *     g_error ("Unknown Clutter backend.");
 * ]|
 *
 * Return value: %TRUE if the current Clutter windowing system backend is
 *   the one checked, and %FALSE otherwise
 *
 * Since: 1.10
 */
gboolean
clutter_check_windowing_backend (const char *backend_type)
{
  ClutterMainContext *context = _clutter_context_get_default ();

  g_return_val_if_fail (backend_type != NULL, FALSE);

  backend_type = g_intern_string (backend_type);

#ifdef CLUTTER_WINDOWING_EGL
  if (backend_type == I_(CLUTTER_WINDOWING_EGL) &&
      CLUTTER_IS_BACKEND_EGL_NATIVE (context->backend))
    return TRUE;
  else
#endif
#ifdef CLUTTER_WINDOWING_X11
  if (backend_type == I_(CLUTTER_WINDOWING_X11) &&
      CLUTTER_IS_BACKEND_X11 (context->backend))
    return TRUE;
  else
#endif
  return FALSE;
}

/**
 * clutter_add_debug_flags: (skip)
 *
 * Adds the debug flags passed to the list of debug flags.
 */
void
clutter_add_debug_flags (ClutterDebugFlag     debug_flags,
                         ClutterDrawDebugFlag draw_flags,
                         ClutterPickDebugFlag pick_flags)
{
  clutter_debug_flags |= debug_flags;
  clutter_paint_debug_flags |= draw_flags;
  clutter_pick_debug_flags |= pick_flags;
}

/**
 * clutter_remove_debug_flags: (skip)
 *
 * Removes the debug flags passed from the list of debug flags.
 */
void
clutter_remove_debug_flags (ClutterDebugFlag     debug_flags,
                            ClutterDrawDebugFlag draw_flags,
                            ClutterPickDebugFlag pick_flags)
{
  clutter_debug_flags &= ~debug_flags;
  clutter_paint_debug_flags &= ~draw_flags;
  clutter_pick_debug_flags &= ~pick_flags;
}

void
_clutter_set_sync_to_vblank (gboolean sync_to_vblank)
{
  clutter_sync_to_vblank = !!sync_to_vblank;
}

void
_clutter_debug_messagev (const char *format,
                         va_list     var_args)
{
  static gint64 last_debug_stamp;
  gchar *stamp, *fmt;
  gint64 cur_time, debug_stamp;

  cur_time = g_get_monotonic_time ();

  /* if the last debug message happened less than a second ago, just
   * show the increments instead of the full timestamp
   */
  if (last_debug_stamp == 0 ||
      cur_time - last_debug_stamp >= G_USEC_PER_SEC)
    {
      debug_stamp = cur_time;
      last_debug_stamp = debug_stamp;

      stamp = g_strdup_printf ("[%16" G_GINT64_FORMAT "]", debug_stamp);
    }
  else
    {
      debug_stamp = cur_time - last_debug_stamp;

      stamp = g_strdup_printf ("[%+16" G_GINT64_FORMAT "]", debug_stamp);
    }

  fmt = g_strconcat (stamp, ":", format, NULL);
  g_free (stamp);

  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, fmt, var_args);

  g_free (fmt);
}

void
_clutter_debug_message (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  _clutter_debug_messagev (format, args);
  va_end (args);
}

gboolean
_clutter_diagnostic_enabled (void)
{
  static const char *clutter_enable_diagnostic = NULL;

  if (G_UNLIKELY (clutter_enable_diagnostic == NULL))
    {
      clutter_enable_diagnostic = g_getenv ("CLUTTER_ENABLE_DIAGNOSTIC");

      if (clutter_enable_diagnostic == NULL)
        clutter_enable_diagnostic = "0";
    }

  return *clutter_enable_diagnostic != '0';
}

void
_clutter_diagnostic_message (const char *format, ...)
{
  va_list args;
  char *fmt;

  fmt = g_strconcat ("[DIAGNOSTIC]: ", format, NULL);

  va_start (args, format);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, fmt, args);
  va_end (args);

  g_free (fmt);
}
