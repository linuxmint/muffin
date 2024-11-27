/* Clutter.
 * An OpenGL based 'interactive canvas' library.
 * Authored By Matthew Allum  <mallum@openedhand.com>
 * Copyright (C) 2006-2007 OpenedHand
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
 *
 */

#include "clutter-build-config.h"

#include <string.h>

#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>

#include <errno.h>

#include "clutter-backend-x11.h"
#include "clutter-settings-x11.h"
#include "clutter-x11.h"

#include "xsettings/xsettings-common.h"

#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XInput2.h>

#include <cogl/cogl.h>
#include <cogl/cogl-xlib.h>

#include "clutter-backend.h"
#include "clutter-debug.h"
#include "clutter-event-private.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-settings-private.h"

G_DEFINE_TYPE (ClutterBackendX11, clutter_backend_x11, CLUTTER_TYPE_BACKEND)

GType
clutter_x11_filter_return_get_type (void)
{
  static gsize g_define_type;

  if (g_once_init_enter (&g_define_type))
    {
      static const GEnumValue values[] = {
        { CLUTTER_X11_FILTER_CONTINUE, "CLUTTER_X11_FILTER_CONTINUE", "continue" },
        { CLUTTER_X11_FILTER_TRANSLATE, "CLUTTER_X11_FILTER_TRANSLATE", "translate" },
        { CLUTTER_X11_FILTER_REMOVE, "CLUTTER_X11_FILTER_REMOVE", "remove" },
        { 0, NULL, NULL },
      };

      GType id =
        g_enum_register_static (g_intern_static_string ("ClutterX11FilterReturn"), values);

      g_once_init_leave (&g_define_type, id);
    }

  return g_define_type;
}

/* atoms; remember to add the code that assigns the atom value to
 * the member of the ClutterBackendX11 structure if you add an
 * atom name here. do not change the order!
 */
static const gchar *atom_names[] = {
  "_NET_WM_PID",
  "_NET_WM_PING",
  "_NET_WM_STATE",
  "_NET_WM_USER_TIME",
  "WM_PROTOCOLS",
  "WM_DELETE_WINDOW",
  "_XEMBED",
  "_XEMBED_INFO",
  "_NET_WM_NAME",
  "UTF8_STRING",
};

#define N_ATOM_NAMES G_N_ELEMENTS (atom_names)

/* various flags corresponding to pre init setup calls */
static gboolean clutter_enable_xinput = TRUE;
static gboolean clutter_enable_argb = FALSE;
static gboolean clutter_enable_stereo = FALSE;
static Display  *_foreign_dpy = NULL;

/* options */
static gchar *clutter_display_name = NULL;
static gint clutter_screen = -1;
static gboolean clutter_synchronise = FALSE;

/* X error trap */
static int TrappedErrorCode = 0;
static int (* old_error_handler) (Display *, XErrorEvent *);

static ClutterX11FilterReturn
xsettings_filter (XEvent       *xevent,
                  ClutterEvent *event,
                  gpointer      data)
{
  ClutterBackendX11 *backend_x11 = data;

  _clutter_xsettings_client_process_event (backend_x11->xsettings, xevent);

  /* we always want the rest of the stack to get XSettings events, even
   * if Clutter already handled them
   */

  return CLUTTER_X11_FILTER_CONTINUE;
}

static ClutterX11FilterReturn
cogl_xlib_filter (XEvent       *xevent,
                  ClutterEvent *event,
                  gpointer      data)
{
  ClutterBackend *backend = data;
  ClutterX11FilterReturn retval;
  CoglFilterReturn ret;

  ret = cogl_xlib_renderer_handle_event (backend->cogl_renderer, xevent);
  switch (ret)
    {
    case COGL_FILTER_REMOVE:
      retval = CLUTTER_X11_FILTER_REMOVE;
      break;

    case COGL_FILTER_CONTINUE:
    default:
      retval = CLUTTER_X11_FILTER_CONTINUE;
      break;
    }

  return retval;
}

