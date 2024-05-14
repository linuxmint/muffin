/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013-2017 Red Hat Inc.
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

#include "backends/x11/meta-crtc-xrandr.h"

#include <X11/Xlib-xcb.h>
#include <X11/extensions/Xrender.h>
#include <stdlib.h>
#include <xcb/randr.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-output.h"
#include "backends/x11/meta-crtc-xrandr.h"
#include "backends/x11/meta-gpu-xrandr.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"

#define ALL_TRANSFORMS ((1 << (META_MONITOR_TRANSFORM_FLIPPED_270 + 1)) - 1)
#define DOUBLE_TO_FIXED(d) ((xcb_render_fixed_t) ((d) * 65536))

typedef struct _MetaCrtcXrandr
{
  MetaRectangle rect;
  MetaMonitorTransform transform;
  MetaCrtcMode *current_mode;
} MetaCrtcXrandr;

gboolean
meta_crtc_xrandr_set_config (MetaCrtc            *crtc,
                             xcb_randr_crtc_t     xrandr_crtc,
                             xcb_timestamp_t      timestamp,
                             int                  x,
                             int                  y,
                             xcb_randr_mode_t     mode,
                             xcb_randr_rotation_t rotation,
                             xcb_randr_output_t  *outputs,
                             int                  n_outputs,
                             xcb_timestamp_t     *out_timestamp)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaGpuXrandr *gpu_xrandr = META_GPU_XRANDR (gpu);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay;
  XRRScreenResources *resources;
  xcb_connection_t *xcb_conn;
  xcb_timestamp_t config_timestamp;
  xcb_randr_set_crtc_config_cookie_t cookie;
  xcb_randr_set_crtc_config_reply_t *reply;
  xcb_generic_error_t *xcb_error = NULL;

  xdisplay = meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  xcb_conn = XGetXCBConnection (xdisplay);
  resources = meta_gpu_xrandr_get_resources (gpu_xrandr);
  config_timestamp = resources->configTimestamp;
  cookie = xcb_randr_set_crtc_config (xcb_conn,
                                      xrandr_crtc,
                                      timestamp,
                                      config_timestamp,
                                      x, y,
                                      mode,
                                      rotation,
                                      n_outputs,
                                      outputs);
  reply = xcb_randr_set_crtc_config_reply (xcb_conn,
                                           cookie,
                                           &xcb_error);
  if (xcb_error || !reply)
    {
      free (xcb_error);
      free (reply);
      return FALSE;
    }

  *out_timestamp = reply->timestamp;
  free (reply);


  return TRUE;
}

gboolean
meta_crtc_xrandr_set_scale (MetaCrtc         *crtc,
                            xcb_randr_crtc_t  xrandr_crtc,
                            float             scale)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay;
  const char *scale_filter;
  xcb_connection_t *xcb_conn;
  xcb_void_cookie_t transform_cookie;
  xcb_generic_error_t *xcb_error = NULL;
  xcb_render_transform_t transformation = {
    DOUBLE_TO_FIXED (1), DOUBLE_TO_FIXED (0), DOUBLE_TO_FIXED (0),
    DOUBLE_TO_FIXED (0), DOUBLE_TO_FIXED (1), DOUBLE_TO_FIXED (0),
    DOUBLE_TO_FIXED (0), DOUBLE_TO_FIXED (0), DOUBLE_TO_FIXED (1)
  };
  float integer_scale;

  if (!(meta_monitor_manager_get_capabilities (monitor_manager) &
        META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING))
    return FALSE;

  xdisplay = meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  xcb_conn = XGetXCBConnection (xdisplay);

  if (fabsf (scale - 1.0f) > 0.001)
    {
      integer_scale = roundf (scale);
      if (fabsf (scale - integer_scale) > 0.001)
        {
          scale_filter = FilterGood;
          transformation.matrix11 = DOUBLE_TO_FIXED (1.0 / scale);
          transformation.matrix22 = DOUBLE_TO_FIXED (1.0 / scale);
        }
      else /* if integer multiple then use nearest neighbor filter */
        {
          scale_filter = "nearest";
          transformation.matrix11 = DOUBLE_TO_FIXED (1.0 / integer_scale);
          transformation.matrix22 = DOUBLE_TO_FIXED (1.0 / integer_scale);
        }
    }
  else
    scale_filter = FilterFast;

  transform_cookie =
    xcb_randr_set_crtc_transform_checked (xcb_conn, xrandr_crtc, transformation,
                                          strlen (scale_filter), scale_filter,
                                          0, NULL);

  xcb_error = xcb_request_check (xcb_conn, transform_cookie);
  if (xcb_error)
    {
      g_warning ("Impossible to set scaling on crtc %u to %f, error id %u",
                 xrandr_crtc, scale, xcb_error->error_code);
      g_clear_pointer (&xcb_error, free);

      return FALSE;
    }

  return TRUE;
}

