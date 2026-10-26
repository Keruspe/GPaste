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

/* g_paste_history_delete (), @deleted, when not %NULL, saying whether the
 * history is gone: set once its store is, the delete failing over images it
 * could not all sweep included, and left FALSE by every refusal before the
 * unlink. */
gboolean g_paste_history_delete_full (GPasteHistory *self,
                                      const gchar   *name,
                                      gboolean      *deleted,
                                      GError       **error);

/* Whether deleting, emptying or backing up @name reaches every store keeping
 * it, through the backend in use (g_paste_storage_backend_reaches_history ()). */
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

/* The same, matched on a worker thread rather than under the history's lock:
 * a pattern can take its regular expression's match limit on every item, tens
 * of milliseconds apiece whatever their length, which on the main loop --
 * gnome-shell's compositor, for the in-shell daemon -- would stall it for as
 * long. The items are taken as the history holds them when it is asked. */
void       g_paste_history_search_items_async  (GPasteHistory      *self,
                                                const gchar        *pattern,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
GPtrArray *g_paste_history_search_items_finish (GAsyncResult       *result,
                                                GError            **error);

/* The passwords the change being announced takes out of the history, which
 * GPasteHistory::password-dropped announces after ::selected: a handler of the
 * one can tell a head elected because a password went from any other. %NULL
 * outside of an announcement. */
const GSList *g_paste_history_get_dropping (GPasteHistory *self);

G_END_DECLS
