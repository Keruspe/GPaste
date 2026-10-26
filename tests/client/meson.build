test('test-use-index',
  executable(
    'gpaste-client-use-index-test',
    sources: 'test-use-index.c',
    dependencies: [ gio_dep, gpaste_test_bus_dep, libgpaste_internal_dep ],
  ),
  depends: gpaste_client_exe,
  env: { 'G_PASTE_CLIENT': gpaste_client_exe.full_path() },
)