static void
clutter_backend_x11_xsettings_notify (const char       *name,
                                      XSettingsAction   action,
                                      XSettingsSetting *setting,
                                      void             *cb_data)
{
  ClutterSettings *settings = clutter_settings_get_default ();
  gint i;

  if (name == NULL || *name == '\0')
    return;

  if (setting == NULL)
    return;

  g_object_freeze_notify (G_OBJECT (settings));

  for (i = 0; i < _n_clutter_settings_map; i++)
    {
      if (g_strcmp0 (name, CLUTTER_SETTING_X11_NAME (i)) == 0)
        {
          GValue value = G_VALUE_INIT;

          switch (setting->type)
            {
            case XSETTINGS_TYPE_INT:
              g_value_init (&value, G_TYPE_INT);
              g_value_set_int (&value, setting->data.v_int);
              break;

            case XSETTINGS_TYPE_STRING:
              g_value_init (&value, G_TYPE_STRING);
              g_value_set_string (&value, setting->data.v_string);
              break;

            case XSETTINGS_TYPE_COLOR:
              {
                ClutterColor color;

                color.red   = (guint8) ((float) setting->data.v_color.red
                            / 65535.0 * 255);
                color.green = (guint8) ((float) setting->data.v_color.green
                            / 65535.0 * 255);
                color.blue  = (guint8) ((float) setting->data.v_color.blue
                            / 65535.0 * 255);
                color.alpha = (guint8) ((float) setting->data.v_color.alpha
                            / 65535.0 * 255);

                g_value_init (&value, G_TYPE_BOXED);
                clutter_value_set_color (&value, &color);
              }
              break;
            }

          CLUTTER_NOTE (BACKEND,
                        "Mapping XSETTING '%s' to 'ClutterSettings:%s'",
                        CLUTTER_SETTING_X11_NAME (i),
                        CLUTTER_SETTING_PROPERTY (i));

          clutter_settings_set_property_internal (settings,
                                                  CLUTTER_SETTING_PROPERTY (i),
                                                  &value);

          g_value_unset (&value);

          break;
        }
    }

  g_object_thaw_notify (G_OBJECT (settings));
}

static gboolean
clutter_backend_x11_pre_parse (ClutterBackend  *backend,
                               GError         **error)
{
  const gchar *env_string;

  /* we don't fail here if DISPLAY is not set, as the user
   * might pass the --display command line switch
   */
  env_string = g_getenv ("DISPLAY");
  if (env_string)
    {
      clutter_display_name = g_strdup (env_string);
      env_string = NULL;
    }

  env_string = g_getenv ("CLUTTER_DISABLE_ARGB_VISUAL");
  if (env_string)
    {
      clutter_enable_argb = FALSE;
      env_string = NULL;
    }

  env_string = g_getenv ("CLUTTER_DISABLE_XINPUT");
  if (env_string)
    {
      clutter_enable_xinput = FALSE;
      env_string = NULL;
    }

  return TRUE;
}

