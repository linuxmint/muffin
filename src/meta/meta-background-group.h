/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_GROUP_H
#define META_BACKGROUND_GROUP_H

#include <clutter/clutter.h>

/**
 * MetaBackgroundGroup:
 *
 * This class is a subclass of ClutterActor with special handling for
 * MetaBackgroundActor/MetaBackgroundGroup when painting children.
 * It makes sure to only draw the parts of the backgrounds not
 * occluded by opaque windows.
 *
 * See #MetaWindowGroup for more information behind the motivation,
 * and details on implementation.
 */

#define META_TYPE_BACKGROUND_GROUP            (meta_background_group_get_type ())
#define META_BACKGROUND_GROUP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_BACKGROUND_GROUP, MetaBackgroundGroup))
#define META_BACKGROUND_GROUP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_BACKGROUND_GROUP, MetaBackgroundGroupClass))
#define META_IS_BACKGROUND_GROUP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_BACKGROUND_GROUP))
#define META_IS_BACKGROUND_GROUP_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_BACKGROUND_GROUP))
#define META_BACKGROUND_GROUP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_BACKGROUND_GROUP, MetaBackgroundGroupClass))

typedef struct _MetaBackgroundGroup        MetaBackgroundGroup;
typedef struct _MetaBackgroundGroupClass   MetaBackgroundGroupClass;
typedef struct _MetaBackgroundGroupPrivate MetaBackgroundGroupPrivate;

struct _MetaBackgroundGroupClass
{
  ClutterActorClass parent_class;
};

struct _MetaBackgroundGroup
{
  ClutterActor parent;

  MetaBackgroundGroupPrivate *priv;
};

GType meta_background_group_get_type (void);

ClutterActor *meta_background_group_new (void);

#endif /* META_BACKGROUND_GROUP_H */
