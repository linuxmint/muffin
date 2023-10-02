msgfmt = find_program('msgfmt')

custom_target('muffin.desktop',
  input: 'muffin.desktop.in',
  output: 'muffin.desktop',
  command: [
    msgfmt,
    '--desktop', '--template',
    '@INPUT@',
    '-d', join_paths(meson.project_source_root(), 'po'),
    '-o', '@OUTPUT@'
  ],
  install: true,
  install_dir: join_paths(datadir, 'applications'),
)

#xwayland_grab_default_access_rules = get_option('xwayland_grab_default_access_rules')
xwayland_grab_default_access_rules = ''

gschema_config = configuration_data()
gschema_config.set('GETTEXT_DOMAIN', meson.project_name())
gschema_config.set('XWAYLAND_GRAB_DEFAULT_ACCESS_RULES',
                   xwayland_grab_default_access_rules)

schemadir = join_paths(datadir, 'glib-2.0', 'schemas')
configure_file(
  input: 'org.cinnamon.muffin.gschema.xml.in',
  output: 'org.cinnamon.muffin.gschema.xml',
  configuration: gschema_config,
  install_dir: schemadir
)

configure_file(
  input: 'org.cinnamon.muffin.wayland.gschema.xml.in',
  output: 'org.cinnamon.muffin.wayland.gschema.xml',
  configuration: gschema_config,
  install_dir: schemadir
)

configure_file(
  input: 'org.cinnamon.muffin.x11.gschema.xml.in',
  output: 'org.cinnamon.muffin.x11.gschema.xml',
  configuration: gschema_config,
  install_dir: schemadir
)

