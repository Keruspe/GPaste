// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-history.h>

G_BEGIN_DECLS

/* Load the history called @name -- the configured one for %NULL -- on the
 * calling thread, for the tests, which want a model in place before they go
 * on, and exported for them alone. It follows none of the guards
 * g_paste_history_load_async () does: the storage lock, the writes still out
 * for @name, a load in flight that it does not give up, whose result then
 * lands over it. Nothing in the daemon calls it, and no binding sees it. */
void g_paste_history_load (GPasteHistory *self,
                           const gchar   *name);

/* Whether deleting or emptying @name reaches every store keeping it, through
 * the backend in use (g_paste_storage_backend_reaches_history ()). */
gboolean g_paste_history_reaches_store (GPasteHistory *self,
                                        const gchar   *name);

/* The items matching @pattern, as g_paste_history_search () finds them, read
 * under the one lock that search takes: a reference on each, in the history's
 * order, so that a caller answering with them asks the history nothing more.
 * A pattern that is not a regular expression fails with
 * %G_PASTE_ERROR_INVALID_ARGUMENT. */
GPtrArray *g_paste_history_search_items (GPasteHistory *self,
                                         const gchar   *pattern,
                                         GError       **error);

G_END_DECLS
