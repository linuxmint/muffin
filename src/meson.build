muffinInclude = include_directories('.')

subdir('compositor/plugins')
subdir('tools')
subdir('wm-tester')

muffinAllInclude = include_directories(
  '.',
  'compositor',
  'core',
  'meta',
  'ui',
)


libmuffin_headers = [
  'meta/boxes.h',
  'meta/common.h',
  'meta/compositor-muffin.h',
  'meta/compositor.h',
  'meta/display.h',
  'meta/errors.h',
  'meta/gradient.h',
  'meta/group.h',
  'meta/keybindings.h',
  'meta/main.h',
  'meta/meta-background-actor.h',
  'meta/meta-plugin.h',
  'meta/meta-shadow-factory.h',
  'meta/meta-shaped-texture.h',
  'meta/meta-window-actor.h',
  'meta/prefs.h',
  'meta/screen.h',
  'meta/theme.h',
  'meta/types.h',
  'meta/util.h',
  'meta/window.h',
  'meta/workspace.h',
]
libmuffin_headers_nointrospect = [
  'meta/atomnames.h',
  'meta/preview-widget.h',
]

muffin_enum_sources = gnome.mkenums(
  'muffin-enum-types',
  c_template: 'muffin-enum-types.c.in',
  h_template: 'muffin-enum-types.h.in',
  sources: libmuffin_headers,
)

# Gdk pixbuf csource

inlinepixbufs_h = configure_file(
  output: 'inlinepixbufs.h',
  command: [
    find_program('gdk-pixbuf-csource'),
    '--raw', '--build-list',
    'stock_maximize_data',  join_paths(meson.current_source_dir(), 'stock_maximize.png'),
    'stock_minimize_data',  join_paths(meson.current_source_dir(), 'stock_minimize.png'),
    'stock_delete_data',    join_paths(meson.current_source_dir(), 'stock_delete.png'),
  ],
  capture: true,
)


libmuffin_sources = [
  'compositor/cogl-utils.c',
  'compositor/compositor.c',
  'compositor/meta-background-actor.c',
  'compositor/meta-background.c',
  'compositor/meta-module.c',
  'compositor/meta-plugin-manager.c',
  'compositor/meta-plugin.c',
  'compositor/meta-shadow-factory.c',
  'compositor/meta-shaped-texture.c',
  'compositor/meta-sync-ring.c',
  'compositor/meta-texture-rectangle.c',
  'compositor/meta-texture-tower.c',
  'compositor/meta-window-actor.c',
  'compositor/meta-window-group.c',
  'compositor/meta-window-shape.c',
  'compositor/region-utils.c',
  'core/above-tab-keycode.c',
  'core/async-getprop.c',
  'core/bell.c',
  'core/boxes.c',
  'core/constraints.c',
  'core/core.c',
  'core/delete.c',
  'core/display.c',
  'core/edge-resistance.c',
  'core/errors.c',
  'core/eventqueue.c',
  'core/frame.c',
  'core/group-props.c',
  'core/group.c',
  'core/iconcache.c',
  'core/keybindings.c',
  'core/main.c',
  'core/place.c',
  'core/prefs.c',
  'core/screen.c',
  'core/session.c',
  'core/stack-tracker.c',
  'core/stack.c',
  'core/util.c',
  'core/window-props.c',
  'core/window.c',
  'core/workspace.c',
  'core/xprops.c',
  'ui/draw-workspace.c',
  'ui/frames.c',
  'ui/gradient.c',
  'ui/menu.c',
  'ui/metaaccellabel.c',
  'ui/preview-widget.c',
  'ui/resizepopup.c',
  'ui/theme-parser.c',
  'ui/theme.c',
  'ui/ui.c',
  muffin_enum_sources,
  inlinepixbufs_h,
]


muffin_deps = [
  glib,
  cogl,
  clutter,
  cinn_desktop,
  startup_notif,
  gobject_intr,
  math,
  x_libs,
  # xsync,
  sm,
  gl,
  libcanberra,
]

libmuffin = shared_library('muffin',
  libmuffin_sources,
  include_directories: [ rootInclude, muffinAllInclude, ],
  dependencies: muffin_deps,
)

install_headers(libmuffin_headers + libmuffin_headers_nointrospect,
  subdir: 'muffin/meta',
)


muffin = executable('muffin',
  'core/muffin.c',
  include_directories: [ rootInclude, muffinAllInclude, ],
  link_with: libmuffin,
  dependencies: muffin_deps,
)

muffin_theme_viewer = executable('muffin-theme-viewer',
  'ui/theme-viewer.c',
  include_directories: rootInclude,
  link_with: libmuffin,
  dependencies: [
    gtk,
  ],
)

testboxes = executable('testboxes',
  [ 'core/testboxes.c', 'core/boxes.c', 'core/util.c', ],
  include_directories: rootInclude,
  link_with: libmuffin,
  dependencies: [
    gtk,
    clutter,
  ],
)

testgradient = executable('testgradient',
  'ui/testgradient.c',
  include_directories: rootInclude,
  link_with: libmuffin,
  dependencies: [
    gtk,
  ],
)

testasyncgetprop = executable('testasyncgetprop',
  [ 'core/testasyncgetprop.c', 'core/async-getprop.c', ],
  include_directories: rootInclude,
  link_with: libmuffin,
  dependencies: [
    gtk,
    x_libs,
  ],
)




i18n.merge_file(
  input : 'muffin.desktop.in',
  output: 'muffin.desktop',
  type: 'desktop',
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

i18n.merge_file(
  input : 'org.cinnamon.muffin.gschema.xml.in',
  output: 'org.cinnamon.muffin.gschema.xml',
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

pkgconfig.generate(
  name: 'libmuffin',
  description: 'Muffin window manager library',
  version: meson.project_version(),
  libraries: libmuffin,
  subdirs: 'muffin',
  requires: (
    meson.version().version_compare('>=0.46')
    ? [
      cinn_desktop,
      gtk,
      clutter,
      x11lib,
    ]
    : [
      'cinnamon-desktop',
      'gtk+-3.0',
      'clutter-1.0',
      'x11',
    ]
  ),
  variables: [
    'girdir=${libdir}/muffin',
    'typelibdir=${libdir}/muffin',
  ],
)

pkgconfig.generate(
  name: 'muffin-plugins',
  description: 'Dev parameters for muffin plugins',
  version: meson.project_version(),
  libraries: [],
  subdirs: [],
  requires: [

  ],
  variables: [
    'plugindir=#TODO',
    'libgnome_serverdir=#TODO',
  ],
)


gnome.generate_gir(libmuffin,
  namespace: 'Meta',
  nsversion: 'Muffin.0',
  identifier_prefix: 'Meta',
  symbol_prefix: 'meta',

  sources: [
    libmuffin_headers,
    libmuffin_sources,
  ],

  includes: [
    'CDesktopEnums-3.0',
    'Clutter-1.0',
    'Gdk-3.0',
    'GObject-2.0',
    'Gtk-3.0',
    'xfixes-4.0',
    'xlib-2.0',
  ],

  install: true,
)
