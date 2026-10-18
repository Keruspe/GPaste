# Derived from the G_PASTE_FOR_EACH_KEYBINDING macro, so this file and the two
# shortcut UIs cannot disagree on which shortcuts exist or what they do: a
# helper built for the machine running the build expands it.
gen_keybindings_xml = executable(
  'gen-keybindings-xml',
  '../../tools/gen-keybindings-xml.c',
  # GLib 2.44's g_autoptr and g_autofree are the newest of what it uses: a
  # cross build's build machine needs no GLib as new as the host's. Built
  # against that version's API, so that anything newer is an error here rather
  # than a configure failure on such a machine.
  c_args: [
    '-DG_PASTE_COMPILATION',
    '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_44',
    '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_44',
    '-Werror=deprecated-declarations',
  ],
  include_directories: include_directories('../../src/libgpaste'),
  dependencies: dependency('glib-2.0', version: '>= 2.44', native: true),
  native: true,
)

keybindings_xml = custom_target(
  '42-gpaste.xml',
  output: '42-gpaste.xml',
  command: [ gen_keybindings_xml, '@OUTPUT@' ],
  install: true,
  install_dir: keybindings_dir,
)
