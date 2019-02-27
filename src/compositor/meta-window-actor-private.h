/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_WINDOW_ACTOR_PRIVATE_H
#define META_WINDOW_ACTOR_PRIVATE_H

#include <config.h>

#include <X11/extensions/Xdamage.h>
#include <meta/compositor-muffin.h>
#include <clutter/clutter-muffin.h>

MetaWindowActor *meta_window_actor_new (MetaWindow *window);

void meta_window_actor_destroy   (MetaWindowActor *self);

void meta_window_actor_show (MetaWindowActor *self,
                             MetaCompEffect   effect);
void meta_window_actor_hide (MetaWindowActor *self,
                             MetaCompEffect   effect);

void meta_window_actor_maximize   (MetaWindowActor *self,
                                   MetaRectangle   *old_rect,
                                   MetaRectangle   *new_rect);
void meta_window_actor_unmaximize (MetaWindowActor *self,
                                   MetaRectangle   *old_rect,
                                   MetaRectangle   *new_rect);

void meta_window_actor_tile       (MetaWindowActor *self,
                                   MetaRectangle   *old_rect,
                                   MetaRectangle   *new_rect);

void meta_window_actor_process_damage (MetaWindowActor    *self,
                                       XDamageNotifyEvent *event);

void meta_window_actor_pre_paint      (MetaWindowActor    *self);
void meta_window_actor_post_paint     (MetaWindowActor    *self);

void meta_window_actor_invalidate_shadow (MetaWindowActor *self);

void meta_window_actor_set_redirected (MetaWindowActor *self, gboolean state);

gboolean meta_window_actor_should_unredirect (MetaWindowActor *self);

void meta_window_actor_get_shape_bounds (MetaWindowActor       *self,
                                          cairo_rectangle_int_t *bounds);

gboolean meta_window_actor_effect_in_progress  (MetaWindowActor *self);

void     meta_window_actor_sync_visibility     (MetaWindowActor *self);
void     meta_window_actor_update_shape        (MetaWindowActor *self);
void     meta_window_actor_update_opacity      (MetaWindowActor *self,
                                                guint8           opacity);
void     meta_window_actor_mapped              (MetaWindowActor *self);
void     meta_window_actor_unmapped            (MetaWindowActor *self);
void     meta_window_actor_set_updates_frozen  (MetaWindowActor *self,
                                                gboolean         updates_frozen);
void     meta_window_actor_queue_frame_drawn   (MetaWindowActor *self,
                                                gboolean         no_delay_frame);

cairo_region_t *meta_window_actor_get_obscured_region (MetaWindowActor *self);

void meta_window_actor_set_clip_region         (MetaWindowActor *self,
                                                cairo_region_t  *clip_region);
void meta_window_actor_set_clip_region_beneath (MetaWindowActor *self,
                                                cairo_region_t  *clip_region);
void meta_window_actor_reset_visible_regions      (MetaWindowActor *self);

void meta_window_actor_set_unobscured_region      (MetaWindowActor *self,
                                                   cairo_region_t  *unobscured_region);

void meta_window_actor_effect_completed (MetaWindowActor *actor,
                                         gulong           event);

void meta_window_actor_check_obscured (MetaWindowActor *self);
void set_obscured (MetaWindowActor *self,
                   gboolean         obscured);

void meta_window_actor_reset_texture (MetaWindowActor *self);

void meta_window_actor_decorated_notify (MetaWindowActor *self);
void meta_window_actor_appears_focused_notify (MetaWindowActor *self);
void meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                            gboolean         did_placement);

void meta_window_actor_override_obscured_internal (MetaWindowActor *self,
                                                   gboolean         obscured);

#endif /* META_WINDOW_ACTOR_PRIVATE_H */