static gboolean
clutter_backend_x11_post_parse (ClutterBackend  *backend,
                                GError         **error)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (backend);
  Atom atoms[N_ATOM_NAMES];

  if (_foreign_dpy)
    backend_x11->xdpy = _foreign_dpy;

  /* Only open connection if not already set by prior call to
   * clutter_x11_set_display()
   */
  if (backend_x11->xdpy == NULL)
    {
      if (clutter_display_name != NULL &&
          *clutter_display_name != '\0')
	{
	  CLUTTER_NOTE (BACKEND, "XOpenDisplay on '%s'", clutter_display_name);

	  backend_x11->xdpy = XOpenDisplay (clutter_display_name);
          if (backend_x11->xdpy == NULL)
            {
              g_set_error (error, CLUTTER_INIT_ERROR,
                           CLUTTER_INIT_ERROR_BACKEND,
                           "Unable to open display '%s'",
                           clutter_display_name);
              return FALSE;
            }
	}
      else
	{
	  g_set_error_literal (error, CLUTTER_INIT_ERROR,
                               CLUTTER_INIT_ERROR_BACKEND,
                               "Unable to open display. You have to set the "
                               "DISPLAY environment variable, or use the "
                               "--display command line argument");
	  return FALSE;
	}
    }

  g_assert (backend_x11->xdpy != NULL);

  CLUTTER_NOTE (BACKEND, "Getting the X screen");

  /* add event filter for Cogl events */
  clutter_x11_add_filter (cogl_xlib_filter, backend);

  if (clutter_screen == -1)
    backend_x11->xscreen = DefaultScreenOfDisplay (backend_x11->xdpy);
  else
    backend_x11->xscreen = ScreenOfDisplay (backend_x11->xdpy,
                                            clutter_screen);

  backend_x11->xscreen_num = XScreenNumberOfScreen (backend_x11->xscreen);
  backend_x11->xscreen_width = WidthOfScreen (backend_x11->xscreen);
  backend_x11->xscreen_height = HeightOfScreen (backend_x11->xscreen);

  backend_x11->xwin_root = RootWindow (backend_x11->xdpy,
                                       backend_x11->xscreen_num);

  backend_x11->display_name = g_strdup (clutter_display_name);

  /* create XSETTINGS client */
  backend_x11->xsettings =
    _clutter_xsettings_client_new (backend_x11->xdpy,
                                   backend_x11->xscreen_num,
                                   clutter_backend_x11_xsettings_notify,
                                   NULL,
                                   backend_x11);

  /* add event filter for XSETTINGS events */
  clutter_x11_add_filter (xsettings_filter, backend_x11);

  if (clutter_synchronise)
    XSynchronize (backend_x11->xdpy, True);

  XInternAtoms (backend_x11->xdpy,
                (char **) atom_names, N_ATOM_NAMES,
                False, atoms);

  backend_x11->atom_NET_WM_PID = atoms[0];
  backend_x11->atom_NET_WM_PING = atoms[1];
  backend_x11->atom_NET_WM_STATE = atoms[2];
  backend_x11->atom_NET_WM_USER_TIME = atoms[3];
  backend_x11->atom_WM_PROTOCOLS = atoms[4];
  backend_x11->atom_WM_DELETE_WINDOW = atoms[5];
  backend_x11->atom_XEMBED = atoms[6];
  backend_x11->atom_XEMBED_INFO = atoms[7];
  backend_x11->atom_NET_WM_NAME = atoms[8];
  backend_x11->atom_UTF8_STRING = atoms[9];

  g_free (clutter_display_name);

  CLUTTER_NOTE (BACKEND,
                "X Display '%s'[%p] opened (screen:%d, root:%u, dpi:%f)",
                backend_x11->display_name,
                backend_x11->xdpy,
                backend_x11->xscreen_num,
                (unsigned int) backend_x11->xwin_root,
                clutter_backend_get_resolution (backend));

  return TRUE;
}

static const GOptionEntry entries[] =
{
  {
    "display", 0,
    G_OPTION_FLAG_IN_MAIN,
    G_OPTION_ARG_STRING, &clutter_display_name,
    N_("X display to use"), "DISPLAY"
  },
  {
    "screen", 0,
    G_OPTION_FLAG_IN_MAIN,
    G_OPTION_ARG_INT, &clutter_screen,
    N_("X screen to use"), "SCREEN"
  },
  { "synch", 0,
    0,
    G_OPTION_ARG_NONE, &clutter_synchronise,
    N_("Make X calls synchronous"), NULL
  },
  {
    "disable-xinput", 0,
    G_OPTION_FLAG_REVERSE,
    G_OPTION_ARG_NONE, &clutter_enable_xinput,
    N_("Disable XInput support"), NULL
  },
  { NULL }
};

static void
clutter_backend_x11_add_options (ClutterBackend *backend,
                                 GOptionGroup   *group)
{
  g_option_group_add_entries (group, entries);
}

static void
clutter_backend_x11_finalize (GObject *gobject)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (gobject);

  g_free (backend_x11->display_name);

  clutter_x11_remove_filter (cogl_xlib_filter, gobject);

  clutter_x11_remove_filter (xsettings_filter, backend_x11);
  _clutter_xsettings_client_destroy (backend_x11->xsettings);

  XCloseDisplay (backend_x11->xdpy);

  G_OBJECT_CLASS (clutter_backend_x11_parent_class)->finalize (gobject);
}

