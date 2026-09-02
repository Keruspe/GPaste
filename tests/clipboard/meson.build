test_clipboard = executable(
  'test-clipboard',
  sources: [ 'test-clipboard.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep ],
)

# The password countdowns under test are real GLib timeouts, 1 to 2 s each, and
# they add up to over 20 s: too close to meson's 30 s default on a loaded machine.
test('clipboard', test_clipboard, depends: [gpaste_schemas, test_bus_supervisor], timeout: 120)

clipboard_gdk_test = executable(
  'test-clipboard-gdk',
  sources: [ 'test-clipboard-gdk.c', '../../src/daemon/gpaste-text-content-provider.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, libadwaita_dep, libgpaste_gtk4_internal_dep ],
)

test('clipboard-gdk', clipboard_gdk_test, depends: [gpaste_schemas, test_bus_supervisor])
