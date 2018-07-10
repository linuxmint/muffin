if get_option('gtk-doc')

private_headers = [
  'async-getprop.h',
  'atoms.h',
  'core/bell.h',
  'boxes-private.h',
  'clutter-utils.h',
  'cogl-utils.h',
  'compositor-private.h',
  'constraints.h',
  'core.h',
  'display-private.h',
  'draw-workspace.h',
  'edge-resistance.h',
  'eventqueue.h',
  'frame.h',
  'frames.h',
  'group-private.h',
  'group-props.h',
  'iconcache.h',
  'inlinepixbufs.h',
  'keybindings-private.h',
  'meta-background-actor-private.h',
  'meta-background-group-private.h',
  'meta-dbus-login1.h',
  'meta-module.h',
  'meta-plugin-manager.h',
  'meta-shadow-factory-private.h',
  'meta-texture-rectangle.h',
  'compositor/meta-texture-tower.h',
  'meta-window-actor-private.h',
  'meta-window-group.h',
  'meta-window-shape.h',
  'muffin-enum-types.h',
  'muffin-Xatomtype.h',
  'place.h',
  'preview-widget.h',
  'region-utils.h',
  'resizepopup.h',
  'screen-private.h',
  'session.h',
  'stack-tracker.h',
  'src/core/stack.h',
  'stamp-muffin-enum-types.h',
  'tabpopup.h',
  'theme-private.h',
  'theme.h',
  'tile-preview.h',
  'ui.h',
  'window-private.h',
  'window-props.h',
  'workspace-private.h',
  'xprops.h',
]


docsconf = configuration_data()
docsconf.set('VERSION', meson.project_version())

configure_file(
  input : 'muffin-docs.sgml.in',
  output: 'muffin-docs.sgml',
  configuration: docsconf,
)
gtkdoc_mainfile = join_paths(meson.current_build_dir(), 'muffin-docs.sgml')

if meson.version().version_compare('<0.47.0') or true
  new_gtkdoc_mainfile = join_paths(meson.current_source_dir(), 'muffin-docs.sgml')
  run_command([
    'mv', gtkdoc_mainfile, new_gtkdoc_mainfile
  ])
  gtkdoc_mainfile = new_gtkdoc_mainfile
endif


gnome.gtkdoc('muffin',
  main_sgml: gtkdoc_mainfile,
  content_files: [
    'muffin-overview.xml',
    'running-muffin.xml',
  ],
  ignore_headers: private_headers,
  # mkdb_args: ['--xml-mode', '--output-format=xml'],
  # scan_args: [ '--rebuild-types', '--rebuild-sections' ],
  src_dir: [
    join_paths(meson.source_root(), 'src'),
    join_paths(meson.build_root(), 'src')
  ],
  dependencies: [
    gobject_intr,
    declare_dependency(link_with: libmuffin),
  ],
  scan_args: [
    '--ignore-headers=' + ' '.join(private_headers),
    '--rebuild-sections',
    '--rebuild-types'
  ],
)

endif