static void
clutter_backend_x11_dispose (GObject *gobject)
{
  G_OBJECT_CLASS (clutter_backend_x11_parent_class)->dispose (gobject);
}

static ClutterFeatureFlags
clutter_backend_x11_get_features (ClutterBackend *backend)
{
  ClutterFeatureFlags flags = CLUTTER_FEATURE_STAGE_CURSOR;

  flags |= CLUTTER_BACKEND_CLASS (clutter_backend_x11_parent_class)->get_features (backend);

  return flags;
}

static void
update_last_event_time (ClutterBackendX11 *backend_x11,
                        XEvent            *xevent)
{
  Time current_time = CurrentTime;
  Time last_time = backend_x11->last_event_time;

  switch (xevent->type)
    {
    case KeyPress:
    case KeyRelease:
      current_time = xevent->xkey.time;
      break;

    case ButtonPress:
    case ButtonRelease:
      current_time = xevent->xbutton.time;
      break;

    case MotionNotify:
      current_time = xevent->xmotion.time;
      break;

    case EnterNotify:
    case LeaveNotify:
      current_time = xevent->xcrossing.time;
      break;

    case PropertyNotify:
      current_time = xevent->xproperty.time;
      break;

    default:
      break;
    }

  /* only change the current event time if it's after the previous event
   * time, or if it is at least 30 seconds earlier - in case the system
   * clock was changed
   */
  if ((current_time != CurrentTime) &&
      (current_time > last_time || (last_time - current_time > (30 * 1000))))
    backend_x11->last_event_time = current_time;
}

static gboolean
clutter_backend_x11_translate_event (ClutterBackend *backend,
                                     gpointer        native,
                                     ClutterEvent   *event)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (backend);
  XEvent *xevent = native;

  /* X11 filter functions have a higher priority */
  if (backend_x11->event_filters != NULL)
    {
      GSList *node = backend_x11->event_filters;

      while (node != NULL)
        {
          ClutterX11EventFilter *filter = node->data;

          switch (filter->func (xevent, event, filter->data))
            {
            case CLUTTER_X11_FILTER_CONTINUE:
              break;

            case CLUTTER_X11_FILTER_TRANSLATE:
              return TRUE;

            case CLUTTER_X11_FILTER_REMOVE:
              return FALSE;

            default:
              break;
            }

          node = node->next;
        }
    }

  /* we update the event time only for events that can
   * actually reach Clutter's event queue
   */
  update_last_event_time (backend_x11, xevent);

  return FALSE;
}

static CoglRenderer *
clutter_backend_x11_get_renderer (ClutterBackend  *backend,
                                  GError         **error)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (backend);
  Display *xdisplay = backend_x11->xdpy;
  CoglRenderer *renderer;

  CLUTTER_NOTE (BACKEND, "Creating a new Xlib renderer");

  renderer = cogl_renderer_new ();

  cogl_renderer_add_constraint (renderer, COGL_RENDERER_CONSTRAINT_USES_X11);

  /* set the display object we're using */
  cogl_xlib_renderer_set_foreign_display (renderer, xdisplay);

  return renderer;
}

static gboolean
check_onscreen_template (CoglRenderer         *renderer,
                         CoglSwapChain        *swap_chain,
                         CoglOnscreenTemplate *onscreen_template,
                         gboolean              enable_argb,
                         gboolean              enable_stereo,
                         GError              **error)
{
  GError *internal_error = NULL;

  cogl_swap_chain_set_has_alpha (swap_chain, enable_argb);
  cogl_onscreen_template_set_stereo_enabled (onscreen_template,
					     clutter_enable_stereo);

  /* cogl_renderer_check_onscreen_template() is actually just a
   * shorthand for creating a CoglDisplay, and calling
   * cogl_display_setup() on it, then throwing the display away. If we
   * could just return that display, then it would be more efficient
   * not to use cogl_renderer_check_onscreen_template(). However, the
   * backend API requires that we return an CoglDisplay that has not
   * yet been setup, so one way or the other we'll have to discard the
   * first display and make a new fresh one.
   */
  if (cogl_renderer_check_onscreen_template (renderer, onscreen_template, &internal_error))
    {
      clutter_enable_argb = enable_argb;
      clutter_enable_stereo = enable_stereo;

      return TRUE;
    }
  else
    {
      if (enable_argb || enable_stereo) /* More possibilities to try */
        CLUTTER_NOTE (BACKEND,
                      "Creation of a CoglDisplay with alpha=%s, stereo=%s failed: %s",
                      enable_argb ? "enabled" : "disabled",
                      enable_stereo ? "enabled" : "disabled",
                      internal_error != NULL
                        ?  internal_error->message
                        : "Unknown reason");
      else
        g_set_error_literal (error, CLUTTER_INIT_ERROR,
                             CLUTTER_INIT_ERROR_BACKEND,
                             internal_error != NULL
                               ? internal_error->message
                               : "Creation of a CoglDisplay failed");

      g_clear_error (&internal_error);

      return FALSE;
    }
}

