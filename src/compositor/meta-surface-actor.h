/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_SURFACE_ACTOR_PRIVATE_H
#define META_SURFACE_ACTOR_PRIVATE_H

#include "config.h"

#include "backends/meta-backend-types.h"
#include "meta/meta-shaped-texture.h"
#include "meta/window.h"

G_BEGIN_DECLS

#define META_TYPE_SURFACE_ACTOR (meta_surface_actor_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaSurfaceActor,
                          meta_surface_actor,
                          META, SURFACE_ACTOR,
                          ClutterActor)

struct _MetaSurfaceActorClass
{
  /*< private >*/
  ClutterActorClass parent_class;

  void     (* process_damage)    (MetaSurfaceActor *actor,
                                  int x, int y, int width, int height);
  void     (* pre_paint)         (MetaSurfaceActor *actor);
  gboolean (* is_opaque)         (MetaSurfaceActor *actor);
};

cairo_surface_t *meta_surface_actor_get_image (MetaSurfaceActor      *self,
                                               cairo_rectangle_int_t *clip);

MetaShapedTexture *meta_surface_actor_get_texture (MetaSurfaceActor *self);

void meta_surface_actor_update_area (MetaSurfaceActor *self,
                                     int               x,
                                     int               y,
                                     int               width,
                                     int               height);

gboolean meta_surface_actor_is_obscured (MetaSurfaceActor *self);

void meta_surface_actor_set_input_region (MetaSurfaceActor *self,
                                          cairo_region_t   *region);
void meta_surface_actor_set_opaque_region (MetaSurfaceActor *self,
                                           cairo_region_t   *region);
cairo_region_t * meta_surface_actor_get_opaque_region (MetaSurfaceActor *self);

void meta_surface_actor_process_damage (MetaSurfaceActor *actor,
                                        int x, int y, int width, int height);
void meta_surface_actor_pre_paint (MetaSurfaceActor *actor);
gboolean meta_surface_actor_is_opaque (MetaSurfaceActor *actor);

gboolean meta_surface_actor_is_frozen (MetaSurfaceActor *actor);
void meta_surface_actor_set_frozen (MetaSurfaceActor *actor,
                                    gboolean          frozen);

void meta_surface_actor_set_transform (MetaSurfaceActor     *self,
                                       MetaMonitorTransform  transform);
void meta_surface_actor_set_viewport_src_rect (MetaSurfaceActor *self,
                                               graphene_rect_t  *src_rect);
void meta_surface_actor_reset_viewport_src_rect (MetaSurfaceActor *self);
void meta_surface_actor_set_viewport_dst_size (MetaSurfaceActor  *self,
                                               int                dst_width,
                                               int                dst_height);
void meta_surface_actor_reset_viewport_dst_size (MetaSurfaceActor *self);
G_END_DECLS

#endif /* META_SURFACE_ACTOR_PRIVATE_H */
