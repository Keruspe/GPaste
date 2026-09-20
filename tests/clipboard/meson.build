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

# The least an update needs of a provider, for the two suites that drive updates
# with no backend under them.
test_clipboard_provider_sources = [ 'gpaste-test-clipboard-provider.c' ]

# The update API the X11 hints workaround reads through; goes with
# src/daemon/gpaste-clipboard-x11-hints.c.
test_clipboard_hints = executable(
  'test-clipboard-hints',
  sources: [ 'test-clipboard-hints.c' ] + test_clipboard_provider_sources,
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep ],
)

test('clipboard-hints', test_clipboard_hints, depends: [gpaste_schemas, test_bus_supervisor])

# The workaround itself, against the private Xvfb and a selection owner of its
# own. It compiles the file in rather than linking it: it belongs to the daemon
# executable, not to a library.
test_clipboard_x11_hints = executable(
  'test-clipboard-x11-hints',
  sources: [ 'test-clipboard-x11-hints.c', '../../src/daemon/gpaste-clipboard-x11-hints.c' ] + test_clipboard_provider_sources,
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gtk4x11_dep, xfixes_dep ],
)

test('clipboard-x11-hints', test_clipboard_x11_hints, depends: [gpaste_schemas, test_bus_supervisor])
