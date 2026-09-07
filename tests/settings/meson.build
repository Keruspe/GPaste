settings_test_exe = executable(
  'gpaste-settings-test',
  sources: 'test-settings.c',
  dependencies: [ gio_dep, libgpaste_internal_dep ],
)

test('test-settings', settings_test_exe,
  depends: gpaste_schemas,
  env: {
    'GSETTINGS_BACKEND': 'memory',
    'GSETTINGS_SCHEMA_DIR': meson.project_build_root() / 'data/gsettings',
  },
)
