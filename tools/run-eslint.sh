#!/bin/sh
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>
# SPDX-FileCopyrightText: 2025 Florian Müllner <fmuellner@gnome.org>
# SPDX-License-Identifier: MIT OR LGPL-3.0-or-later

srcdir=$(dirname -- "$0")

cd $srcdir
[ ! -d node_modules ] && npm clean-install
# Link in project root to make imports work properly
[ ! -e ../node_modules ] && ln -s $srcdir/node_modules ../node_modules
npm run lint -- "$@"
