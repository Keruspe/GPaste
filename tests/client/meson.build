test_client_reexec = executable(
  'test-client-reexec',
  sources: 'test-reexec.c',
  dependencies: [ gio_dep, libgpaste_internal_dep, gpaste_test_env_dep ],
)
test('client-reexec', test_client_reexec, depends: [gpaste_schemas, test_bus_supervisor])

test_client_presence = executable(
  'test-client-presence',
  sources: 'test-presence.c',
  dependencies: [ gio_dep, libgpaste_internal_dep, gpaste_test_bus_dep, gpaste_test_env_dep ],
)
test('client-presence', test_client_presence, depends: [gpaste_schemas, test_bus_supervisor])
