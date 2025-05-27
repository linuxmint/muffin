cogl_test_unit_sources = [
  'test-unit-main.c',
]

cogl_test_unit_includes = [
  cogl_includepath,
  cogl_test_fixtures_includepath,
]

libmuffin_cogl_test_unit = executable('test-unit',
  sources: cogl_test_unit_sources,
  c_args: cogl_debug_c_args + [
    '-DCOGL_DISABLE_DEPRECATED',
    '-DCOGL_COMPILATION',
    '-DTESTS_DATADIR="@0@/tests/data"'.format(cogl_srcdir),
  ],
  include_directories: cogl_test_unit_includes,
  dependencies: [
    libmuffin_cogl_dep,
    libmuffin_cogl_path_dep,
    libmuffin_cogl_test_fixtures_dep,
  ],
  install: false,
)

cogl_unit_tests = run_command(
  find_program('meson/find-unit-tests.sh'), cogl_srcdir, '/dev/stdout',
  check: true,
).stdout().strip().split('\n')

foreach test_target: cogl_unit_tests
  test_name = '-'.join(test_target.split('_'))
  test(test_name, cogl_run_tests,
    suite: ['cogl', 'cogl/unit'],
    env: ['RUN_TESTS_QUIET=1'],
    args: [
      cogl_config_env,
      libmuffin_cogl_test_unit,
      'unit_test_' + test_target
    ],
    is_parallel: false,
  )
endforeach