static CoglDisplay *
clutter_backend_x11_get_display (ClutterBackend  *backend,
                                 CoglRenderer    *renderer,
                                 CoglSwapChain   *swap_chain,
                                 GError         **error)
{
  CoglOnscreenTemplate *onscreen_template;
  CoglDisplay *display = NULL;
  gboolean res = FALSE;

  CLUTTER_NOTE (BACKEND, "Creating CoglDisplay, alpha=%s, stereo=%s",
                clutter_enable_argb ? "enabled" : "disabled",
                clutter_enable_stereo ? "enabled" : "disabled");

  onscreen_template = cogl_onscreen_template_new (swap_chain);

  /* It's possible that the current renderer doesn't support transparency
   * or doesn't support stereo, so we try the different combinations.
   */
  if (clutter_enable_argb && clutter_enable_stereo)
    res = check_onscreen_template (renderer, swap_chain, onscreen_template,
                                  TRUE, TRUE, error);

  /* Prioritize stereo over alpha */
  if (!res && clutter_enable_stereo)
    res = check_onscreen_template (renderer, swap_chain, onscreen_template,
                                  FALSE, TRUE, error);

  if (!res && clutter_enable_argb)
    res = check_onscreen_template (renderer, swap_chain, onscreen_template,
                                  TRUE, FALSE, error);

  if (!res)
    res = check_onscreen_template (renderer, swap_chain, onscreen_template,
                                  FALSE, FALSE, error);

  if (res)
    display = cogl_display_new (renderer, onscreen_template);

  cogl_object_unref (onscreen_template);

  return display;
}

static void
clutter_backend_x11_class_init (ClutterBackendX11Class *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterBackendClass *backend_class = CLUTTER_BACKEND_CLASS (klass);

  gobject_class->dispose = clutter_backend_x11_dispose;
  gobject_class->finalize = clutter_backend_x11_finalize;

  backend_class->pre_parse = clutter_backend_x11_pre_parse;
  backend_class->post_parse = clutter_backend_x11_post_parse;
  backend_class->add_options = clutter_backend_x11_add_options;
  backend_class->get_features = clutter_backend_x11_get_features;

  backend_class->translate_event = clutter_backend_x11_translate_event;

  backend_class->get_renderer = clutter_backend_x11_get_renderer;
  backend_class->get_display = clutter_backend_x11_get_display;
}

static void
clutter_backend_x11_init (ClutterBackendX11 *backend_x11)
{
  backend_x11->last_event_time = CurrentTime;
}

ClutterBackend *
clutter_backend_x11_new (void)
{
  return g_object_new (CLUTTER_TYPE_BACKEND_X11, NULL);
}

static int
error_handler(Display     *xdpy,
              XErrorEvent *error)
{
  TrappedErrorCode = error->error_code;
  return 0;
}

/**
 * clutter_x11_trap_x_errors:
 *
 * Traps every X error until clutter_x11_untrap_x_errors() is called.
 *
 * Since: 0.6
 */
void
clutter_x11_trap_x_errors (void)
{
  TrappedErrorCode  = 0;
  old_error_handler = XSetErrorHandler (error_handler);
}

/**
 * clutter_x11_untrap_x_errors:
 *
 * Removes the X error trap and returns the current status.
 *
 * Return value: the trapped error code, or 0 for success
 *
 * Since: 0.4
 */
