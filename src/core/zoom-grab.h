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

#ifndef META_ZOOM_GRAB_H
#define META_ZOOM_GRAB_H

#include "core/display-private.h"

void meta_zoom_pointer_grab_ensure (MetaDisplay *display,
                                    uint32_t     timestamp);

void meta_zoom_pointer_grab_release_all (MetaDisplay *display);

#endif /* META_ZOOM_GRAB_H */
