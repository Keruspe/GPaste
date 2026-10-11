test_daemon_teardown = executable(
  'test-daemon-teardown',
  sources: [ 'test-daemon-teardown.c' ] + test_clipboard_provider_sources,
  include_directories: include_directories('../clipboard'),
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, cc.find_library('dl') ],
)


# The prompt is the daemon executable's own, so the test compiles it in.
test_prompt_adw = executable(
  'test-prompt-adw',
  sources: 'test-prompt-adw.c',
  include_directories: include_directories('../../src/daemon'),
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, libadwaita_dep, libgpaste_gtk4_internal_dep ],
)

test('prompt-adw', test_prompt_adw, depends: [gpaste_schemas, test_bus_supervisor])
test('daemon-teardown', test_daemon_teardown, depends: [gpaste_schemas, test_bus_supervisor])
