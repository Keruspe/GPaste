#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# release.sh, run in a scratch repository with meson, ninja, appstreamcli and
# msgattrib stood in for: the version it checks is meson.build's, not what a
# build directory was last configured at; the soversions a release bumps go in
# its commit; a file modified for anything else stops it before it commits, as
# the tarball would; and a catalog msgattrib fails on stops it too, nothing
# stray in po/ committed on the way.

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]

# meson reads the version off meson.build, or off what build/ was configured
# at; ninja's dist refuses a tree with changes, as meson dist does; msgattrib
# fails on a catalog holding BROKEN.
FAKES = {
    'meson': '''#!/bin/sh
case "$3" in
    meson.build) version=$(sed -n "s/^ *version: '\\\\(.*\\\\)',/\\\\1/p" meson.build) ;;
    *) version=$(cat build/configured-version) ;;
esac
printf '{"version": "%s"}\\n' "$version"
''',
    'ninja': '''#!/bin/sh
[ "$3" = dist ] || exit 0
test -z "$(git status --porcelain --untracked-files=no)"
''',
    'appstreamcli': '#!/bin/sh\n',
    'msgattrib': '''#!/bin/sh
shift
[ "$1" = -o ] && { out=$2; shift 2; }
grep -q BROKEN "$1" && exit 1
[ -n "$out" ] && exit 0
cat "$1"
''',
}


# git as a scratch repository needs it: none of what the environment says about
# another repository -- a hook running the suite exports GIT_DIR and
# GIT_INDEX_FILE, which would have it commit and tag there -- and none of the
# developer's configuration, whose signing or hooks would make the commits fail.
GIT_ENV = {name: value for name, value in os.environ.items() if not name.startswith('GIT_')}
GIT_ENV.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1',
               GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t', GIT_COMMITTER_NAME='t', GIT_COMMITTER_EMAIL='t@t')


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], capture_output=True, text=True, check=True,
                          env=GIT_ENV).stdout


def scratch_repo(base):
    repo = base / 'repo'
    (repo / 'po').mkdir(parents=True)
    (repo / 'data/metainfo').mkdir(parents=True)
    (repo / 'src/libgpaste').mkdir(parents=True)
    (repo / 'build').mkdir()
    shutil.copy(ROOT / 'release.sh', repo)
    (repo / 'meson.build').write_text("project('x',\n  version: '9.9',\n)\n")
    (repo / 'build/configured-version').write_text('9.9')
    (repo / 'src/libgpaste/meson.build').write_text("soversion = '1.0.0'\n")
    (repo / 'src/other.c').write_text('int x;\n')
    for name in ('NEWS', 'README.md', 'data/metainfo/x.xml.in', 'po/GPaste.pot', 'po/fr.po', 'po/LINGUAS'):
        (repo / name).write_text(name + '\n')
    (repo / '.gitignore').write_text('build/\n')
    git(repo, 'init', '-q')
    # Forced: the gitignore git reads by default may hold *.pot, which the real
    # tree tracks.
    git(repo, 'add', '-f', '.')
    git(repo, 'commit', '-q', '-m', 'start')
    return repo


def release(base, repo, version):
    env = dict(GIT_ENV, PATH=f'{base / "bin"}{os.pathsep}{os.environ.get("PATH", "")}')
    return subprocess.run(['bash', 'release.sh', version], cwd=repo, env=env, capture_output=True, text=True)


def main():
    # A package build may have no git at all, which says nothing of release.sh.
    if not shutil.which('git'):
        print('git is not installed: skipping', file=sys.stderr)
        return 77

    failures = []

    def case(name, prepare, version, succeeds, check=None):
        with tempfile.TemporaryDirectory() as scratch:
            base = Path(scratch)
            (base / 'bin').mkdir()
            for tool, script in FAKES.items():
                (base / 'bin' / tool).write_text(script)
                (base / 'bin' / tool).chmod(0o755)
            repo = scratch_repo(base)
            prepare(repo)
            result = release(base, repo, version)
            tagged = bool(git(repo, 'tag', '--list', f'v{version}').strip())
            if (result.returncode == 0) != succeeds or tagged != succeeds:
                failures.append(f'{name}: exit {result.returncode}, tagged {tagged}: {result.stderr.strip()}')
            elif not succeeds and git(repo, 'log', '-1', '--format=%s').strip() != 'start':
                failures.append(f'{name}: committed before stopping')
            elif check:
                problem = check(repo)
                if problem:
                    failures.append(f'{name}: {problem}')

    def soversion_bump(repo):
        (repo / 'src/libgpaste/meson.build').write_text("soversion = '2.0.0'\n")
        (repo / 'po/fr.po.orig').write_text('a leftover\n')

    def bumped_and_clean(repo):
        committed = git(repo, 'show', '--name-only', '--format=', 'HEAD').split()
        if 'src/libgpaste/meson.build' not in committed:
            return 'the soversion bump is not in the release commit'
        if any(name.startswith('po/') and not name.endswith(('.po', '.pot', 'LINGUAS')) for name in committed):
            return f'a stray file went in: {committed}'
        return None

    case('soversions', soversion_bump, '9.9', True, bumped_and_clean)
    case('version off meson.build',
         lambda repo: (repo / 'meson.build').write_text("project('x',\n  version: '10.0',\n)\n"), '10.0', True)
    case('another file modified', lambda repo: (repo / 'src/other.c').write_text('int y;\n'), '9.9', False)
    case('msgattrib failing', lambda repo: (repo / 'po/fr.po').write_text('BROKEN\n'), '9.9', False)

    for f in failures:
        print(f'FAIL: {f}', file=sys.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
