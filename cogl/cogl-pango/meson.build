cogl_pango_sources = [
  'cogl-pango-display-list.c',
  'cogl-pango-display-list.h',
  'cogl-pango-fontmap.c',
  'cogl-pango-glyph-cache.c',
  'cogl-pango-glyph-cache.h',
  'cogl-pango-pipeline-cache.c',
  'cogl-pango-pipeline-cache.h',
  'cogl-pango-private.h',
  'cogl-pango-render.c',
]

cogl_pango_public_headers = [
  'cogl-pango.h',
]

cogl_pango_deps = [
  pango_dep,
  pangocairo_dep,
  libmuffin_cogl_dep,
]

libmuffin_cogl_pango = shared_library('muffin-cogl-pango-' + libmuffin_api_version,
  sources: [cogl_pango_sources, cogl_pango_public_headers],
  version: '0.0.0',
  soversion: 0,
  c_args: cogl_c_args,
  include_directories: [cogl_includepath, cogl_path_includepath],
  gnu_symbol_visibility: 'hidden',
  dependencies: [cogl_pango_deps],
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)

libmuffin_cogl_pango_dep = declare_dependency(
  link_with: libmuffin_cogl_pango,
)

if have_introspection
  libmuffin_cogl_pango_gir = gnome.generate_gir(libmuffin_cogl_pango,
    sources: cogl_pango_public_headers,
    nsversion: libmuffin_api_version,
    namespace: 'CoglPango',
    symbol_prefix: 'cogl_pango',
    header: 'cogl-pango.h',
    includes: [
      libmuffin_cogl_gir[0],
      'Pango-1.0',
      'PangoCairo-1.0'
    ],
    dependencies: [
      cogl_deps,
      pango_dep,
      libmuffin_cogl_pango_dep,
    ],
    extra_args: introspection_args + [
      '-UCOGL_COMPILATION',
      '-DG_LOG_DOMAIN="CoglPango"',
    ],
    install_dir_gir: pkglibdir,
    install_dir_typelib: pkglibdir,
    install: true
  )
endif

cogl_pango_includesubdir = join_paths(cogl_includesubdir, 'cogl-pango')
install_headers(cogl_pango_public_headers, subdir: cogl_pango_includesubdir)

pkg.generate(libmuffin_cogl_pango,
  name: 'CoglPango',
  filebase: 'muffin-cogl-pango-' + libmuffin_api_version,
  description: 'A text rendering for Cogl in muffin',
  subdirs: join_paths(pkgname, 'cogl'),
  requires: [cogl_pkg_deps, libmuffin_cogl_name],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmuffin_api_version,
  ],
  install_dir: pcdir,
)