gint
clutter_x11_untrap_x_errors (void)
{
  XSetErrorHandler (old_error_handler);

  return TrappedErrorCode;
}

/**
 * clutter_x11_get_default_display:
 *
 * Retrieves the pointer to the default display.
 *
 * Return value: (transfer none): the default display
 *
 * Since: 0.6
 */
Display *
clutter_x11_get_default_display (void)
{
  ClutterBackend *backend = clutter_get_default_backend ();

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return NULL;
    }

  if (!CLUTTER_IS_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return NULL;
    }

  return CLUTTER_BACKEND_X11 (backend)->xdpy;
}

/**
 * clutter_x11_set_display:
 * @xdpy: pointer to a X display connection.
 *
 * Sets the display connection Clutter should use; must be called
 * before clutter_init(), clutter_init_with_args() or other functions
 * pertaining Clutter's initialization process.
 *
 * If you are parsing the command line arguments by retrieving Clutter's
 * #GOptionGroup with clutter_get_option_group() and calling
 * g_option_context_parse() yourself, you should also call
 * clutter_x11_set_display() before g_option_context_parse().
 *
 * Since: 0.8
 */
void
clutter_x11_set_display (Display *xdpy)
{
  if (_clutter_context_is_initialized ())
    {
      g_warning ("%s() can only be used before calling clutter_init()",
                 G_STRFUNC);
      return;
    }

  _foreign_dpy= xdpy;
}

/**
 * clutter_x11_get_default_screen:
 *
 * Gets the number of the default X Screen object.
 *
 * Return value: the number of the default screen
 *
 * Since: 0.6
 */
int
clutter_x11_get_default_screen (void)
{
 ClutterBackend *backend = clutter_get_default_backend ();

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return 0;
    }

  if (!CLUTTER_IS_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return 0;
    }

  return CLUTTER_BACKEND_X11 (backend)->xscreen_num;
}

/**
 * clutter_x11_get_root_window: (skip)
 *
 * Retrieves the root window.
 *
 * Return value: the id of the root window
 *
 * Since: 0.6
 */
Window
clutter_x11_get_root_window (void)
{
 ClutterBackend *backend = clutter_get_default_backend ();

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return None;
    }

  if (!CLUTTER_IS_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return None;
    }

  return CLUTTER_BACKEND_X11 (backend)->xwin_root;
}

/**
 * clutter_x11_add_filter: (skip)
 * @func: a filter function
 * @data: user data to be passed to the filter function, or %NULL
 *
 * Adds an event filter function.
 *
 * Since: 0.6
 */
void
clutter_x11_add_filter (ClutterX11FilterFunc func,
                        gpointer             data)
{
  ClutterX11EventFilter *filter;
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterBackendX11 *backend_x11;

  g_return_if_fail (func != NULL);

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return;
    }

  if (!CLUTTER_IS_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return;
    }

  backend_x11 = CLUTTER_BACKEND_X11 (backend);

  filter = g_new0 (ClutterX11EventFilter, 1);
  filter->func = func;
  filter->data = data;

  backend_x11->event_filters =
    g_slist_append (backend_x11->event_filters, filter);

  return;
}

/**
 * clutter_x11_remove_filter: (skip)
 * @func: a filter function
 * @data: user data to be passed to the filter function, or %NULL
 *
 * Removes the given filter function.
 *
 * Since: 0.6
 */
void
clutter_x11_remove_filter (ClutterX11FilterFunc func,
                           gpointer             data)
{
  GSList                *tmp_list, *this;
  ClutterX11EventFilter *filter;
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterBackendX11 *backend_x11;

  g_return_if_fail (func != NULL);

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return;
    }

  if (!CLUTTER_IS_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return;
    }

  backend_x11 = CLUTTER_BACKEND_X11 (backend);

  tmp_list = backend_x11->event_filters;

  while (tmp_list)
    {
      filter   = tmp_list->data;
      this     =  tmp_list;
      tmp_list = tmp_list->next;

      if (filter->func == func && filter->data == data)
        {
          backend_x11->event_filters =
            g_slist_remove_link (backend_x11->event_filters, this);

          g_slist_free_1 (this);
          g_free (filter);

          return;
        }
    }
}

