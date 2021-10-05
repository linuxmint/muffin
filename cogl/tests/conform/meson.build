cogl_test_conformance_sources = [
  'test-conform-main.c',
  'test-atlas-migration.c',
  'test-blend-strings.c',
  'test-blend.c',
  'test-depth-test.c',
  'test-color-hsl.c',
  'test-backface-culling.c',
  'test-just-vertex-shader.c',
  'test-pipeline-user-matrix.c',
  'test-pipeline-uniforms.c',
  'test-pixel-buffer.c',
  'test-premult.c',
  'test-snippets.c',
  'test-wrap-modes.c',
  'test-sub-texture.c',
  'test-custom-attributes.c',
  'test-offscreen.c',
  'test-primitive.c',
  'test-sparse-pipeline.c',
  'test-read-texture-formats.c',
  'test-write-texture-formats.c',
  'test-point-size.c',
  'test-point-size-attribute.c',
  'test-point-sprite.c',
  'test-no-gl-header.c',
  'test-version.c',
  'test-euler.c',
  'test-layer-remove.c',
  'test-alpha-test.c',
  'test-map-buffer-range.c',
  'test-npot-texture.c',
  'test-alpha-textures.c',
  'test-texture-get-set-data.c',
  'test-framebuffer-get-bits.c',
  'test-primitive-and-journal.c',
  'test-copy-replace-texture.c',
  'test-pipeline-cache-unrefs-texture.c',
  'test-texture-no-allocate.c',
  'test-pipeline-shader-state.c',
  'test-texture-rg.c',
  'test-fence.c',
  'test-path.c',
  'test-path-clip.c',
]

#unported = [
#  "test-viewport.c",
#  "test-multitexture.c",
#  "test-npot-texture.c",
#  "test-object.c",
#  "test-readpixels.c",
#  "test-texture-mipmaps.c",
#  "test-texture-pixmap-x11.c",",
#]

cogl_test_conformance_includes = [
  cogl_includepath,
  cogl_test_fixtures_includepath,
]

if have_installed_tests
  cogl_installed_tests_cdata = configuration_data()
  cogl_installed_tests_cdata.set('libexecdir', libexecdir)
  cogl_installed_tests_cdata.set('apiversion', libmutter_api_version)

  configure_file(
    input: 'mutter-cogl.test.in',
    output: 'mutter-cogl.test',
    configuration: cogl_installed_tests_cdata,
    install: true,
    install_dir: mutter_installed_tests_datadir,
  )
endif

libmutter_cogl_test_conformance = executable('test-conformance',
  sources: cogl_test_conformance_sources,
  c_args: cogl_debug_c_args + [
    '-DCOGL_ENABLE_EXPERIMENTAL_API',
    '-DCOGL_DISABLE_DEPRECATED',
    '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
    '-DTESTS_DATADIR="@0@/tests/data"'.format(cogl_srcdir),
  ],
  include_directories: cogl_test_conformance_includes,
  dependencies: [
    libmutter_cogl_dep,
    libmutter_cogl_path_dep,
    libmutter_cogl_test_fixtures_dep,
  ],
  install: have_installed_tests,
  install_dir: cogl_installed_tests_libexecdir,
  install_rpath: pkglibdir,
)

find_unit_tests = find_program('meson/find-conform-unit-tests.sh')
test_conform_main = files(join_paths(meson.current_source_dir(), 'test-conform-main.c'))
cogl_conform_unit_tests = custom_target('cogl-tests-conform-unit-tests',
  output: 'unit-tests',
  input: test_conform_main,
  command: [find_unit_tests, '@INPUT@', '@OUTPUT@'],
  install: have_installed_tests,
  install_dir: cogl_installed_tests_libexecdir,
)

cogl_conformance_tests = run_command(
  find_unit_tests, test_conform_main, '/dev/stdout',
  check: true,
).stdout().strip().split('\n')

foreach test_target: cogl_conformance_tests
  name_parts = []
  foreach part: test_target.split('_')
    if part != 'test'
      name_parts += [part]
    endif
  endforeach

  test_name = '-'.join(name_parts)
  test(test_name, cogl_run_tests,
    suite: ['cogl', 'cogl/conform'],
    env: ['RUN_TESTS_QUIET=1'],
    args: [
      cogl_config_env,
      libmutter_cogl_test_conformance,
      test_target
    ],
    is_parallel: false,
  )
endforeach
