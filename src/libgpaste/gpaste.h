// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#define __G_PASTE_H_INSIDE__

/* Misc. macros */
#include <gpaste-3/gpaste-macros.h>

/* Talking to the daemon: its error domain, where it lives on the bus, and the
 * enums that cross the wire */
#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-item-enums.h>
#include <gpaste-3/gpaste-update-enums.h>

/* GPasteSettings */
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-storage.h>

/* GPasteClient */
#include <gpaste-3/gpaste-client.h>
#include <gpaste-3/gpaste-client-item.h>

/* Utilities */
#include <gpaste-3/gpaste-util.h>

/* GPasteScreensaverClient */

#undef __G_PASTE_H_INSIDE__
