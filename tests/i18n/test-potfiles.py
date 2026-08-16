#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

"""Check po/POTFILES.in against the sources it claims to cover.

xgettext only looks at the files POTFILES.in lists, and it says nothing about
the ones it does not: a source that grows a _() without being added here has its
strings silently dropped from the catalog, and nobody finds out until a user
reports an untranslated dialog. The other direction is quieter but drifts the
same way -- a file whose strings have all moved elsewhere stays listed forever,
and the list stops describing anything.

So both directions are checked: every source carrying a translatable string is
listed, and everything listed is either a data file (whose strings xgettext
extracts through its own ITS rules) or a source that still carries one.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POTFILES = ROOT / 'po/POTFILES.in'

# The keywords meson's "glib" gettext preset passes to xgettext, plus the plain
# gettext()/ngettext() a JS file might use. A leading identifier character rules
# out the likes of g_dgettext_this_is_not_it and, above all, our own g_paste_*.
# The call has to carry an argument: prose that writes the bare _() down is how
# gpaste-macros.h talks about the macro it defines, and that is not a string.
MARKER = re.compile(
    r'(?:^|[^A-Za-z0-9_])(?:_|N_|C_|NC_|Q_|gettext|ngettext|pgettext'
    r'|g_dgettext|g_dngettext|g_dpgettext2)\s*\(\s*[^)\s]')

SOURCES = ('*.c', '*.h', '*.js')


def carries_strings(path):
    return bool(MARKER.search(path.read_text(encoding='utf-8')))


def main():
    listed = [line.strip() for line in POTFILES.read_text().splitlines()
              if line.strip() and not line.startswith('#')]
    failures = []

    for entry in listed:
        path = ROOT / entry
        if not path.exists():
            failures.append(f'{entry} is listed in POTFILES.in but does not exist')
        elif entry.startswith('src/') and not carries_strings(path):
            failures.append(f'{entry} is listed in POTFILES.in but carries no translatable string')

    listed = set(listed)

    for pattern in SOURCES:
        for path in (ROOT / 'src').rglob(pattern):
            entry = str(path.relative_to(ROOT))
            if entry not in listed and carries_strings(path):
                failures.append(f'{entry} carries translatable strings but is missing from POTFILES.in')

    if failures:
        for failure in sorted(failures):
            print(failure, file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
