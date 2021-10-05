cogl_path_includesubdir = join_paths(cogl_includesubdir, 'cogl-path')
cogl_path_includedir = join_paths(cogl_includedir, 'cogl-path')

cogl_path_public_headers = [
  'cogl-path.h',
  'cogl-path-functions.h',
  'cogl-path-types.h',
]

cogl_path_sources = [
  'cogl-path.c',
  'cogl-path-private.h',
  'tesselator/dict-list.h',
  'tesselator/dict.c',
  'tesselator/dict.h',
  'tesselator/geom.c',
  'tesselator/geom.h',
  'tesselator/gluos.h',
  'tesselator/memalloc.h',
  'tesselator/mesh.c',
  'tesselator/mesh.h',
  'tesselator/normal.c',
  'tesselator/normal.h',
  'tesselator/priorityq-heap.h',
  'tesselator/priorityq-sort.h',
  'tesselator/priorityq.c',
  'tesselator/priorityq.h',
  'tesselator/render.c',
  'tesselator/render.h',
  'tesselator/sweep.c',
  'tesselator/sweep.h',
  'tesselator/tess.c',
  'tesselator/tess.h',
  'tesselator/tesselator.h',
  'tesselator/tessmono.c',
  'tesselator/tessmono.h',
]

cogl_path_includepath = include_directories('.')

libmutter_cogl_path_enum_types = gnome.mkenums('cogl-path-enum-types',
  sources: 'cogl-path-types.h',
  c_template: 'cogl-path-enum-types.c.in',
  h_template: 'cogl-path-enum-types.h.in',
  install_dir: cogl_path_includedir,
  install_header: true,
)
libmutter_cogl_path_enum_types_h = libmutter_cogl_path_enum_types[1]

cogl_path_sources += libmutter_cogl_path_enum_types

cogl_path_c_args = [
  cogl_c_args,
]

libmutter_cogl_path = shared_library('muffin-cogl-path-' + libmutter_api_version,
  sources: [cogl_path_sources, cogl_path_public_headers],
  version: '0.0.0',
  soversion: 0,
  c_args: cogl_path_c_args,
  include_directories: [cogl_includepath, cogl_path_includepath],
  gnu_symbol_visibility: 'hidden',
  dependencies: libmutter_cogl_dep,
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)
libmutter_cogl_path_dep = declare_dependency(
  sources: [libmutter_cogl_path_enum_types_h],
  link_with: libmutter_cogl_path
)

install_headers(cogl_path_public_headers,
  subdir: cogl_path_includesubdir)

pkg.generate(libmutter_cogl_path,
  name: 'CoglPath',
  filebase: 'muffin-cogl-path-' + libmutter_api_version,
  description: 'A 2D path drawing library for Cogl in mutter',
  subdirs: join_paths(pkgname, 'cogl'),
  requires: [cogl_pkg_deps, libmutter_cogl_name],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
  ],
  install_dir: pcdir,
)
