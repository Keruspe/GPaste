#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# Every Gio, GLib, GObject and Pango constant the GNOME Shell extension names
# exists, every Clutter and St one where mutter's and the shell's typelibs are
# found, and every GPaste name it reaches through the build's own typelibs: a
# misspelt one is undefined in GJS, which passes it on as 0 without a word -- a
# call flag dropped, an error never matched, a key never recognised, a modifier
# never held, the wrong sentence in a dialog. The suites' doubles cannot tell,
# defining whatever name the code uses; the typelibs can.
#
# Run as test-gi-constants.py [TYPELIB_DIR [MUTTER_TYPELIB_DIR]], TYPELIB_DIR
# holding the build's GPaste typelibs and libraries, MUTTER_TYPELIB_DIR
# mutter's private one, the shell's St beside it in ../gnome-shell; without
# them, only GLib's own and Pango are checked.

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
# GLib.SOURCE_REMOVE as well as Gio.DBusCallFlags.NO_AUTO_START.
CONSTANT = re.compile(r'(?<![\w.])(Gio|GLib|GObject)\.(?:([A-Z][A-Za-z0-9]+)\.)?([A-Z][A-Z0-9_]*)\b')
# Clutter.KEY_Escape and Clutter.EVENT_STOP as well as St.PolicyType.NEVER, and
# the classes, which exist as much as the rest.
SHELL_CONSTANT = r'(?<![\w.])({})\.(?:([A-Z][A-Za-z0-9]+)\.)?([A-Z][A-Za-z0-9_]*)\b'
# A class, a function or a constant, and a member of the first, a constant or a
# function: GPaste.Client, GPasteDaemon.prompt_text, GPasteDaemon.PromptText.CANCEL,
# GPaste.Client.is_daemon_gone_error -- not the end of a dotted name, such as an
# icon's.
GPASTE = re.compile(r'(?<![\w.])(GPaste|GPasteDaemon|GPasteGtk)\.([A-Za-z_][A-Za-z0-9_]*)'
                    r'(?:\.([A-Z][A-Z0-9_]*\b|[a-z_][a-z0-9_]*\b))?')
VERSIONS = {'GPaste': '3', 'GPasteDaemon': '1', 'GPasteGtk': '4'}
# A name the extension calls as name?.(), its binding missing from a build
# without the feature compiling it in -- GPasteDaemon.StorageBackend.set_passphrase
# without encryption: looked for in the libraries' headers instead, by the C name
# its namespace's prefix gives it, their text declaring it whatever the build
# compiles. A misspelt one is missing from both.
OPTIONAL_CALL = '?.('
SYMBOL_PREFIXES = {'GPaste': 'g_paste', 'GPasteDaemon': 'g_paste', 'GPasteGtk': 'g_paste_gtk'}
# What is read is the code, its comments aside: one naming an instance's method
# through its class, as "GPaste.ClientItem.get_display_line ()" does, is no name
# the code reaches. Strings are kept whole, a "//" in one being no comment.
COMMENT = re.compile(r"""'(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*"|`(?:[^`\\]|\\.)*`|//[^\n]*|/\*.*?\*/""", re.S)


def code(text):
    return COMMENT.sub(lambda m: m[0] if m[0][0] in '\'"`' else ' ', text)


def c_symbol(name):
    namespace, *path = name.split('.')
    snake = (re.sub(r'(?<!^)(?=[A-Z])', '_', part).lower() for part in path)
    return '_'.join((SYMBOL_PREFIXES[namespace], *snake))


def undeclared(names):
    headers = '\n'.join(header.read_text(encoding='utf-8') for header in (ROOT / 'src/libgpaste').rglob('*.h'))
    return [name for name in names if not re.search(rf'\b{c_symbol(name)}\s*\(', headers)]


def main():
    gjs = shutil.which('gjs')
    if not gjs:
        print('gjs not found')
        return 77

    typelibs = sys.argv[1] if len(sys.argv) > 1 else None
    mutter = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    # The versions are the typelibs' own: Clutter-51, St-51.
    shell = {'Pango': '1.0'}
    typelib_dirs = {'Clutter': mutter, 'St': mutter and mutter.parent / 'gnome-shell'}
    for namespace, directory in typelib_dirs.items():
        found = sorted(directory.glob(f'{namespace}-*.typelib')) if directory else []
        if found:
            shell[namespace] = found[-1].stem.split('-', 1)[1]
    shell_dirs = [str(directory) for directory in typelib_dirs.values() if directory]
    patterns = (CONSTANT, re.compile(SHELL_CONSTANT.format('|'.join(shell))), *((GPASTE,) if typelibs else ()))
    sources = [code(source.read_text(encoding='utf-8')) for source in (ROOT / 'src/gnome-shell').glob('*.js')]
    found = {('.'.join(part for part in m.groups() if part), pattern is GPASTE and text.startswith(OPTIONAL_CALL, m.end()))
             for pattern in patterns
             for text in sources
             for m in pattern.finditer(text)}
    optional = sorted({name for name, called_optionally in found if called_optionally} -
                      {name for name, called_optionally in found if not called_optionally})
    names = sorted({name for name, _ in found} - set(optional))
    if not any(name.startswith('GLib.') and name.count('.') == 1 for name in names) or \
       not any(name.startswith('Gio.') and name.count('.') == 2 for name in names) or \
       not any(name.startswith('Pango.') for name in names) or \
       ('Clutter' in shell and not any(name.startswith('Clutter.KEY_') for name in names)) or \
       (typelibs and not any(name.startswith('GPasteDaemon.') for name in names)) or \
       (typelibs and not any(name.count('.') == 2 and name.split('.')[2][0].islower() for name in names)):
        print(f'not every kind of name found: the patterns no longer match the extension: {names}')
        return 1

    versions = {**shell, **(VERSIONS if typelibs else {})}
    namespaces = ['Gio', 'GLib', 'GObject', *versions]
    script = (''.join(f"imports.gi.versions.{ns} = '{version}';\n" for ns, version in versions.items()) +
              f"const namespaces = {{{', '.join(f'{ns}: imports.gi.{ns}' for ns in namespaces)}}};\n"
              f'const names = {names!r};\n'
              'const missing = names.filter(n => {\n'
              "    const [ns, ...path] = n.split('.');\n"
              '    return path.reduce((object, part) => object?.[part], namespaces[ns]) === undefined;\n'
              '});\n'
              "print(missing.join('\\n'));\n")
    env = dict(os.environ)
    for variable in ('GI_TYPELIB_PATH', 'LD_LIBRARY_PATH'):
        env[variable] = os.pathsep.join(filter(None, (typelibs, *shell_dirs, env.get(variable))))
    result = subprocess.run([gjs, '-c', script], capture_output=True, text=True, env=env)
    if result.returncode:
        print(f'gjs failed: {result.stderr}')
        return 1
    missing = result.stdout.split() + undeclared(optional)
    if missing:
        print('undefined in GJS: ' + ', '.join(missing))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
