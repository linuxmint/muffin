default_plugin_c_args = [
  '-fPIC',
  '-DG_LOG_DOMAIN="muffin"',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

default_plugin = shared_module('default',
  sources: ['default.c', libmuffin_cogl_path_enum_types_h],
  include_directories: muffin_includes,
  c_args: default_plugin_c_args,
  dependencies: [
    glib_dep,
    gtk3_dep,
    json_glib_dep,
    cinnamon_desktop_dep,
    libmuffin_clutter_dep,
  ],
  install_dir: join_paths(pkglibdir, 'plugins'),
  install: true,
)
