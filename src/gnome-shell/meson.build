shell_extension_files = [
  'actionButton.js',
  'actions.js',
  'daemon.js',
  'deleteButton.js',
  'dependencies.js',
  'dummyHistoryItem.js',
  'extension.js',
  'favouriteButton.js',
  'historySwitcher.js',
  'indicator.js',
  'item.js',
  'itemActions.js',
  'prefs.js',
  'prompt.js',
  'searchItem.js',
  'stateSwitch.js',
]

npm = find_program('npm', required: false)
run_eslint = find_program(
  meson.project_source_root() / 'tools/run-eslint.sh',
  required: false,
)
if npm.found() and run_eslint.found()
  test(
    'eslint',
    run_eslint,
    workdir: meson.project_source_root(),
    env: { 'G_PASTE_LINT_NO_INSTALL': '1' },
  )
endif

configured_shell_ext_metadata = configure_file(
  input: 'metadata.json.in',
  configuration: conf,
  output: 'metadata.json',
)

shell_extension_dir = get_option('datadir') / 'gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org'

install_data(
  shell_extension_files,
  configured_shell_ext_metadata,
  install_dir: shell_extension_dir,
)

# GNOME Shell binds the extension's gettext domain to its locale directory when
# it has one, and to the shell's own otherwise: pointing it at where
# i18n.gettext () installs the catalogs, the extension and the daemon the shell
# hosts read them wherever the prefix and -Dlocaledir put them.
install_symlink(
  'locale',
  install_dir: shell_extension_dir,
  pointing_to: get_option('prefix') / get_option('localedir'),
)
