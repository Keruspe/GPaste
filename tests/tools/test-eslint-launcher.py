#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# tools/run-eslint.sh, run in scratch checkouts against an npm that records
# where it ran and installs nothing: no dependency, no network.

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def check(condition, message=''):
    """assert, which python -O (PYTHONOPTIMIZE) would strip."""
    if not condition:
        raise SystemExit(f'FAIL: {message}')


FAKE_NPM = '''#!/bin/sh
printf '%s\\n' "$PWD" >> "$G_PASTE_LINT_TEST_LOG"
if [ "$1" = clean-install ]; then mkdir node_modules; fi
'''


def checkout(scratch, name):
    project = scratch / name
    (project / 'tools').mkdir(parents=True)
    shutil.copy2(ROOT / 'tools/run-eslint.sh', project / 'tools/run-eslint.sh')
    return project


def run(scratch, project, command, cwd):
    log = scratch / 'npm.log'
    log.unlink(missing_ok=True)
    environment = os.environ | {
        'PATH': f'{scratch / "bin"}:/usr/bin:/bin',
        'G_PASTE_LINT_TEST_LOG': str(log),
        # A decoy tools/ for a bare `cd tools` to wander into.
        'CDPATH': str(scratch / 'decoy'),
    }
    subprocess.run([command], cwd=cwd, env=environment, check=True)
    return log.read_text().splitlines()


def assert_linked(project):
    link = project / 'node_modules'
    check(os.readlink(link) == 'tools/node_modules', os.readlink(link))
    check(link.resolve(strict=True) == project / 'tools/node_modules')


with tempfile.TemporaryDirectory(prefix='gpaste-lint-paths-') as temporary:
    # Resolved: under a $TMPDIR that goes through a symlink, the shell's $PWD and
    # the link's target are the real path, not the one tempfile spells.
    scratch = Path(temporary).resolve()
    (scratch / 'bin').mkdir()
    (scratch / 'bin/npm').write_text(FAKE_NPM)
    (scratch / 'bin/npm').chmod(0o755)
    (scratch / 'decoy/tools').mkdir(parents=True)

    # By absolute path from the root, as meson's eslint test runs it, in a
    # checkout whose path has spaces: npm installs, then lints, both in tools/.
    project = checkout(scratch, 'project with spaces')
    tools = project / 'tools'
    check(run(scratch, project, str(tools / 'run-eslint.sh'), project) == [str(tools)] * 2)
    assert_linked(project)
    # The link survives the checkout moving.
    moved = project.rename(scratch / 'moved project')
    assert_linked(moved)

    # From inside tools/, and as tools/run-eslint.sh from the root, which a
    # CDPATH would otherwise send to the decoy.
    for name, command, inside_tools in [('from tools', './run-eslint.sh', True),
                                        ('bare relative', 'tools/run-eslint.sh', False)]:
        project = checkout(scratch, name)
        tools = project / 'tools'
        cwd = tools if inside_tools else project
        check(run(scratch, project, command, cwd) == [str(tools)] * 2)
        assert_linked(project)

    # A root link that does not resolve is replaced, whether it pointed into a
    # checkout that has since moved or at itself.
    for name, target in [('stale absolute', '/nonexistent/tools/node_modules'),
                         ('self link', './node_modules')]:
        project = checkout(scratch, name)
        tools = project / 'tools'
        (tools / 'node_modules').mkdir()
        (project / 'node_modules').symlink_to(target)
        check(run(scratch, project, str(tools / 'run-eslint.sh'), project) == [str(tools)])
        assert_linked(project)

    check(not any((scratch / 'decoy/tools').iterdir()))
