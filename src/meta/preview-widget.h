/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Metacity theme preview widget */

/* 
 * Copyright (C) 2002 Havoc Pennington
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>

#include <meta/common.h>
#include <meta/theme.h>
#include <gtk/gtk.h>

#ifndef META_PREVIEW_WIDGET_H
#define META_PREVIEW_WIDGET_H

#define META_TYPE_PREVIEW			 (meta_preview_get_type ())
#define META_PREVIEW(obj)			 (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_PREVIEW, MetaPreview))
#define META_PREVIEW_CLASS(klass)		 (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_PREVIEW, MetaPreviewClass))
#define META_IS_PREVIEW(obj)		 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_PREVIEW))
#define META_IS_PREVIEW_CLASS(klass)	 (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_PREVIEW))
#define META_PREVIEW_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_PREVIEW, MetaPreviewClass))

typedef struct _MetaPreview	MetaPreview;
typedef struct _MetaPreviewClass	MetaPreviewClass;

struct _MetaPreview
{
  GtkBin bin;

  MetaTheme *theme;
  char *title;
  MetaFrameType type;
  MetaFrameFlags flags;  

  PangoLayout *layout;
  int text_height;

  MetaFrameBorders borders;
  guint            borders_cached : 1;

  MetaButtonLayout button_layout;
};

struct _MetaPreviewClass
{
  GtkBinClass parent_class;
};


GType    meta_preview_get_type (void) G_GNUC_CONST;
GtkWidget* meta_preview_new	 (void);

void meta_preview_set_theme         (MetaPreview            *preview,
                                     MetaTheme              *theme);
void meta_preview_set_title         (MetaPreview            *preview,
                                     const char             *title);
void meta_preview_set_frame_type    (MetaPreview            *preview,
                                     MetaFrameType           type);
void meta_preview_set_frame_flags   (MetaPreview            *preview,
                                     MetaFrameFlags          flags);
void meta_preview_set_button_layout (MetaPreview            *preview,
                                     const MetaButtonLayout *button_layout);

cairo_region_t * meta_preview_get_clip_region (MetaPreview *preview,
                                               gint         new_window_width,
                                               gint         new_window_height);

GdkPixbuf* meta_preview_get_icon (void);
GdkPixbuf* meta_preview_get_mini_icon (void);

#endif