/**
 * clutter_x11_has_composite_extension:
 *
 * Retrieves whether Clutter is running on an X11 server with the
 * XComposite extension
 *
 * Return value: %TRUE if the XComposite extension is available
 */
gboolean
clutter_x11_has_composite_extension (void)
{
  static gboolean have_composite = FALSE, done_check = FALSE;
  int error = 0, event = 0;
  Display *dpy;

  if (done_check)
    return have_composite;

  if (!_clutter_context_is_initialized ())
    {
      g_critical ("X11 backend has not been initialised");
      return FALSE;
    }

  dpy = clutter_x11_get_default_display();
  if (dpy == NULL)
    return FALSE;

  if (XCompositeQueryExtension (dpy, &event, &error))
    {
      int major = 0, minor = 0;
      if (XCompositeQueryVersion (dpy, &major, &minor))
        {
          if (major >= 0 && minor >= 3)
            have_composite = TRUE;
        }
    }

  done_check = TRUE;

  return have_composite;
}

/**
 * clutter_x11_set_use_argb_visual:
 * @use_argb: %TRUE if ARGB visuals should be requested by default
 *
 * Sets whether the Clutter X11 backend should request ARGB visuals by default
 * or not.
 *
 * By default, Clutter requests RGB visuals.
 *
 * If no ARGB visuals are found, the X11 backend will fall back to
 * requesting a RGB visual instead.
 *
 * ARGB visuals are required for the #ClutterStage:use-alpha property to work.
 *
 * This function can only be called once, and before clutter_init() is
 * called.
 *
 * Since: 1.2
 */
void
clutter_x11_set_use_argb_visual (gboolean use_argb)
{
  if (_clutter_context_is_initialized ())
    {
      g_warning ("%s() can only be used before calling clutter_init()",
                 G_STRFUNC);
      return;
    }

  CLUTTER_NOTE (BACKEND, "ARGB visuals are %s",
                use_argb ? "enabled" : "disabled");

  clutter_enable_argb = use_argb;
}

/**
 * clutter_x11_get_use_argb_visual:
 *
 * Retrieves whether the Clutter X11 backend is using ARGB visuals by default
 *
 * Return value: %TRUE if ARGB visuals are queried by default
 *
 * Since: 1.2
 */
gboolean
clutter_x11_get_use_argb_visual (void)
{
  return clutter_enable_argb;
}

/**
 * clutter_x11_set_use_stereo_stage:
 * @use_stereo: %TRUE if the stereo stages should be used if possible.
 *
 * Sets whether the backend object for Clutter stages, will,
 * if possible, be created with the ability to support stereo drawing
 * (drawing separate images for the left and right eyes).
 *
 * This function must be called before clutter_init() is called.
 * During paint callbacks, cogl_framebuffer_is_stereo() can be called
 * on the framebuffer retrieved by cogl_get_draw_framebuffer() to
 * determine if stereo support was successfully enabled, and
 * cogl_framebuffer_set_stereo_mode() to determine which buffers
 * will be drawn to.
 *
 * Note that this function *does not* cause the stage to be drawn
 * multiple times with different perspective transformations and thus
 * appear in 3D, it simply enables individual ClutterActors to paint
 * different images for the left and and right eye.
 *
 * Since: 1.22
 */
void
clutter_x11_set_use_stereo_stage (gboolean use_stereo)
{
  if (_clutter_context_is_initialized ())
    {
      g_warning ("%s() can only be used before calling clutter_init()",
                 G_STRFUNC);
      return;
    }

  CLUTTER_NOTE (BACKEND, "STEREO stages are %s",
                use_stereo ? "enabled" : "disabled");

  clutter_enable_stereo = use_stereo;
}

/**
 * clutter_x11_get_use_stereo_stage:
 *
 * Retrieves whether the Clutter X11 backend will create stereo
 * stages if possible.
 *
 * Return value: %TRUE if stereo stages are used if possible
 *
 * Since: 1.22
 */
gboolean
clutter_x11_get_use_stereo_stage (void)
{
  return clutter_enable_stereo;
}

