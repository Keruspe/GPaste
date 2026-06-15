// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-storage-backend.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_NOOP_BACKEND (g_paste_noop_backend_get_type ())

G_PASTE_FINAL_TYPE (NoopBackend, noop_backend, NOOP_BACKEND, GPasteStorageBackend)

G_END_DECLS
