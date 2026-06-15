// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#define __G_PASTE_DAEMON_H_INSIDE__

/* Clipboard item types */
#include <gpaste-daemon/gpaste-special-atom.h>
#include <gpaste-daemon/gpaste-binary-data.h>
#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

/* D-Bus plumbing */
#include <gpaste-daemon/gpaste-bus.h>
#include <gpaste-daemon/gpaste-bus-object.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-search-provider.h>

/* Clipboard */
#include <gpaste-daemon/gpaste-clipboard-provider.h>
#include <gpaste-daemon/gpaste-clipboard-gdk.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>

/* History and storage */
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-history-saver.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-noop-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

/* Keybindings */
#include <gpaste-daemon/gpaste-keybinder.h>
#include <gpaste-daemon/gpaste-keybinding.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

#undef __G_PASTE_DAEMON_H_INSIDE__
