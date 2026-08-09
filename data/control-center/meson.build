# Derived from the G_PASTE_FOR_EACH_KEYBINDING macro, so this file and the two
# shortcut UIs cannot disagree on which shortcuts exist or what they do.
custom_target(
  '42-gpaste.xml',
  input: [
    '../../src/libgpaste/gpaste-3/gpaste-keybindings.h',
    '../../src/libgpaste/gpaste-3/gpaste-gsettings-keys.h',
  ],
  output: '42-gpaste.xml',
  command: [
    find_program('../../tools/gen-keybindings-xml.py'),
    '@INPUT0@', '@INPUT1@', '@OUTPUT@',
  ],
  install: true,
  install_dir: keybindings_dir,
)
