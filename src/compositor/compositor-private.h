/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_COMPOSITOR_PRIVATE_H
#define META_COMPOSITOR_PRIVATE_H

#include <X11/extensions/Xfixes.h>

#include "clutter/clutter.h"
#include "compositor/meta-plugin-manager.h"
#include "compositor/meta-window-actor-private.h"
#include "meta/compositor.h"
#include "meta/display.h"

/* Wait 2ms after vblank before starting to draw next frame */
#define META_SYNC_DELAY 2

struct _MetaCompositorClass
{
  GObjectClass parent_class;

  void (* manage) (MetaCompositor *compositor);
  void (* unmanage) (MetaCompositor *compositor);
  void (* pre_paint) (MetaCompositor *compositor);
  void (* post_paint) (MetaCompositor *compositor);
  void (* remove_window) (MetaCompositor *compositor,
                          MetaWindow     *window);
  int64_t (* monotonic_to_high_res_xserver_time) (MetaCompositor *compositor,
                                                  int64_t         time_us);
};

void meta_compositor_remove_window_actor (MetaCompositor  *compositor,
                                          MetaWindowActor *window_actor);

void meta_switch_workspace_completed (MetaCompositor *compositor);

gboolean meta_begin_modal_for_plugin (MetaCompositor   *compositor,
                                      MetaPlugin       *plugin,
                                      MetaModalOptions  options,
                                      guint32           timestamp);
void     meta_end_modal_for_plugin   (MetaCompositor   *compositor,
                                      MetaPlugin       *plugin,
                                      guint32           timestamp);

MetaPluginManager * meta_compositor_get_plugin_manager (MetaCompositor *compositor);

int64_t meta_compositor_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                            int64_t         monotonic_time_us);

void meta_compositor_flash_window (MetaCompositor *compositor,
                                   MetaWindow     *window);

MetaCloseDialog * meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                                       MetaWindow     *window);

MetaInhibitShortcutsDialog * meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                                              MetaWindow     *window);

void meta_compositor_locate_pointer (MetaCompositor *compositor);

void meta_compositor_redirect_x11_windows (MetaCompositor *compositor);

gboolean meta_compositor_is_unredirect_inhibited (MetaCompositor *compositor);

MetaDisplay * meta_compositor_get_display (MetaCompositor *compositor);

MetaWindowActor * meta_compositor_get_top_window_actor (MetaCompositor *compositor);

ClutterStage * meta_compositor_get_stage (MetaCompositor *compositor);

gboolean meta_compositor_is_switching_workspace (MetaCompositor *compositor);

static inline int64_t
us (int64_t us)
{
  return us;
}

static inline int64_t
ms2us (int64_t ms)
{
  return us (ms * 1000);
}

static inline int64_t
s2us (int64_t s)
{
  return ms2us(s * 1000);
}

/*
 * This function takes a 64 bit time stamp from the monotonic clock, and clamps
 * it to the scope of the X server clock, without losing the granularity.
 */
static inline int64_t
meta_translate_to_high_res_xserver_time (int64_t time_us)
{
  int64_t us;
  int64_t ms;

  us = time_us % 1000;
  ms = time_us / 1000;

  return ms2us (ms & 0xffffffff) + us;
}

#endif /* META_COMPOSITOR_PRIVATE_H */
