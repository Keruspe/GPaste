test('test-client',
  executable(
    'gpaste-client-test',
    sources: 'test-client.c',
    dependencies: [ gio_dep, gpaste_test_bus_dep, libgpaste_internal_dep ],
  ),
  depends: gpaste_client_exe,
  env: { 'G_PASTE_CLIENT': gpaste_client_exe.full_path() },
)
