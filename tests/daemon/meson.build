test_daemon_teardown = executable(
  'test-daemon-teardown',
  sources: [ 'test-daemon-teardown.c' ] + test_clipboard_provider_sources,
  include_directories: include_directories('../clipboard'),
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, cc.find_library('dl') ],
)

test('daemon-teardown', test_daemon_teardown, depends: [gpaste_schemas, test_bus_supervisor])
