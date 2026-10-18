#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# Every Gio, GLib and GObject constant the GNOME Shell extension names exists,
# and every GPaste name it reaches through the build's own typelibs: a misspelt
# one is undefined in GJS, which passes it on as 0 without a word -- a call flag
# dropped, an error never matched, the wrong sentence in a dialog. The suites'
# doubles cannot tell, defining whatever name the code uses; the typelibs can.
#
# Run as test-gi-constants.py [TYPELIB_DIR], TYPELIB_DIR holding the build's
# GPaste typelibs and libraries; without it, only GLib's own are checked.

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
# GLib.SOURCE_REMOVE as well as Gio.DBusCallFlags.NO_AUTO_START.
CONSTANT = re.compile(r'(?<![\w.])(Gio|GLib|GObject)\.(?:([A-Z][A-Za-z0-9]+)\.)?([A-Z][A-Z0-9_]*)\b')
# A class, a function or a constant, and a member of the first: GPaste.Client,
# GPasteDaemon.prompt_text, GPasteDaemon.PromptText.CANCEL -- not the end of a
# dotted name, such as an icon's.
GPASTE = re.compile(r'(?<![\w.])(GPaste|GPasteDaemon|GPasteGtk)\.([A-Za-z_][A-Za-z0-9_]*)(?:\.([A-Z][A-Z0-9_]*)\b)?')
VERSIONS = {'GPaste': '3', 'GPasteDaemon': '1', 'GPasteGtk': '4'}


def main():
    gjs = shutil.which('gjs')
    if not gjs:
        print('gjs not found')
        return 77

    typelibs = sys.argv[1] if len(sys.argv) > 1 else None
    sources = [source.read_text(encoding='utf-8') for source in (ROOT / 'src/gnome-shell').glob('*.js')]
    names = sorted({'.'.join(part for part in m.groups() if part)
                    for pattern in ((CONSTANT, GPASTE) if typelibs else (CONSTANT,))
                    for text in sources
                    for m in pattern.finditer(text)})
    if not any(name.startswith('GLib.') and name.count('.') == 1 for name in names) or \
       not any(name.startswith('Gio.') and name.count('.') == 2 for name in names) or \
       (typelibs and not any(name.startswith('GPasteDaemon.') for name in names)):
        print(f'not every kind of name found: the patterns no longer match the extension: {names}')
        return 1

    namespaces = ['Gio', 'GLib', 'GObject', *(VERSIONS if typelibs else ())]
    script = (''.join(f"imports.gi.versions.{ns} = '{version}';\n" for ns, version in VERSIONS.items() if typelibs) +
              f"const namespaces = {{{', '.join(f'{ns}: imports.gi.{ns}' for ns in namespaces)}}};\n"
              f'const names = {names!r};\n'
              'const missing = names.filter(n => {\n'
              "    const [ns, ...path] = n.split('.');\n"
              '    return path.reduce((object, part) => object?.[part], namespaces[ns]) === undefined;\n'
              '});\n'
              "print(missing.join('\\n'));\n")
    env = dict(os.environ)
    if typelibs:
        for variable in ('GI_TYPELIB_PATH', 'LD_LIBRARY_PATH'):
            env[variable] = os.pathsep.join(filter(None, (typelibs, env.get(variable))))
    result = subprocess.run([gjs, '-c', script], capture_output=True, text=True, env=env)
    if result.returncode:
        print(f'gjs failed: {result.stderr}')
        return 1
    missing = result.stdout.split()
    if missing:
        print('undefined in GJS: ' + ', '.join(missing))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
