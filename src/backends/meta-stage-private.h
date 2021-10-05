/*
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_STAGE_PRIVATE_H
#define META_STAGE_PRIVATE_H

#include "backends/meta-cursor.h"
#include "meta/boxes.h"
#include "meta/meta-stage.h"
#include "meta/types.h"

G_BEGIN_DECLS

typedef struct _MetaStageWatch MetaStageWatch;
typedef struct _MetaOverlay    MetaOverlay;

typedef enum
{
  META_STAGE_WATCH_BEFORE_PAINT,
  META_STAGE_WATCH_AFTER_ACTOR_PAINT,
  META_STAGE_WATCH_AFTER_OVERLAY_PAINT,
  META_STAGE_WATCH_AFTER_PAINT,
} MetaStageWatchPhase;

typedef void (* MetaStageWatchFunc) (MetaStage           *stage,
                                     ClutterStageView    *view,
                                     ClutterPaintContext *paint_context,
                                     gpointer             user_data);

ClutterActor     *meta_stage_new                     (MetaBackend *backend);

MetaOverlay      *meta_stage_create_cursor_overlay   (MetaStage   *stage);
void              meta_stage_remove_cursor_overlay   (MetaStage   *stage,
						      MetaOverlay *overlay);

void              meta_stage_update_cursor_overlay   (MetaStage       *stage,
                                                      MetaOverlay     *overlay,
                                                      CoglTexture     *texture,
                                                      graphene_rect_t *rect);

void meta_stage_set_active (MetaStage *stage,
                            gboolean   is_active);

MetaStageWatch * meta_stage_watch_view (MetaStage           *stage,
                                        ClutterStageView    *view,
                                        MetaStageWatchPhase  watch_mode,
                                        MetaStageWatchFunc   callback,
                                        gpointer             user_data);

void meta_stage_remove_watch (MetaStage      *stage,
                              MetaStageWatch *watch);

G_END_DECLS

#endif /* META_STAGE_PRIVATE_H */
