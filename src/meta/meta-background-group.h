/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_GROUP_H
#define META_BACKGROUND_GROUP_H

#include "clutter/clutter.h"

#include <meta/common.h>

#define META_TYPE_BACKGROUND_GROUP (meta_background_group_get_type ())

META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaBackgroundGroup,
                          meta_background_group,
                          META, BACKGROUND_GROUP,
                          ClutterActor)

struct _MetaBackgroundGroupClass
{
  ClutterActorClass parent_class;
};

META_EXPORT
ClutterActor *meta_background_group_new (void);

#endif /* META_BACKGROUND_GROUP_H */
