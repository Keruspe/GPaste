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

test_search_provider = executable(
  'test-search-provider',
  sources: 'test-search-provider.c',
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep ],
)

test('search-provider', test_search_provider, depends: [gpaste_schemas, test_bus_supervisor])

# The standalone daemon's host, compiled in with what it is built of.
test_daemon_host = executable(
  'test-daemon-host',
  sources: [
    'test-daemon-host.c',
    '../../src/daemon/gpaste-clipboard-gdk.c',
    '../../src/daemon/gpaste-clipboard-x11-hints.c',
    '../../src/daemon/gpaste-prompt-adw.c',
    '../../src/daemon/gpaste-text-content-provider.c',
  ],
  include_directories: include_directories('../../src/daemon'),
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, gtk4x11_dep, libadwaita_dep, libgpaste_gtk4_internal_dep, xfixes_dep ],
)

test('daemon-host', test_daemon_host, depends: [gpaste_schemas, test_bus_supervisor])
