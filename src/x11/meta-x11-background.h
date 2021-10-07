/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_X11_BACKGROUND_H
#define META_X11_BACKGROUND_H

#include <clutter/clutter.h>

#include <meta/display.h>

#define META_TYPE_X11_BACKGROUND            (meta_x11_background_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaX11Background,
                      meta_x11_background,
                      META, X11_BACKGROUND,
                      ClutterActor)

ClutterActor * meta_x11_background_new (MetaDisplay *display);

void meta_x11_background_set_layer           (MetaX11Background       *self,
                                          CoglTexture          *texture);
void meta_x11_background_set_layer_wrap_mode (MetaX11Background       *self,
                                          CoglPipelineWrapMode  wrap_mode);
void meta_x11_background_set_visible_region  (MetaX11Background       *self,
                                          cairo_region_t       *visible_region);

#endif /* META_X11_BACKGROUND_H */
