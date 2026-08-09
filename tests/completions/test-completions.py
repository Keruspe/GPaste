#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

"""Check the shell completions and the man page against gpaste-client's verbs.

The commands[] table in src/client/gpaste-client.c is the list of what the
command line answers to, and --help is printed straight from it. The three
completion scripts and the man page cannot be generated from it -- each verb
completes its arguments differently, and the man page says more about them than
a table could carry -- so this checks instead that none of them has fallen
behind: every verb and alias has to be offered by all three shells, and every
canonical verb documented in the man page.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# { verb, aliases, ... } rows; the verb-less forms have NULL for both.
COMMAND = re.compile(
    r'^\s*\{\s*\d+,\s*(?:"(?P<verb>[^"]+)"|NULL),\s*(?:"(?P<aliases>[^"]*)"|NULL),', re.M)


def verbs():
    body = (ROOT / 'src/client/gpaste-client.c').read_text()
    table = body[body.index('static const Command commands[] = {'):]
    table = table[:table.index('\n};')]

    canonical, every = [], set()
    for m in COMMAND.finditer(table):
        if not m['verb']:
            continue
        canonical.append(m['verb'])
        every.add(m['verb'])
        every.update((m['aliases'] or '').split())

    if not canonical:
        sys.exit('found no commands to check against')

    return canonical, every


def words(path):
    """Every bare word in a file, so a verb is found however it is quoted."""
    return set(re.findall(r'[\w-]+', (ROOT / path).read_text()))


def main():
    canonical, every = verbs()
    failures = []

    for completion in ('data/completions/gpaste-client',
                       'data/completions/_gpaste-client',
                       'data/completions/gpaste-client.fish'):
        missing = sorted(every - words(completion))
        if missing:
            failures.append(f'{completion} does not offer: {", ".join(missing)}')

    man = words('man/1/gpaste-client.1')
    missing = sorted(v for v in canonical if v not in man)
    if missing:
        failures.append(f'man/1/gpaste-client.1 does not document: {", ".join(missing)}')

    for f in failures:
        print(f'FAIL: {f}', file=sys.stderr)

    if failures:
        print('\nAdd them there, or drop them from commands[] in '
              'src/client/gpaste-client.c.', file=sys.stderr)
        return 1

    print(f'{len(canonical)} verbs ({len(every)} with aliases) '
          'are offered by all three completions and documented in the man page')
    return 0


if __name__ == '__main__':
    sys.exit(main())
