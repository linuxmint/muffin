/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/x11/meta-cursor-renderer-x11.h"

#include <X11/extensions/Xfixes.h>

#include "backends/meta-cursor-sprite-xcursor.h"
#include "backends/meta-stage-private.h"
#include "backends/x11/meta-backend-x11.h"

struct _MetaCursorRendererX11Private
{
  gboolean server_cursor_visible;
};
typedef struct _MetaCursorRendererX11Private MetaCursorRendererX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRendererX11, meta_cursor_renderer_x11, META_TYPE_CURSOR_RENDERER);

static gboolean
meta_cursor_renderer_x11_update_cursor (MetaCursorRenderer *renderer,
                                        MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererX11 *x11 = META_CURSOR_RENDERER_X11 (renderer);
  MetaCursorRendererX11Private *priv = meta_cursor_renderer_x11_get_instance_private (x11);

  MetaBackendX11 *backend = META_BACKEND_X11 (meta_get_backend ());
  Window xwindow = meta_backend_x11_get_xwindow (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend);

  if (xwindow == None)
    {
      if (cursor_sprite)
        meta_cursor_sprite_realize_texture (cursor_sprite);
      return FALSE;
    }

  gboolean has_server_cursor = FALSE;

  if (cursor_sprite && META_IS_CURSOR_SPRITE_XCURSOR (cursor_sprite))
    {
      MetaCursorSpriteXcursor *sprite_xcursor =
        META_CURSOR_SPRITE_XCURSOR (cursor_sprite);
      MetaCursor cursor;

      cursor = meta_cursor_sprite_xcursor_get_cursor (sprite_xcursor);
      if (cursor != META_CURSOR_NONE)
        {
          Cursor xcursor;

          xcursor = meta_create_x_cursor (xdisplay, cursor);
          XDefineCursor (xdisplay, xwindow, xcursor);
          XFlush (xdisplay);
          XFreeCursor (xdisplay, xcursor);

          has_server_cursor = TRUE;
        }
    }

  if (has_server_cursor != priv->server_cursor_visible)
    {
      if (has_server_cursor)
        XFixesShowCursor (xdisplay, xwindow);
      else
        XFixesHideCursor (xdisplay, xwindow);

      priv->server_cursor_visible = has_server_cursor;
    }

  if (!priv->server_cursor_visible && cursor_sprite)
    meta_cursor_sprite_realize_texture (cursor_sprite);

  return priv->server_cursor_visible;
}

static void
meta_cursor_renderer_x11_class_init (MetaCursorRendererX11Class *klass)
{
  MetaCursorRendererClass *renderer_class = META_CURSOR_RENDERER_CLASS (klass);

  renderer_class->update_cursor = meta_cursor_renderer_x11_update_cursor;
}

static void
meta_cursor_renderer_x11_init (MetaCursorRendererX11 *x11)
{
  MetaCursorRendererX11Private *priv = meta_cursor_renderer_x11_get_instance_private (x11);

  /* XFixes has no way to retrieve the current cursor visibility. */
  priv->server_cursor_visible = TRUE;
}
