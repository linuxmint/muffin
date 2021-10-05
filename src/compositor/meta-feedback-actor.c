/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2014 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * SECTION:meta-feedback-actor
 * @title: MetaFeedbackActor
 * @short_description: Actor for painting user interaction feedback
 */

#include "config.h"

#include "compositor/compositor-private.h"
#include "compositor/meta-feedback-actor-private.h"
#include "core/display-private.h"

enum
{
  PROP_ANCHOR_X = 1,
  PROP_ANCHOR_Y
};

typedef struct _MetaFeedbackActorPrivate MetaFeedbackActorPrivate;

struct _MetaFeedbackActorPrivate
{
  float anchor_x;
  float anchor_y;
  float pos_x;
  float pos_y;

  int geometry_scale;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaFeedbackActor, meta_feedback_actor, CLUTTER_TYPE_ACTOR)

static void
meta_feedback_actor_constructed (GObject *object)
{
  MetaDisplay *display;
  ClutterActor *feedback_group;

  display = meta_get_display ();
  feedback_group = meta_get_feedback_group_for_display (display);
  clutter_actor_add_child (feedback_group, CLUTTER_ACTOR (object));
}

static void
meta_feedback_actor_update_position (MetaFeedbackActor *self)
{
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);

  clutter_actor_set_position (CLUTTER_ACTOR (self),
                              priv->pos_x -
                              (priv->anchor_x * priv->geometry_scale),
                              priv->pos_y -
                              (priv->anchor_y * priv->geometry_scale));
}

static void
meta_feedback_actor_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ANCHOR_X:
      priv->anchor_x = g_value_get_int (value);
      meta_feedback_actor_update_position (self);
      break;
    case PROP_ANCHOR_Y:
      priv->anchor_y = g_value_get_int (value);
      meta_feedback_actor_update_position (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_feedback_actor_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  MetaFeedbackActor *self = META_FEEDBACK_ACTOR (object);
  MetaFeedbackActorPrivate *priv = meta_feedback_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ANCHOR_X:
      g_value_set_float (value, priv->anchor_x);
      break;
    case PROP_ANCHOR_Y:
      g_value_set_float (value, priv->anchor_y);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_feedback_actor_class_init (MetaFeedbackActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  object_class->constructed = meta_feedback_actor_constructed;
  object_class->set_property = meta_feedback_actor_set_property;
  object_class->get_property = meta_feedback_actor_get_property;

  pspec = g_param_spec_float ("anchor-x",
                              "Anchor X",
                              "The X axis of the anchor point",
                              0, G_MAXFLOAT, 0,
                              G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_ANCHOR_X,
                                   pspec);

  pspec = g_param_spec_float ("anchor-y",
                              "Anchor Y",
                              "The Y axis of the anchor point",
                              0, G_MAXFLOAT, 0,
                              G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_ANCHOR_Y,
                                   pspec);
}

static void
meta_feedback_actor_init (MetaFeedbackActor *self)
{
  clutter_actor_set_reactive (CLUTTER_ACTOR (self), FALSE);
}

/**
 * meta_feedback_actor_new:
 *
 * Creates a new actor to draw the current drag and drop surface.
 *
 * Return value: the newly created background actor
 */
ClutterActor *
meta_feedback_actor_new (float anchor_x,
                         float anchor_y)
{
  MetaFeedbackActor *self;

  self = g_object_new (META_TYPE_FEEDBACK_ACTOR,
                       "anchor-x", anchor_x,
                       "anchor-y", anchor_y,
                       NULL);

  return CLUTTER_ACTOR (self);
}

void
meta_feedback_actor_set_anchor (MetaFeedbackActor *self,
                                float              anchor_x,
                                float              anchor_y)
{
  MetaFeedbackActorPrivate *priv;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));

  priv = meta_feedback_actor_get_instance_private (self);

  if (priv->anchor_x == anchor_x && priv->anchor_y == anchor_y)
    return;

  if (priv->anchor_x != anchor_x)
    {
      priv->anchor_x = anchor_x;
      g_object_notify (G_OBJECT (self), "anchor-x");
    }

  if (priv->anchor_y != anchor_y)
    {
      priv->anchor_y = anchor_y;
      g_object_notify (G_OBJECT (self), "anchor-y");
    }

  meta_feedback_actor_update_position (self);
}

void
meta_feedback_actor_get_anchor (MetaFeedbackActor *self,
                                float             *anchor_x,
                                float             *anchor_y)
{
  MetaFeedbackActorPrivate *priv;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));

  priv = meta_feedback_actor_get_instance_private (self);

  if (anchor_x)
    *anchor_x = priv->anchor_x;
  if (anchor_y)
    *anchor_y = priv->anchor_y;
}

void
meta_feedback_actor_set_position (MetaFeedbackActor  *self,
                                  float               x,
                                  float               y)
{
  MetaFeedbackActorPrivate *priv;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));

  priv = meta_feedback_actor_get_instance_private (self);
  priv->pos_x = x;
  priv->pos_y = y;

  meta_feedback_actor_update_position (self);
}

void
meta_feedback_actor_update (MetaFeedbackActor  *self,
                            const ClutterEvent *event)
{
  graphene_point_t point;

  g_return_if_fail (META_IS_FEEDBACK_ACTOR (self));
  g_return_if_fail (event != NULL);

  clutter_event_get_position (event, &point);
  meta_feedback_actor_set_position (self, point.x, point.y);
}

void
meta_feedback_actor_set_geometry_scale (MetaFeedbackActor *self,
                                        int                geometry_scale)
{
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);
  CoglMatrix child_transform;

  if (priv->geometry_scale == geometry_scale)
    return;

  priv->geometry_scale = geometry_scale;

  cogl_matrix_init_identity (&child_transform);
  cogl_matrix_scale (&child_transform, geometry_scale, geometry_scale, 1);
  clutter_actor_set_child_transform (CLUTTER_ACTOR (self),
                                     &child_transform);
}

int
meta_feedback_actor_get_geometry_scale (MetaFeedbackActor *self)
{
  MetaFeedbackActorPrivate *priv =
    meta_feedback_actor_get_instance_private (self);

  return priv->geometry_scale;
}
