test_history = executable(
  'test-history',
  sources: [ 'test-history.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, cc.find_library('dl') ],
)

# The encrypted cases derive an Argon2id key (at libsodium's MODERATE limits)
# several times each, which alone accounts for most of the runtime and brushes
# meson's 30s default on a slow or loaded machine. Give it real room rather than
# letting the suite fail intermittently for a reason that is not a bug.
test('history', test_history, depends: [gpaste_schemas, test_bus_supervisor], timeout: 180)

test_enums = executable(
  'test-enums',
  sources: [ 'test-enums.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, cc.find_library('dl') ],
)
test('enums', test_enums, depends: [gpaste_schemas, test_bus_supervisor])

test_ordering = executable(
  'test-ordering',
  sources: [ 'test-ordering.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep, gpaste_test_bus_dep, cc.find_library('dl') ],
)
test('history-ordering', test_ordering, depends: [gpaste_schemas, test_bus_supervisor])

test_saver = executable(
  'test-saver',
  sources: [ 'test-saver.c' ],
  dependencies: [ gpaste_daemon_internal_dep, gpaste_test_env_dep ],
)
test('saver', test_saver, depends: [gpaste_schemas, test_bus_supervisor])
