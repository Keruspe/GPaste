# The subprocess traps, the waits on the daemon's methods and on a load, and
# the whole-file reads and writes that test-history, test-ordering and
# test-storage-failures share.
test_history_sources = files('gpaste-test-history.c')

# It defines g_paste_settings_reload () beside libgpaste's, which a static
# library's object defines again (library_static).
if not library_static
  test_history = executable(
    'test-history',
    sources: [ 'test-history.c' ] + test_history_sources + gpaste_test_pwhash_sources,
    dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, cc.find_library('dl') ],
  )

  # Its longest waits give a migration a minute
  # (G_PASTE_TEST_STORAGE_WAIT_SECONDS) and a load thirty seconds
  # (pump_until_loaded ()) before failing the case waiting on them: room past
  # those, rather than meson's 30s default killing the binary with no case
  # named.
  test('history', test_history, depends: [gpaste_schemas, test_bus_supervisor], timeout: 180)
endif

test_enums = executable(
  'test-enums',
  sources: [ 'test-enums.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, cc.find_library('dl') ],
)
test('enums', test_enums, depends: [gpaste_schemas, test_bus_supervisor])

if library_interposable
  test_ordering = executable(
    'test-ordering',
    sources: [ 'test-ordering.c' ] + test_history_sources,
    dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, cc.find_library('dl') ],
  )
  test('history-ordering', test_ordering, depends: [gpaste_schemas, test_bus_supervisor])
endif

test_saver = executable(
  'test-saver',
  sources: [ 'test-saver.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_bus_dep, gpaste_test_env_dep ],
)
test('saver', test_saver, depends: [gpaste_schemas, test_bus_supervisor])

# The cases go through the migration's own code, which the binary includes, and
# inject the failures each of them is about. Registered under -Bsymbolic all the
# same: the cases needing the library's own calls interposed skip themselves
# (library_calls_interposed ()). Against a static library it does not link at
# all, defining functions the library's objects define too.
if not library_static
  test_storage_failures = executable(
    'test-storage-failures',
    sources: [ 'test-storage-failures.c' ] + test_history_sources,
    c_args: library_own_calls_args,
    dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, cc.find_library('dl') ],
  )
  # Its pumps and fixed waits add up to several seconds, on top of the minute
  # its storage operations are given (G_PASTE_TEST_STORAGE_WAIT_SECONDS) and
  # the thirty /storage/lock/taken-is-ours gives its child: room past those,
  # rather than meson's 30s default killing the binary with no case named.
  test('storage-failures', test_storage_failures, depends: [gpaste_schemas, test_bus_supervisor], timeout: 120)
endif
