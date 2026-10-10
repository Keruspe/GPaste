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
canonical verb documented in the man page. Bash is also the one completion
offering options verb by verb, so every verb whose arguments name items has to
be offered --use-index there; zsh and fish offer every option anywhere.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# { argc, verb, aliases, extra_args, needs_client, reads_pipe, item_args, ... }
# rows; the verb-less forms have NULL for both names.
COMMAND = re.compile(
    r'^\s*\{\s*\d+,\s*(?:"(?P<verb>[^"]+)"|NULL),\s*(?:"(?P<aliases>[^"]*)"|NULL),'
    r'\s*\w+,\s*\w+,\s*\w+,\s*(?P<item_args>\w+),', re.M)


def verbs():
    body = (ROOT / 'src/client/gpaste-client.c').read_text()
    table = body[body.index('static const Command commands[] = {'):]
    table = table[:table.index('\n};')]

    # Every row has to be read: one in a shape COMMAND does not match would
    # otherwise go unchecked rather than fail.
    rows = sum(1 for line in table.splitlines() if line.lstrip().startswith('{'))
    matches = list(COMMAND.finditer(table))
    if len(matches) != rows:
        sys.exit(f'read {len(matches)} of the {rows} commands[] rows; write each as '
                 '{ argc, "verb" or NULL, "aliases" or NULL, extra_args, needs_client, reads_pipe, item_args, ... } '
                 'with bare words and no comment before item_args')

    canonical, every, items = [], set(), set()
    for m in matches:
        if not m['verb']:
            continue
        names = [m['verb'], *(m['aliases'] or '').split()]
        canonical.append(m['verb'])
        every.update(names)
        if m['item_args'] != '0':
            items.update(names)

    if not canonical:
        sys.exit('found no commands to check against')

    return canonical, every, items


def bash_options(names):
    """The options the bash completion offers after each of @names."""
    script = ('source "$1"; shift; '
              'for verb; do printf \'%s\\t%s\\n\' "$verb" "$(_gpaste_client_options "$verb")"; done')
    out = subprocess.run(['bash', '-c', script, 'bash', str(ROOT / 'data/completions/gpaste-client'), *names],
                         capture_output=True, text=True, check=True).stdout
    return {verb: set(options.split()) for verb, options in (line.split('\t', 1) for line in out.splitlines())}


def words(path):
    """Every bare word in a file, so a verb is found however it is quoted."""
    return set(re.findall(r'[\w-]+', (ROOT / path).read_text()))


def main():
    canonical, every, items = verbs()
    failures = []

    for completion in ('data/completions/gpaste-client',
                       'data/completions/_gpaste-client',
                       'data/completions/gpaste-client.fish'):
        missing = sorted(every - words(completion))
        if missing:
            failures.append(f'{completion} does not offer: {", ".join(missing)}')

    if shutil.which('bash'):
        missing = sorted(verb for verb, options in bash_options(sorted(items)).items()
                         if not {'--use-index', '-i'} <= options)
        if missing:
            failures.append(f'data/completions/gpaste-client does not offer --use-index after: {", ".join(missing)}')

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
