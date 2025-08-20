muffin_public_headers = [
  'barrier.h',
  'boxes.h',
  'common.h',
  'compositor.h',
  'compositor-muffin.h',
  'display.h',
  'group.h',
  'keybindings.h',
  'main.h',
  'meta-backend.h',
  'meta-background.h',
  'meta-background-actor.h',
  'meta-background-group.h',
  'meta-background-image.h',
  'meta-close-dialog.h',
  'meta-cursor-tracker.h',
  'meta-dnd.h',
  'meta-idle-monitor.h',
  'meta-inhibit-shortcuts-dialog.h',
  'meta-launch-context.h',
  'meta-later.h',
  'meta-monitor-manager.h',
  'meta-plugin.h',
  'meta-remote-access-controller.h',
  'meta-selection.h',
  'meta-selection-source.h',
  'meta-selection-source-memory.h',
  'meta-settings.h',
  'meta-shadow-factory.h',
  'meta-shaped-texture.h',
  'meta-sound-player.h',
  'meta-stage.h',
  'meta-startup-notification.h',
  'meta-window-actor.h',
  'meta-window-group.h',
  'meta-window-shape.h',
  'meta-workspace-manager.h',
  'prefs.h',
  'theme.h',
  'types.h',
  'util.h',
  'window.h',
  'workspace.h',
]

if have_x11
  muffin_public_headers += [
    'meta-x11-background-actor.h',
    'meta-x11-display.h',
    'meta-x11-errors.h'
  ]
endif

install_headers(muffin_public_headers,
  subdir: muffin_includesubdir
)

muffin_public_header_files = files(muffin_public_headers)

muffin_enum_types = gnome.mkenums('meta-enum-types',
  sources: [muffin_public_headers],
  c_template: 'meta-enum-types.c.in',
  h_template: 'meta-enum-types.h.in',
  install_dir: muffin_includedir,
  install_header: true,
)

muffin_version_array = meson.project_version().split('.')
muffin_version_major = muffin_version_array[0]
muffin_version_minor = muffin_version_array[1]
muffin_version_micro = muffin_version_array[2]

muffin_version_cdata = configuration_data()
muffin_version_cdata.set('MUFFIN_MAJOR_VERSION',
                         '@0@'.format(muffin_version_major))
muffin_version_cdata.set('MUFFIN_MINOR_VERSION',
                         '@0@'.format(muffin_version_minor))
muffin_version_cdata.set('MUFFIN_MICRO_VERSION',
                         '@0@'.format(muffin_version_micro))
muffin_version_cdata.set('MUFFIN_PLUGIN_API_VERSION',
                         '@0@'.format(muffin_plugin_api_version))

muffin_version = configure_file(
  input: 'meta-version.h.in',
  output: 'meta-version.h',
  configuration: muffin_version_cdata,
  install_dir: muffin_includedir,
  install: true,
)
