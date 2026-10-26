#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# data/completions/gpaste-client, sourced in bash and asked to complete a
# command line, in a scratch directory of its own.

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
COMPLETION = ROOT / 'data' / 'completions' / 'gpaste-client'


def check(condition, message=''):
    """assert, which python -O (PYTHONOPTIMIZE) would strip."""
    if not condition:
        raise SystemExit(f'FAIL: {message}')


def complete_with_options(cwd, *words):
    """What the completion offers for gpaste-client WORDS, the last one being
    completed, one entry a line, and the compopt calls it makes, which only
    work while bash completes and are recorded instead."""
    script = f'''
exec 3>&2
compopt() {{ printf '%s\\n' "$*" >&3; }}
source "$1"
COMP_WORDS=(gpaste-client {' '.join(f"'{w}'" for w in words)})
COMP_CWORD={len(words)}
_gpaste-client
printf '%s\\n' "${{COMPREPLY[@]}}"
'''
    result = subprocess.run(['bash', '-c', script, 'bash', str(COMPLETION)], cwd=cwd,
                            capture_output=True, text=True, check=True)
    return ([line for line in result.stdout.split('\n') if line],
            [line for line in result.stderr.split('\n') if line])


def complete(cwd, *words):
    """What the completion offers for gpaste-client WORDS."""
    return complete_with_options(cwd, *words)[0]


def main():
    if not shutil.which('bash'):
        print('1..0 # SKIP no bash')
        return

    with tempfile.TemporaryDirectory() as scratch:
        (Path(scratch) / 'a file').touch()
        (Path(scratch) / 'other').touch()

        # Only what matches the word typed, each name whole.
        offered = complete(scratch, 'file', 'a')
        check(offered == ['a file'], f'file a: {offered}')

        offered = complete(scratch, 'get', '--r')
        check(offered == ['--raw'], f'get --r: {offered}')

        offered = complete(scratch, 'history', '--o')
        check(offered == ['--oneline'], f'history --o: {offered}')

        # Offered as names, for bash to quote the space and end a directory
        # with a /; options are not.
        _, options = complete_with_options(scratch, 'file', 'a')
        check(options == ['-o filenames'], f'file a: compopt {options}')
        _, options = complete_with_options(scratch, 'get', '--r')
        check(options == [], f'get --r: compopt {options}')

        # What was typed is matched, never expanded against the directory: a
        # bare --* would read as the "--r" there.
        (Path(scratch) / '--r').touch()
        offered = complete(scratch, 'get', '--*')
        check(offered == [], f'get --*: {offered}')

    print('ok')


if __name__ == '__main__':
    sys.exit(main())
