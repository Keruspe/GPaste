#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# GNOME Shell reads the extension's catalogs, and those of the daemon it hosts,
# from the extension's locale directory, falling back to its own: installed,
# that directory leads to where the build put the catalogs, whatever the prefix
# and -Dlocaledir say.
#
# Run as test-extension-locale.py BUILD MESON..., MESON the meson that set BUILD
# up, which alone reads its install data.

import os
from pathlib import Path
import subprocess
import sys
import tempfile


EXTENSION = 'gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org'


def check(condition, message=''):
    """assert, which python -O (PYTHONOPTIMIZE) would strip."""
    if not condition:
        raise SystemExit(f'FAIL: {message}')


def shadow(build, scratch):
    """A directory meson installs @build from, with a log directory of its own:
    `meson install` rewrites the build's install log, which would forget the
    real installation an uninstall reads, or fail where a root install left it
    read-only."""
    root = Path(scratch) / 'build'
    (root / 'meson-logs').mkdir(parents=True)
    (root / 'meson-private').mkdir()
    for entry in build.iterdir():
        if entry.name not in ('meson-logs', 'meson-private'):
            (root / entry.name).symlink_to(entry)
    # meson finds its log as meson-private/../meson-logs, so meson-private
    # cannot be a link into the build.
    for entry in (build / 'meson-private').iterdir():
        (root / 'meson-private' / entry.name).symlink_to(entry)
    return root


def main():
    build = Path(sys.argv[1]).resolve()
    meson = sys.argv[2:]

    with tempfile.TemporaryDirectory() as scratch:
        destdir = os.path.join(scratch, 'dest')
        subprocess.run([*meson, 'install', '-C', str(shadow(build, scratch)), '--destdir', destdir, '--no-rebuild',
                        '--quiet'], check=True, stdout=subprocess.DEVNULL)
        extensions = list(Path(destdir).rglob(EXTENSION))
        check(len(extensions) == 1, f'no single extension installed: {extensions}')

        locale = extensions[0] / 'locale'
        check(locale.is_symlink(), 'the extension has no locale directory')

        target = Path(destdir + str(locale.readlink()))
        catalogs = list(target.glob('*/LC_MESSAGES/GPaste.mo'))
        check(catalogs, f'{locale} leads to {locale.readlink()}, which holds no catalog')

    return 0


if __name__ == '__main__':
    sys.exit(main())
