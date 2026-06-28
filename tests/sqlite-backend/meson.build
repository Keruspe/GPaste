test_sqlite_backend_c1 = executable(
  'test-sqlite-backend-c1',
  sources: [ 'test-c1.c' ],
  dependencies: [ gpaste_daemon_internal_dep, sqlite3_dep ],
)

test('sqlite-backend-c1', test_sqlite_backend_c1, env: test_history_env)

test_sqlite_backend_c2 = executable(
  'test-sqlite-backend-c2',
  sources: [ 'test-c2.c' ],
  dependencies: [ gpaste_daemon_internal_dep, sqlite3_dep ],
)

test('sqlite-backend-c2', test_sqlite_backend_c2, env: test_history_env)
