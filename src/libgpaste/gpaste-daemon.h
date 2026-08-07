// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/* The headers below reach into <gpaste/...>, which refuses to be included
 * except through <gpaste.h>. Pull that in first: by the time they ask for one
 * of those headers it has already been seen, so the guard never fires. Without
 * this, including <gpaste-daemon.h> on its own does not compile at all. */
#include <gpaste.h>

#define __G_PASTE_DAEMON_H_INSIDE__

/* This umbrella covers the installed headers only. The library's internal
 * machinery (the clipboards manager, the history saver, the keybinder, the
 * concrete storage backends and the item subclasses beyond the ones the public
 * headers hand around) is compiled in but neither installed nor introspected,
 * so it must not appear here: an installed umbrella that includes an
 * uninstalled header is a broken install. */

/* Clipboard item types */
#include <gpaste-daemon/gpaste-special-atom.h>
#include <gpaste-daemon/gpaste-binary-data.h>
#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-password-item.h>

/* D-Bus plumbing */
#include <gpaste-daemon/gpaste-bus.h>
#include <gpaste-daemon/gpaste-bus-object.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-search-provider.h>

/* Clipboard */
#include <gpaste-daemon/gpaste-clipboard-provider.h>

/* History and storage */
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-passphrase.h>
#include <gpaste-daemon/gpaste-prompt.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

#undef __G_PASTE_DAEMON_H_INSIDE__
