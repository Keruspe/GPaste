#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# tools/gen-keybindings-xml.py, run on headers of its own: an entry it cannot
# read fails the build rather than leave the shortcut out of the file, and a
# description holding a typographic apostrophe comes out as the UTF-8 the file
# says it is, whatever the locale.

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/gen-keybindings-xml.py'


def check(condition, message=''):
    """assert, which python -O (PYTHONOPTIMIZE) would strip."""
    if not condition:
        raise SystemExit(f'FAIL: {message}')


KEYS = '''#define G_PASTE_POP_SETTING "pop"
#define G_PASTE_UPLOAD_SETTING "upload"
'''


def generate(scratch, entries):
    keybindings = scratch / 'keybindings.h'
    keys = scratch / 'keys.h'
    output = scratch / 'out.xml'
    keybindings.write_text('#define G_PASTE_FOR_EACH_KEYBINDING(K) \\\n' + ' \\\n'.join(entries) + '\n\n',
                           encoding='utf-8')
    keys.write_text(KEYS, encoding='utf-8')
    # A locale with no UTF-8 in it, as a build may run under.
    env = dict(os.environ, LC_ALL='C', LANG='C', PYTHONUTF8='0', PYTHONCOERCECLOCALE='0')
    result = subprocess.run(['python3', str(SCRIPT), str(keybindings), str(keys), str(output)],
                            env=env, capture_output=True, text=True)
    return result, output


def main():
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)

        result, output = generate(scratch, [
            '    K (G_PASTE_POP_SETTING, N_ ("Items"), N_ ("Delete the item’s copy"), N_ ("Pop"))',
            '    K (G_PASTE_UPLOAD_SETTING, N_ ("Items"), N_ ("Upload"), N_ ("Upload"))',
        ])
        check(result.returncode == 0, result.stderr)
        text = output.read_text(encoding='utf-8')
        check('Delete the item’s copy' in text, text)
        check('name="upload"' in text, text)

        for unread in ['    K(G_PASTE_UPLOAD_SETTING, N_ ("Items"), N_ ("Upload"), N_ ("Upload"))',
                       '    K (G_PASTE_UPLOAD_SETTING, N_ ("Items"), N_ ("The \\"item\\""), N_ ("Upload"))']:
            result, _ = generate(scratch, [
                '    K (G_PASTE_POP_SETTING, N_ ("Items"), N_ ("Pop"), N_ ("Pop"))',
                unread,
            ])
            check(result.returncode != 0, unread)
            check('read 1 of the 2 keybindings' in result.stderr, result.stderr)


if __name__ == '__main__':
    main()
