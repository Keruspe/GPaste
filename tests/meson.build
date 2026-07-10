# In-memory GSettings backed by the schema compiled into the build tree.
test_history_env = environment()
test_history_env.set('GSETTINGS_BACKEND', 'memory')
test_history_env.set('GSETTINGS_SCHEMA_DIR', meson.project_build_root() / 'data' / 'gsettings')

subdir('history')
