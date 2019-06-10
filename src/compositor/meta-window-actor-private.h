/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_WINDOW_ACTOR_PRIVATE_H
#define META_WINDOW_ACTOR_PRIVATE_H

#include <config.h>

#include <X11/extensions/Xdamage.h>
#include <meta/compositor-muffin.h>
#include <clutter/clutter-muffin.h>
#include "meta-shadow-factory-private.h"

struct _MetaWindowActorPrivate
{
  MetaWindow       *window;
  Window            xwindow;
  MetaScreen       *screen;

  ClutterActor     *actor;

  /* MetaShadowFactory only caches shadows that are actually in use;
   * to avoid unnecessary recomputation we do two things: 1) we store
   * both a focused and unfocused shadow for the window. If the window
   * doesn't have different focused and unfocused shadow parameters,
   * these will be the same. 2) when the shadow potentially changes we
   * don't immediately unreference the old shadow, we just flag it as
   * dirty and recompute it when we next need it (recompute_focused_shadow,
   * recompute_unfocused_shadow.) Because of our extraction of
   * size-invariant window shape, we'll often find that the new shadow
   * is the same as the old shadow.
   */
  MetaShadow       *focused_shadow;
  MetaShadow       *unfocused_shadow;

  Pixmap            back_pixmap;

  Damage            damage;

  guint8            opacity;

  /* If the window is shaped, a region that matches the shape */
  cairo_region_t   *shape_region;
  /* The opaque region, from _NET_WM_OPAQUE_REGION, intersected with
   * the shape region. */
  cairo_region_t   *opaque_region;
  /* The region we should clip to when painting the shadow */
  cairo_region_t   *shadow_clip;

   /* The region that is visible, used to optimize out redraws */
  cairo_region_t   *unobscured_region;

  /* Extracted size-invariant shape used for shadows */
  MetaWindowShape  *shadow_shape;

  gint              last_width;
  gint              last_height;
  gint              last_x;
  gint              last_y;

  gint              freeze_count;

  char *            shadow_class;

  /*
   * These need to be counters rather than flags, since more plugins
   * can implement same effect; the practicality of stacking effects
   * might be dubious, but we have to at least handle it correctly.
   */
  gint              minimize_in_progress;
  gint              maximize_in_progress;
  gint              unmaximize_in_progress;
  gint              tile_in_progress;
  gint              map_in_progress;
  gint              destroy_in_progress;

  /* List of FrameData for recent frames */
  GList            *frames;

  guint		    visible                : 1;
  guint		    argb32                 : 1;
  guint		    disposed               : 1;
  guint             redecorating           : 1;

  guint		    needs_damage_all       : 1;
  guint		    received_damage        : 1;
  guint             repaint_scheduled      : 1;

  /* If set, the client needs to be sent a _NET_WM_FRAME_DRAWN
   * client message using the most recent frame in ->frames */
  guint             send_frame_messages_timer;
  gint64            frame_drawn_time;
  guint             needs_frame_drawn      : 1;

  guint             size_changed_id;
  guint             opacity_changed_id;

  guint		    needs_pixmap           : 1;
  guint             needs_reshape          : 1;
  guint             recompute_focused_shadow   : 1;
  guint             recompute_unfocused_shadow : 1;
  guint             size_changed               : 1;
  guint             position_changed           : 1;
  guint             updates_frozen         : 1;

  guint		    needs_destroy	   : 1;

  guint             no_shadow              : 1;

  guint             unredirected           : 1;
  guint unredirection_blocked;

  /* This is used to detect fullscreen windows that need to be unredirected */
  guint             full_damage_frames_count;
  guint             does_full_damage  : 1;

  guint             has_desat_effect : 1;

  guint             reshapes;
  guint             should_have_shadow : 1;
};

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
void     meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                                gboolean         did_placement);
void     meta_window_actor_sync_visibility     (MetaWindowActor *self);
void     meta_window_actor_update_shape        (MetaWindowActor *self);
void     meta_window_actor_update_opacity      (MetaWindowActor *self);
void     meta_window_actor_mapped              (MetaWindowActor *self);
void     meta_window_actor_unmapped            (MetaWindowActor *self);
void     meta_window_actor_set_updates_frozen  (MetaWindowActor *self,
                                                gboolean         updates_frozen);
void     meta_window_actor_queue_frame_drawn   (MetaWindowActor *self,
                                                gboolean         no_delay_frame);

cairo_region_t *meta_window_actor_get_obscured_region (MetaWindowActor *self);

void meta_window_actor_set_visible_region         (MetaWindowActor *self,
                                                   cairo_region_t  *visible_region);
void meta_window_actor_set_visible_region_beneath (MetaWindowActor *self,
                                                   cairo_region_t  *beneath_region);
void meta_window_actor_reset_visible_regions      (MetaWindowActor *self);

void meta_window_actor_set_unobscured_region      (MetaWindowActor *self,
                                                   cairo_region_t  *unobscured_region);

void meta_window_actor_effect_completed (MetaWindowActor *actor,
                                         gulong           event);

#endif /* META_WINDOW_ACTOR_PRIVATE_H */
