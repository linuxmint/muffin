clutter_tests_conform_c_args = [
  '-DG_LOG_DOMAIN="Clutter-Conform"',
  '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
]
clutter_tests_conform_c_args += clutter_debug_c_args

clutter_tests_conform_link_args = [
  '-Wl,--export-dynamic',
]

clutter_conform_tests_actor_tests = [
  'actor-anchors',
  'actor-clone',
  'actor-destroy',
  'actor-graph',
  'actor-invariants',
  'actor-iter',
  'actor-layout',
  'actor-meta',
  'actor-offscreen-redirect',
  'actor-paint-opacity',
  'actor-pick',
  'actor-shader-effect',
  'actor-size',
]

clutter_conform_tests_classes_tests = [
  'text',
]

clutter_conform_tests_general_tests = [
  'binding-pool',
  'color',
  'interval',
  'script-parser',
  'units',
]

clutter_conform_tests_deprecated_tests = [
  'group',
  'rectangle',
]

clutter_conform_tests = []
clutter_conform_tests +=  clutter_conform_tests_actor_tests
clutter_conform_tests += clutter_conform_tests_classes_tests
clutter_conform_tests += clutter_conform_tests_general_tests
clutter_conform_tests += clutter_conform_tests_deprecated_tests

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('G_ENABLE_DIAGNOSTIC', '0')
test_env.set('CLUTTER_ENABLE_DIAGNOSTIC', '0')
test_env.set('CLUTTER_SCALE', '1')

foreach test : clutter_conform_tests
  test_executable = executable('@0@'.format(test),
    sources: [
      '@0@.c'.format(test),
      clutter_test_utils,
    ],
    include_directories: clutter_includes,
    c_args: clutter_tests_conform_c_args,
    link_args: clutter_tests_conform_link_args,
    dependencies: [
      clutter_deps,
      libmutter_clutter_dep,
      libmutter_cogl_path_dep,
      libmutter_dep
    ],
    install: false,
  )

  test(test, test_executable,
    suite: ['clutter', 'clutter/conform'],
    env: test_env
  )
endforeach
