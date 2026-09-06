portal_provider_test_exe = executable(
  'gpaste-portal-provider-test',
  sources: 'test-portal-provider.c',
  dependencies: [ gio_dep, gpaste_test_bus_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep ],
)

test('test-portal-provider', portal_provider_test_exe)