static MetaMonitorTransform
meta_monitor_transform_from_xrandr (Rotation rotation)
{
  static const MetaMonitorTransform y_reflected_map[4] = {
    META_MONITOR_TRANSFORM_FLIPPED_180,
    META_MONITOR_TRANSFORM_FLIPPED_90,
    META_MONITOR_TRANSFORM_FLIPPED,
    META_MONITOR_TRANSFORM_FLIPPED_270
  };
  MetaMonitorTransform ret;

  switch (rotation & 0x7F)
    {
    default:
    case RR_Rotate_0:
      ret = META_MONITOR_TRANSFORM_NORMAL;
      break;
    case RR_Rotate_90:
      ret = META_MONITOR_TRANSFORM_90;
      break;
    case RR_Rotate_180:
      ret = META_MONITOR_TRANSFORM_180;
      break;
    case RR_Rotate_270:
      ret = META_MONITOR_TRANSFORM_270;
      break;
    }

  if (rotation & RR_Reflect_X)
    return ret + 4;
  else if (rotation & RR_Reflect_Y)
    return y_reflected_map[ret];
  else
    return ret;
}

#define ALL_ROTATIONS (RR_Rotate_0 | RR_Rotate_90 | RR_Rotate_180 | RR_Rotate_270)

static MetaMonitorTransform
meta_monitor_transform_from_xrandr_all (Rotation rotation)
{
  unsigned ret;

  /* Handle the common cases first (none or all) */
  if (rotation == 0 || rotation == RR_Rotate_0)
    return (1 << META_MONITOR_TRANSFORM_NORMAL);

  /* All rotations and one reflection -> all of them by composition */
  if ((rotation & ALL_ROTATIONS) &&
      ((rotation & RR_Reflect_X) || (rotation & RR_Reflect_Y)))
    return ALL_TRANSFORMS;

  ret = 1 << META_MONITOR_TRANSFORM_NORMAL;
  if (rotation & RR_Rotate_90)
    ret |= 1 << META_MONITOR_TRANSFORM_90;
  if (rotation & RR_Rotate_180)
    ret |= 1 << META_MONITOR_TRANSFORM_180;
  if (rotation & RR_Rotate_270)
    ret |= 1 << META_MONITOR_TRANSFORM_270;
  if (rotation & (RR_Rotate_0 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED;
  if (rotation & (RR_Rotate_90 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED_90;
  if (rotation & (RR_Rotate_180 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED_180;
  if (rotation & (RR_Rotate_270 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED_270;

  return ret;
}

gboolean
meta_crtc_xrandr_is_assignment_changed (MetaCrtc     *crtc,
                                        MetaCrtcInfo *crtc_info)
{
  MetaCrtcXrandr *crtc_xrandr = crtc->driver_private;
  unsigned int i;

  if (crtc_xrandr->current_mode != crtc_info->mode)
    return TRUE;

  if (crtc_xrandr->rect.x != (int) roundf (crtc_info->layout.origin.x))
    return TRUE;

  if (crtc_xrandr->rect.y != (int) roundf (crtc_info->layout.origin.y))
    return TRUE;

  if (crtc_xrandr->transform != crtc_info->transform)
    return TRUE;

  for (i = 0; i < crtc_info->outputs->len; i++)
    {
      MetaOutput *output = ((MetaOutput**) crtc_info->outputs->pdata)[i];
      MetaCrtc *assigned_crtc;

      assigned_crtc = meta_output_get_assigned_crtc (output);
      if (assigned_crtc != crtc)
        return TRUE;
    }

  return FALSE;
}

MetaCrtcMode *
meta_crtc_xrandr_get_current_mode (MetaCrtc *crtc)
{
  MetaCrtcXrandr *crtc_xrandr = crtc->driver_private;

  return crtc_xrandr->current_mode;
}

static void
meta_crtc_destroy_notify (MetaCrtc *crtc)
{
  g_free (crtc->driver_private);
}

static float
meta_monitor_scale_from_transformation (XRRCrtcTransformAttributes *transformation)
{
  XTransform *xt;
  float scale;

  if (!transformation)
    return 1.0f;

  xt = &transformation->currentTransform;

  if (xt->matrix[0][0] == xt->matrix[1][1])
    scale = XFixedToDouble (xt->matrix[0][0]);
  else
    scale = XFixedToDouble (xt->matrix[0][0] + xt->matrix[1][1]) / 2.0;

  return 1.0f / scale;
}

MetaCrtc *
meta_create_xrandr_crtc (MetaGpuXrandr              *gpu_xrandr,
                         XRRCrtcInfo                *xrandr_crtc,
                         RRCrtc                      crtc_id,
                         XRRScreenResources         *resources,
                         XRRCrtcTransformAttributes *transform_attributes,
                         float                       scale_multiplier)
{
  MetaGpu *gpu = META_GPU (gpu_xrandr);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay =
    meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  MetaCrtc *crtc;
  MetaCrtcXrandr *crtc_xrandr;
  XRRPanning *panning;
  unsigned int i;
  GList *modes;

  crtc = g_object_new (META_TYPE_CRTC, NULL);

  crtc_xrandr = g_new0 (MetaCrtcXrandr, 1);
  crtc_xrandr->transform =
    meta_monitor_transform_from_xrandr (xrandr_crtc->rotation);

  crtc->driver_private = crtc_xrandr;
  crtc->driver_notify = (GDestroyNotify) meta_crtc_destroy_notify;
  crtc->gpu = META_GPU (gpu_xrandr);
  crtc->crtc_id = crtc_id;

  panning = XRRGetPanning (xdisplay, resources, crtc_id);
  if (panning && panning->width > 0 && panning->height > 0)
    {
      crtc_xrandr->rect = (MetaRectangle) {
        .x = panning->left,
        .y = panning->top,
        .width = panning->width,
        .height = panning->height,
      };
    }
  else
    {
      crtc_xrandr->rect = (MetaRectangle) {
        .x = xrandr_crtc->x,
        .y = xrandr_crtc->y,
        .width = xrandr_crtc->width,
        .height = xrandr_crtc->height,
      };
    }

  crtc->is_dirty = FALSE;
  crtc->all_transforms =
    meta_monitor_transform_from_xrandr_all (xrandr_crtc->rotations);
  crtc->scale = meta_monitor_scale_from_transformation (transform_attributes);

  if (scale_multiplier > 0.0f)
    crtc->scale *= scale_multiplier;

  modes = meta_gpu_get_modes (crtc->gpu);
  for (i = 0; i < (unsigned int) resources->nmode; i++)
    {
      if (resources->modes[i].id == xrandr_crtc->mode)
        {
          crtc_xrandr->current_mode = g_list_nth_data (modes, i);
          break;
        }
    }

  if (crtc_xrandr->current_mode)
    {
      meta_crtc_set_config (crtc,
                            &GRAPHENE_RECT_INIT (crtc_xrandr->rect.x,
                                                 crtc_xrandr->rect.y,
                                                 crtc_xrandr->rect.width,
                                                 crtc_xrandr->rect.height),
                            crtc_xrandr->current_mode,
                            crtc_xrandr->transform);
    }

  return crtc;
}
