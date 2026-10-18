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
# An activation case alone, run as from a launcher that left its token behind,
# which the test environment clears before the case reads what it is handed: in
# a full run, /util/activation-token clears it first.
test('test-util-launched', util_test_exe, args: ['-p', '/util/activate-ui/owned/accepted'],
     depends: [gpaste_schemas, test_bus_supervisor], env: {'XDG_ACTIVATION_TOKEN': 'stale'})

# The library's own calls to a GPaste app, on the test bus.
app_call_test_exe = executable(
  'gpaste-app-call-test',
  sources: 'test-app-call.c',
  dependencies: [ gio_dep, gpaste_test_bus_dep, gpaste_test_env_dep, libgpaste_internal_dep ],
)
test('test-app-call', app_call_test_exe, depends: [gpaste_schemas, test_bus_supervisor])
