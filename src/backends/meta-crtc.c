/*
 * Copyright (C) 2017 Red Hat
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

#include "config.h"

#include "backends/meta-crtc.h"

G_DEFINE_TYPE (MetaCrtc, meta_crtc, G_TYPE_OBJECT)

G_DEFINE_TYPE (MetaCrtcMode, meta_crtc_mode, G_TYPE_OBJECT)

MetaGpu *
meta_crtc_get_gpu (MetaCrtc *crtc)
{
  return crtc->gpu;
}

void
meta_crtc_set_config (MetaCrtc             *crtc,
                      graphene_rect_t      *layout,
                      MetaCrtcMode         *mode,
                      MetaMonitorTransform  transform)
{
  MetaCrtcConfig *config;

  meta_crtc_unset_config (crtc);

  config = g_new0 (MetaCrtcConfig, 1);
  config->layout = *layout;
  config->mode = mode;
  config->transform = transform;

  crtc->config = config;
}

void
meta_crtc_unset_config (MetaCrtc *crtc)
{
  g_clear_pointer (&crtc->config, g_free);
}

static void
meta_crtc_finalize (GObject *object)
{
  MetaCrtc *crtc = META_CRTC (object);

  if (crtc->driver_notify)
    crtc->driver_notify (crtc);

  g_clear_pointer (&crtc->config, g_free);

  G_OBJECT_CLASS (meta_crtc_parent_class)->finalize (object);
}

static void
meta_crtc_init (MetaCrtc *crtc)
{
}

static void
meta_crtc_class_init (MetaCrtcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_crtc_finalize;
}

static void
meta_crtc_mode_finalize (GObject *object)
{
  MetaCrtcMode *crtc_mode = META_CRTC_MODE (object);

  if (crtc_mode->driver_notify)
    crtc_mode->driver_notify (crtc_mode);

  g_clear_pointer (&crtc_mode->name, g_free);

  G_OBJECT_CLASS (meta_crtc_mode_parent_class)->finalize (object);
}

static void
meta_crtc_mode_init (MetaCrtcMode *crtc_mode)
{
}

static void
meta_crtc_mode_class_init (MetaCrtcModeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_crtc_mode_finalize;
}
