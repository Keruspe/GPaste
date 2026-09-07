test_clipboard = executable(
  'test-clipboard',
  sources: [ 'test-clipboard.c' ],
  dependencies: [ gpaste_daemon_internal_dep ],
)

# The password countdowns under test are real GLib timeouts, 1 to 2 s each, and
# they add up to over 20 s: too close to meson's 30 s default on a loaded machine.
test('clipboard', test_clipboard, env: test_history_env, timeout: 120)
