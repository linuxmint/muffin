cogl_test_fixtures_includepath = [include_directories('.')]

cogl_test_fixtures_sources = [
  'test-unit.h',
  'test-utils.h',
  'test-utils.c',
]

test_datadir = join_paths(cogl_srcdir, 'tests', 'data')

libmutter_cogl_test_fixtures = static_library('mutter-cogl-test-fixtures',
  sources: cogl_test_fixtures_sources,
  c_args: [cogl_c_args, '-DTEST_DATADIR=@0@'.format(test_datadir)],
  link_args: ['-Wl,--no-undefined', '-Wl,--unresolved-symbols=ignore-in-object-files'],
  include_directories: cogl_includepath,
  dependencies: [cogl_deps],
  install: false,
)

libmutter_cogl_test_fixtures_dep = declare_dependency(
  link_with: libmutter_cogl_test_fixtures
)
