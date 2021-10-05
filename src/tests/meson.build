clutter_test_utils = files (
  'clutter-test-utils.c',
  'clutter-test-utils.h',
)

if have_clutter_tests
  subdir('clutter')
endif

tests_includepath = mutter_includes
tests_c_args = mutter_c_args

tests_deps = [
  mutter_deps,
  libmutter_dep,
  libmutter_cogl_dep,
  libmutter_clutter_dep,
]

subdir('wayland-test-clients')

if have_installed_tests
  stacking_files_datadir = join_paths(pkgdatadir, 'tests')

  installed_tests_cdata = configuration_data()
  installed_tests_cdata.set('libexecdir', libexecdir)
  installed_tests_cdata.set('apiversion', libmutter_api_version)

  configure_file(
    input: 'mutter-all.test.in',
    output: 'mutter-all.test',
    configuration: installed_tests_cdata,
    install: true,
    install_dir: mutter_installed_tests_datadir,
  )

  install_subdir('stacking', install_dir: stacking_files_datadir)
endif

test_env = environment()
test_env.set('G_TEST_SRCDIR', join_paths(meson.source_root(), 'src'))
test_env.set('G_TEST_BUILDDIR', meson.build_root())
test_env.set('MUTTER_TEST_PLUGIN_PATH', '@0@'.format(default_plugin.full_path()))

test_client = executable('mutter-test-client',
  sources: ['test-client.c'],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [
    gtk3_dep,
    gio_unix_dep,
    x11_dep,
    xext_dep,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

test_runner = executable('mutter-test-runner',
  sources: [
    'test-utils.c',
    'test-utils.h',
    'test-runner.c',
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [tests_deps],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

unit_tests = executable('mutter-test-unit-tests',
  sources: [
    'test-utils.c',
    'test-utils.h',
    'unit-tests.c',
    'boxes-tests.c',
    'boxes-tests.h',
    'meta-backend-test.c',
    'meta-backend-test.h',
    'meta-gpu-test.c',
    'meta-gpu-test.h',
    'meta-monitor-manager-test.c',
    'meta-monitor-manager-test.h',
    'monitor-config-migration-unit-tests.c',
    'monitor-config-migration-unit-tests.h',
    'monitor-store-unit-tests.c',
    'monitor-store-unit-tests.h',
    'monitor-test-utils.c',
    'monitor-test-utils.h',
    'monitor-transform-tests.c',
    'monitor-transform-tests.h',
    'monitor-unit-tests.c',
    'monitor-unit-tests.h',
    'wayland-unit-tests.c',
    'wayland-unit-tests.h',
    test_driver_server_header,
    test_driver_protocol_code,
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [tests_deps],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

headless_start_test = executable('mutter-headless-start-test',
  sources: [
    'headless-start-test.c',
    'meta-backend-test.c',
    'meta-backend-test.h',
    'meta-gpu-test.c',
    'meta-gpu-test.h',
    'meta-monitor-manager-test.c',
    'meta-monitor-manager-test.h',
    'test-utils.c',
    'test-utils.h',
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [tests_deps],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

stacking_tests = [
  'basic-x11',
  'basic-wayland',
  'client-side-decorated',
  'closed-transient',
  'closed-transient-no-input-no-take-focus-parent',
  'closed-transient-no-input-no-take-focus-parents',
  'closed-transient-no-input-parent',
  'closed-transient-no-input-parent-delayed-focus-default-cancelled',
  'closed-transient-no-input-parents',
  'closed-transient-no-input-parents-queued-default-focus-destroyed',
  'closed-transient-only-take-focus-parents',
  'minimized',
  'mixed-windows',
  'set-parent',
  'override-redirect',
  'set-override-redirect-parent',
  'set-parent-exported',
]

foreach stacking_test: stacking_tests
  suites = ['core', 'mutter/stacking']
  if stacking_test.startswith('closed-transient-no-input')
    suites += ['flaky']
  endif
  test(stacking_test, test_runner,
    suite: suites,
    env: test_env,
    args: [
      files(join_paths('stacking', stacking_test + '.metatest')),
    ],
    is_parallel: false,
    timeout: 60,
  )
endforeach

test('normal', unit_tests,
  suite: ['core', 'mutter/unit'],
  env: test_env,
  is_parallel: false,
  timeout: 60,
)

test('headless-start', headless_start_test,
  suite: ['core', 'mutter/unit'],
  env: test_env,
  is_parallel: false,
  timeout: 60,
)
