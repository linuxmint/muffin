/*
 * Zoom pointer grab.
 *
 * Cinnamon's magnifier (Accessibility > Zoom) can be driven by holding a
 * configurable modifier and turning the mouse wheel. On X11, scroll-valuator
 * motion events are delivered directly to the client window under the pointer
 * by the X server, so the magnified application keeps scrolling while the user
 * is zooming. The passive button-4/5 grab in keybindings.c cannot intercept
 * XI_Motion scroll events, so we hold a short-lived active pointer grab for
 * the duration of the zoom gesture; an active grab captures all pointer
 * events, including scroll-valuator motion. The grab is released shortly after
 * the last zoom-scroll event so normal pointer interaction resumes promptly.
 */

#include "config.h"
#include "core/zoom-grab.h"
#include "core/keybindings-private.h"
#include "backends/meta-backend-private.h"
#include "meta/meta-backend.h"

#define ZOOM_GRAB_IDLE_MS 350

static gboolean
zoom_pointer_grab_release (gpointer user_data)
{
  MetaDisplay *display = META_DISPLAY (user_data);
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (keys->zoom_pointer_grab_active)
    {
      meta_backend_ungrab_device (meta_get_backend (),
                                  META_VIRTUAL_CORE_POINTER_ID,
                                  CLUTTER_CURRENT_TIME);
      keys->zoom_pointer_grab_active = FALSE;
    }

  keys->zoom_pointer_grab_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

void
meta_zoom_pointer_grab_ensure (MetaDisplay *display,
                               uint32_t     timestamp)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (meta_is_wayland_compositor ())
    return;

  if (!keys->zoom_pointer_grab_active)
    {
      keys->zoom_pointer_grab_active =
        meta_backend_grab_device (meta_get_backend (),
                                  META_VIRTUAL_CORE_POINTER_ID,
                                  timestamp);
    }

  if (keys->zoom_pointer_grab_timeout_id)
    g_source_remove (keys->zoom_pointer_grab_timeout_id);
  keys->zoom_pointer_grab_timeout_id =
    g_timeout_add_full (G_PRIORITY_DEFAULT, ZOOM_GRAB_IDLE_MS,
                        zoom_pointer_grab_release, display, NULL);
}

void
meta_zoom_pointer_grab_release_all (MetaDisplay *display)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (keys->zoom_pointer_grab_timeout_id)
    {
      g_source_remove (keys->zoom_pointer_grab_timeout_id);
      keys->zoom_pointer_grab_timeout_id = 0;
    }

  if (keys->zoom_pointer_grab_active)
    {
      meta_backend_ungrab_device (meta_get_backend (),
                                  META_VIRTUAL_CORE_POINTER_ID,
                                  CLUTTER_CURRENT_TIME);
      keys->zoom_pointer_grab_active = FALSE;
    }
}
