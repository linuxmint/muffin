mutter_public_headers = [
  'barrier.h',
  'boxes.h',
  'common.h',
  'compositor.h',
  'compositor-mutter.h',
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
  mutter_public_headers += [
    'meta-x11-display.h',
    'meta-x11-errors.h',
  ]
endif

install_headers(mutter_public_headers,
  subdir: mutter_includesubdir
)

mutter_public_header_files = files(mutter_public_headers)

mutter_enum_types = gnome.mkenums('meta-enum-types',
  sources: [mutter_public_headers],
  c_template: 'meta-enum-types.c.in',
  h_template: 'meta-enum-types.h.in',
  install_dir: mutter_includedir,
  install_header: true,
)

mutter_version_array = meson.project_version().split('.')
mutter_version_major = mutter_version_array[0]
mutter_version_minor = mutter_version_array[1]
mutter_version_micro = mutter_version_array[2]

mutter_version_cdata = configuration_data()
mutter_version_cdata.set('MUTTER_MAJOR_VERSION',
                         '@0@'.format(mutter_version_major))
mutter_version_cdata.set('MUTTER_MINOR_VERSION',
                         '@0@'.format(mutter_version_minor))
mutter_version_cdata.set('MUTTER_MICRO_VERSION',
                         '@0@'.format(mutter_version_micro))
mutter_version_cdata.set('MUTTER_PLUGIN_API_VERSION',
                         '@0@'.format(mutter_plugin_api_version))

mutter_version = configure_file(
  input: 'meta-version.h.in',
  output: 'meta-version.h',
  configuration: mutter_version_cdata,
  install_dir: mutter_includedir,
  install: true,
)
