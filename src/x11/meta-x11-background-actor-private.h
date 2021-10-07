/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_X11_BACKGROUND_ACTOR_PRIVATE_H
#define META_X11_BACKGROUND_ACTOR_PRIVATE_H

#include <meta/display.h>
#include <meta/meta-x11-display.h>
#include <meta/meta-x11-background-actor.h>
#include "meta-x11-background.h"

void meta_x11_background_actor_set_visible_region  (MetaX11BackgroundActor *self,
                                                    cairo_region_t         *visible_region);

void meta_x11_background_actor_update              (MetaDisplay *display);
void meta_x11_background_actor_screen_size_changed (MetaDisplay *display);


#endif /* META_X11_BACKGROUND_ACTOR_PRIVATE_H */
