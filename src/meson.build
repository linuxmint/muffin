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
]

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

libmuffin_sources += gnome.mkenums(
  'muffin-enum-types',
  c_template: 'muffin-enum-types.c.in',
  h_template: 'muffin-enum-types.h.in',
  sources: libmuffin_headers,
)

# Gdk pixbuf csource

libmuffin_sources += configure_file(
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



# if HAVE_INTROSPECTION
# include $(INTROSPECTION_MAKEFILE)
#
# # Since we don't make any guarantees about stability and we don't support
# # parallel install, there's no real reason to change directories, filenames,
# # etc. as we change the Muffin tarball version.
# #
# # Change the api_version to a muffin specific number since setting it to
# # 3.0 causes major dependency issues with mutter on distributions which
# # auto generate typelib dependencies in RPMS
# api_version = Muffin.0
# #api_version = 3.0
#
# # These files are in package-private directories, even though they may be used
# # by plugins.  If you're writing a plugin, use g-ir-compiler --add-include-path
# # and g-ir-compiler --includedir.
# girdir = $(pkglibdir)
# gir_DATA = Meta-$(api_version).gir
#
# typelibdir = $(pkglibdir)
# typelib_DATA = Meta-$(api_version).typelib
#
# INTROSPECTION_GIRS = Meta-$(api_version).gir
#
# Meta-$(api_version).gir: libmuffin.la
# @META_GIR@_INCLUDES = GObject-2.0 CDesktopEnums-3.0 Gdk-3.0 Gtk-3.0 Clutter-1.0 xlib-2.0 xfixes-4.0
# @META_GIR@_PACKAGES = clutter-1.0 gtk+-3.0
# @META_GIR@_CFLAGS = $(AM_CPPFLAGS)
# @META_GIR@_LIBS = libmuffin.la
# @META_GIR@_FILES =				\
# 	muffin-enum-types.h			\
# 	$(libmuffininclude_base_headers)	\
# 	$(filter %.c,$(libmuffin_la_SOURCES))
# @META_GIR@_SCANNERFLAGS = $(WARN_SCANNERFLAGS) --warn-all --warn-error
#
# endif


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
  requires: [
    cinn_desktop,
    gtk,
    clutter,
    x11lib,
  ],
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
