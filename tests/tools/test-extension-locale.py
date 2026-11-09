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


def link_target(destdir, link):
    """Where @link, installed under @destdir, leads: an absolute target names a
    path of the installed system, below @destdir here, and a relative one
    leads from the directory @link is in."""
    target = link.readlink()
    if target.is_absolute():
        return Path(destdir + str(target))
    return link.parent / target


def check_link_target():
    """link_target () on both kinds of link, checked before the build's: an
    installation makes one kind only."""
    with tempfile.TemporaryDirectory() as scratch:
        catalogs = Path(scratch) / 'usr/share/locale'
        catalogs.mkdir(parents=True)
        extension = Path(scratch) / 'usr/share/gnome-shell/extensions/x'
        extension.mkdir(parents=True)
        for name, target in (('absolute', '/usr/share/locale'), ('relative', '../../../locale')):
            (extension / name).symlink_to(target)
            check(link_target(scratch, extension / name).resolve() == catalogs.resolve(),
                  f'a {name} link to {target} is not followed to {catalogs}')


def main():
    check_link_target()

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

        catalogs = list(link_target(destdir, locale).glob('*/LC_MESSAGES/GPaste.mo'))
        check(catalogs, f'{locale} leads to {locale.readlink()}, which holds no catalog')

    return 0


if __name__ == '__main__':
    sys.exit(main())
