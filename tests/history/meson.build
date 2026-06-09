test_history = executable(
  'test-history',
  sources: [ 'test-history.c' ],
  dependencies: [ gpaste_daemon_internal_dep ],
)

test('history', test_history, env: test_history_env)
