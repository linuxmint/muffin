/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_ACTOR_PRIVATE_H
#define META_BACKGROUND_ACTOR_PRIVATE_H

#include <meta/screen.h>
#include <meta/meta-background-actor.h>

void meta_background_actor_set_visible_region  (MetaBackgroundActor *self,
                                                cairo_region_t      *visible_region);

cairo_region_t *meta_background_actor_get_visible_region (MetaBackgroundActor *self);

#endif /* META_BACKGROUND_ACTOR_PRIVATE_H */
