#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# Every async function's finish function, as the GIR names it, is one its
# class or interface has: a gtk-doc (finish-func) annotation takes the GI name
# of a sibling (passphrase_finish), and a C symbol written there instead
# (g_paste_prompt_passphrase_finish) names nothing, which a typelib compiler may
# then pair with another method altogether.

import sys
import xml.etree.ElementTree as ET

CORE = '{http://www.gtk.org/introspection/core/1.0}'
GLIB = '{http://www.gtk.org/introspection/glib/1.0}'
CALLABLES = ('method', 'virtual-method', 'function', 'constructor')


def main(paths):
    failures = []
    checked = 0
    for path in paths:
        for owner in ET.parse(path).getroot().iter():
            if owner.tag not in (CORE + 'class', CORE + 'interface', CORE + 'record'):
                continue
            names = {c.get('name') for kind in CALLABLES for c in owner.findall(CORE + kind)}
            for kind in CALLABLES:
                for callable_ in owner.findall(CORE + kind):
                    finish = callable_.get(GLIB + 'finish-func')
                    if finish is None:
                        continue
                    checked += 1
                    if finish not in names:
                        failures.append(f'{path}: {owner.get("name")}.{callable_.get("name")} finishes with {finish}, '
                                        f'which {owner.get("name")} has no callable of')
    if not checked:
        sys.exit('no finish-func found: the GIR no longer carries them, or the test reads the wrong files')
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
