#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

"""Write the control center's keybindings file from GPaste's own list.

The list lives in the G_PASTE_FOR_EACH_KEYBINDING macro in
src/libgpaste/gpaste-3/gpaste-keybindings.h, which is what the shortcuts dialog
and the preferences page are built from too; generating this file from the same
place is what stops the three from drifting apart.

The control center reads the descriptions from its own translation domain, so
they go in untranslated -- exactly as they were when this file was maintained by
hand.
"""

import re
import sys
from xml.sax.saxutils import quoteattr

ENTRY = re.compile(
    r'K \(\s*(?P<key>[A-Z0-9_]+),\s*N_ \("(?P<group>[^"]*)"\),\s*'
    r'N_ \("(?P<description>[^"]*)"\),\s*N_ \("[^"]*"\)\s*\)')
# Every entry, read or not: what ENTRY fails to read -- an escaped quote, `K(`
# with no space -- would otherwise leave the file without that shortcut.
CALL = re.compile(r'\bK\s*\(')
SETTING = re.compile(r'#define\s+(?P<macro>[A-Z0-9_]+_SETTING)\s+"(?P<value>[^"]*)"')


def main(keybindings_h, gsettings_keys_h, output):
    # UTF-8 whatever the build's locale: the descriptions may hold a typographic
    # apostrophe, and the file says it is UTF-8.
    with open(gsettings_keys_h, encoding='utf-8') as f:
        keys = {m['macro']: m['value'] for m in SETTING.finditer(f.read())}

    with open(keybindings_h, encoding='utf-8') as f:
        body = f.read()
    macro = body[body.index('#define G_PASTE_FOR_EACH_KEYBINDING'):]
    macro = macro[:macro.index('\n\n')]

    entries = list(ENTRY.finditer(macro))
    if not entries:
        sys.exit(f'{keybindings_h}: found no keybindings to generate from')

    calls = len(CALL.findall(macro))
    if calls != len(entries):
        sys.exit(f'{keybindings_h}: read {len(entries)} of the {calls} keybindings; '
                 'write each as K (KEY, N_ ("group"), N_ ("description"), N_ ("portal name"))')

    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<!-- Generated from gpaste-keybindings.h by tools/gen-keybindings-xml.py -->',
             '<KeyListEntries schema="org.gnome.GPaste" group="system" name="GPaste" package="GPaste">']
    for e in entries:
        try:
            name = keys[e['key']]
        except KeyError:
            sys.exit(f'{keybindings_h}: {e["key"]} is not a key in {gsettings_keys_h}')
        lines.append(f'\t<KeyListEntry name={quoteattr(name)} '
                     f'description={quoteattr(e["description"])}></KeyListEntry>')
    lines.append('</KeyListEntries>')

    with open(output, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main(*sys.argv[1:])
