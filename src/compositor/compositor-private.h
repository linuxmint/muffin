/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_COMPOSITOR_PRIVATE_H
#define META_COMPOSITOR_PRIVATE_H

#include <X11/extensions/Xfixes.h>

#include <meta/compositor.h>
#include <meta/display.h>
#include "meta-plugin-manager.h"
#include "meta-window-actor-private.h"
#include <clutter/clutter.h>

typedef struct _MetaCompScreen MetaCompScreen;

struct _MetaCompositor
{
  MetaDisplay    *display;

  Atom            atom_x_root_pixmap;
  Atom            atom_x_set_root;
  Atom            atom_net_wm_window_opacity;
  guint           repaint_func_id;

  ClutterActor   *shadow_src;

  MetaPlugin     *modal_plugin;

  gboolean        show_redraw : 1;
  gboolean        debug       : 1;
  gboolean        no_mipmaps  : 1;
};

struct _MetaCompScreen
{
  MetaScreen            *screen;

  ClutterActor          *stage, *bottom_window_group, *window_group, *overlay_group, *top_window_group;
  ClutterActor          *background_actor;
  ClutterActor		*hidden_group;
  GList                 *windows;
  GHashTable            *windows_by_xid;
  Window                 output;

  /* Used for unredirecting fullscreen windows */
  guint                   disable_unredirect_count;
  MetaWindowActor             *unredirected_window;

  /* Before we create the output window */
  XserverRegion     pending_input_region;

  gint                   switch_workspace_in_progress;

  MetaPluginManager *plugin_mgr;
};

void meta_switch_workspace_completed (MetaScreen    *screen);

gboolean meta_begin_modal_for_plugin (MetaScreen       *screen,
                                      MetaPlugin       *plugin,
                                      Window            grab_window,
                                      Cursor            cursor,
                                      MetaModalOptions  options,
                                      guint32           timestamp);
void     meta_end_modal_for_plugin   (MetaScreen       *screen,
                                      MetaPlugin       *plugin,
                                      guint32           timestamp);

void meta_check_end_modal (MetaScreen *screen);

#endif /* META_COMPOSITOR_PRIVATE_H */
