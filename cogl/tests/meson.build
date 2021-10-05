cogl_run_tests = find_program('run-tests.sh')

cdata = configuration_data()
cdata.set('HAVE_GL', have_gl.to_int())
cdata.set('HAVE_GLES2', have_gles2.to_int())

cogl_installed_tests_libexecdir = join_paths(
  mutter_installed_tests_libexecdir, 'cogl', 'conform')
if have_installed_tests
  install_data('run-tests.sh', install_dir: cogl_installed_tests_libexecdir)
endif

cogl_config_env = configure_file(
  input: 'config.env.in',
  output: 'config.env',
  configuration: cdata,
  install: have_installed_tests,
  install_dir: cogl_installed_tests_libexecdir,
)

subdir('conform')
subdir('unit')
