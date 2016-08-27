/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_H
#define META_BACKGROUND_H

#include <clutter/clutter.h>

#include <meta/screen.h>

#define META_TYPE_BACKGROUND            (meta_background_get_type ())
#define META_BACKGROUND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_BACKGROUND, MetaBackground))
#define META_BACKGROUND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_BACKGROUND, MetaBackgroundClass))
#define META_IS_BACKGROUND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_BACKGROUND))
#define META_IS_BACKGROUND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_BACKGROUND))
#define META_BACKGROUND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_BACKGROUND, MetaBackgroundClass))

typedef struct _MetaBackground        MetaBackground;
typedef struct _MetaBackgroundClass   MetaBackgroundClass;
typedef struct _MetaBackgroundPrivate MetaBackgroundPrivate;

struct _MetaBackgroundClass
{
  ClutterActorClass parent_class;
};

struct _MetaBackground
{
  ClutterActor parent;

  MetaBackgroundPrivate *priv;
};

GType meta_background_get_type (void);

ClutterActor * meta_background_new (MetaScreen *screen);

void meta_background_set_layer           (MetaBackground       *self,
                                          CoglHandle           texture);
void meta_background_set_layer_wrap_mode (MetaBackground       *self,
                                          CoglMaterialWrapMode  wrap_mode);
void meta_background_set_visible_region  (MetaBackground       *self,
                                          cairo_region_t       *visible_region);

#endif /* META_BACKGROUND_H */
