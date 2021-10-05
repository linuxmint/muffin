clutter_tests_performance_c_args = [
  '-DTESTS_DATA_DIR="@0@"'.format(clutter_tests_interactive_srcdir),
  '-DG_DISABLE_SINGLE_INCLUDES',
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
  '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
  '-DCLUTTER_DISABLE_DEPRECATION_WARNINGS',
]

clutter_tests_performance_c_args += clutter_debug_c_args

clutter_tests_performance_tests = [
  'test-picking',
  'test-text-perf',
  'test-state',
  'test-state-interactive',
  'test-state-hidden',
  'test-state-mini',
  'test-state-pick',
]

foreach test : clutter_tests_performance_tests
  executable(test,
    sources: [
      '@0@.c'.format(test),
      'test-common.h',
    ],
    include_directories: [
      clutter_includes,
      clutter_tests_includepath,
    ],
    c_args: clutter_tests_performance_c_args,
    dependencies: [
      clutter_deps,
      libmutter_clutter_dep,
    ],
    install: false,
  )
endforeach
