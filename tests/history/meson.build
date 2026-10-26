history_test_exe = executable(
  'gpaste-history-test',
  sources: 'test-history.c',
  dependencies: gpaste_daemon_core_dep,
)

test('test-history', history_test_exe,
  depends: gpaste_schemas,
  env: {
    'GSETTINGS_BACKEND': 'memory',
    'GSETTINGS_SCHEMA_DIR': meson.project_build_root() / 'data/gsettings',
  },
)
