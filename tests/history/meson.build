test_history = executable(
  'test-history',
  sources: [ 'test-history.c' ],
  dependencies: [ gpaste_daemon_internal_dep ],
)

# The encrypted cases derive an Argon2id key (at libsodium's MODERATE limits)
# several times each, which alone accounts for most of the runtime and brushes
# meson's 30s default on a slow or loaded machine. Give it real room rather than
# letting the suite fail intermittently for a reason that is not a bug.
test('history', test_history, env: test_history_env, timeout: 180)
