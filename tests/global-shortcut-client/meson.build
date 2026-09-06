# Exercise the same implementation with short deadlines. Production keeps the
# ten-minute retired-request grace period and one-second retry interval.
portal_provider_test_exe = executable(
  'gpaste-portal-provider-test',
  sources: [
    'test-portal-provider.c',
    '../../src/libgpaste/gpaste-gtk4/gpaste-gtk-global-shortcut-client.c',
  ],
  c_args: [
    # Route logs through g_test_expect_message in this test build.
    '-UG_LOG_USE_STRUCTURED',
    '-DG_PASTE_GTK_PORTAL_RETIRED_REQUEST_TIMEOUT=1000',
    '-DG_PASTE_GTK_PORTAL_BIND_RETRY_DELAY=50',
  ],
  dependencies: [ gio_dep, gtk4_dep, gpaste_test_bus_dep, libgpaste_internal_dep ],
)

configure_file(
  input: 'org.freedesktop.portal.Desktop.service.in',
  output: 'org.freedesktop.portal.Desktop.service',
  configuration: { 'PORTAL_TEST': portal_provider_test_exe.full_path() },
)

test('test-portal-provider', portal_provider_test_exe,
  timeout: 120,
  env: { 'G_TEST_BUILDDIR': meson.current_build_dir() },
)
