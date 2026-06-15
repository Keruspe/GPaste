// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-bus-object.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SEARCH_PROVIDER (g_paste_search_provider_get_type ())

G_PASTE_FINAL_TYPE (SearchProvider, search_provider, SEARCH_PROVIDER, GPasteBusObject)

GPasteBusObject *g_paste_search_provider_new (void);

G_END_DECLS
