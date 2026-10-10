settings_test_exe = executable(
  'gpaste-settings-test',
  sources: 'test-settings.c',
  dependencies: [ gio_dep, gpaste_test_bus_dep, gpaste_test_env_dep, libgpaste_internal_dep ],
)

test('test-settings', settings_test_exe, depends: [gpaste_schemas, test_bus_supervisor])

shell_test_schemas = gnome.compile_schemas(depend_files: files('org.gnome.shell.gschema.xml'))
shell_settings_test_exe = executable(
  'gpaste-shell-settings-test',
  sources: 'test-shell-settings.c',
  c_args: [ '-DG_PASTE_TEST_SHELL_SCHEMA_DIR="@0@"'.format(meson.current_build_dir()) ],
  dependencies: [ libgpaste_lib_deps, gpaste_test_bus_dep, gpaste_test_env_dep, libgpaste_internal_dep ],
)
test('test-shell-settings', shell_settings_test_exe, depends: [gpaste_schemas, shell_test_schemas, test_bus_supervisor])

util_test_exe = executable(
  'gpaste-util-test',
  sources: 'test-util.c',
  dependencies: [ libgpaste_lib_deps, gpaste_test_env_dep, libgpaste_internal_dep ],
)
test('test-util', util_test_exe, depends: [gpaste_schemas, test_bus_supervisor])
