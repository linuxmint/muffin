/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background.h: CoglTexture for paintnig the system background
 *
 * Copyright 2013 Red Hat, Inc.
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

#ifndef META_BACKGROUND_H
#define META_BACKGROUND_H

#include <clutter/clutter.h>

#include <libcinnamon-desktop/cdesktop-enums.h>
#include <meta/screen.h>

/**
 * MetaBackground:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #MetaWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

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
  GObjectClass parent_class;
};

struct _MetaBackground
{
  GObject parent;

  MetaBackgroundPrivate *priv;
};

void meta_background_refresh_all (void);

GType meta_background_get_type (void);

MetaBackground *meta_background_new  (MetaScreen *screen);

void meta_background_set_color    (MetaBackground            *self,
                                   ClutterColor              *color);
void meta_background_set_gradient (MetaBackground            *self,
                                   CDesktopBackgroundShading  shading_direction,
                                   ClutterColor              *color,
                                   ClutterColor              *second_color);
void meta_background_set_filename (MetaBackground            *self,
                                   const char                *filename,
                                   CDesktopBackgroundStyle    style);
void meta_background_set_blend    (MetaBackground            *self,
                                   const char                *filename1,
                                   const char                *filename2,
                                   double                     blend_factor,
                                   CDesktopBackgroundStyle    style);

#endif /* META_BACKGROUND_H */
