#!/bin/sh
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>
# SPDX-FileCopyrightText: 2025 Florian Müllner <fmuellner@gnome.org>
# SPDX-License-Identifier: MIT OR LGPL-3.0-or-later

srcdir=$(dirname -- "$0")

# Quoted for a checkout path with spaces, and with CDPATH cleared so that a
# relative directory such as `tools` is looked up where this was started, never
# under a CDPATH entry.
CDPATH= cd -- "$srcdir"
# A lock newer than the install made from it -- a dependency bump pulled since
# -- is installed again, as a missing tree is.
if [ ! -d node_modules ] || [ package-lock.json -nt node_modules/.package-lock.json ]; then
    npm clean-install
fi
# Link in project root to make imports work properly. The target is relative
# to the root, so the link survives the checkout moving, and a link that does
# not resolve -- absolute into a checkout since moved, or pointing at itself --
# is replaced.
[ -e ../node_modules ] || ln -sfn tools/node_modules ../node_modules
npm run lint -- "$@"
