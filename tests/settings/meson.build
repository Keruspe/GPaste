settings_test_exe = executable(
  'gpaste-settings-test',
  sources: 'test-settings.c',
  dependencies: [ gio_dep, gpaste_test_env_dep, libgpaste_internal_dep ],
)

test('test-settings', settings_test_exe, depends: [gpaste_schemas, test_bus_supervisor])
