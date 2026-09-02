test_clipboard = executable(
  'test-clipboard',
  sources: [ 'test-clipboard.c' ],
  dependencies: [ gpaste_daemon_internal_dep ],
)

test('clipboard', test_clipboard, env: test_history_env)
