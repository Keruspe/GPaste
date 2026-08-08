// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>
#include <gpaste-3/gpaste-storage.h>

#include <gpaste-daemon/gpaste-passphrase.h>

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * GPasteStorageRemember:
 * @G_PASTE_STORAGE_REMEMBER_UNCHANGED: the switch was left as it was found
 * @G_PASTE_STORAGE_REMEMBER_YES: remember the passphrase in the keyring
 * @G_PASTE_STORAGE_REMEMBER_NO: forget whatever the keyring holds
 *
 * What the prompt's "Remember this passphrase" switch says to do with the
 * keyring. %UNCHANGED is not "no": it is the switch having been left as it was
 * found, which must not delete an entry — the switch starts off both when there
 * is nothing remembered and when the keyring could not be reached to ask.
 */
typedef enum {
    G_PASTE_STORAGE_REMEMBER_UNCHANGED,
    G_PASTE_STORAGE_REMEMBER_YES,
    G_PASTE_STORAGE_REMEMBER_NO
} GPasteStorageRemember;

#define G_PASTE_TYPE_PROMPT_REQUEST (g_paste_prompt_request_get_type ())

G_PASTE_FINAL_TYPE (PromptRequest, prompt_request, PROMPT_REQUEST, GObject)

#define G_PASTE_TYPE_PROMPT (g_paste_prompt_get_type ())

G_PASTE_VISIBLE
G_DECLARE_INTERFACE (GPastePrompt, g_paste_prompt, G_PASTE, PROMPT, GObject)

G_PASTE_CONST_FUNCS (Prompt, PROMPT)

/**
 * GPastePromptInterface:
 * @parent_iface: the parent interface
 *
 * The prompts the storage layer needs from whoever is driving it, so that the
 * migration/unlock/re-key logic can stay in one place while its UI does not.
 *
 * None of the methods expose toolkit types: a backend may build libadwaita
 * dialogs (the standalone daemon) or St ones (the daemon hosted inside
 * gnome-shell, which can run neither gtk_init nor adw_init), but
 * gpaste-storage-migration.c talks to both through this single contract.
 *
 * Both concerns are asynchronous — a prompt is answered by a human — but the
 * vfuncs take a #GPastePromptRequest rather than a #GAsyncReadyCallback: GJS
 * refuses to implement a vfunc that takes a callback ("VFunc … accepts another
 * callback as a parameter. This is not supported"), and being implementable
 * from GJS is the whole point of the interface. A backend shows its UI, keeps
 * the request, and answers it whenever the user gets round to it. Callers still
 * get the usual _async/_finish pair below; the request is what bridges the two.
 *
 * The request follows the ordinary convention: it is passed transfer-none, so a
 * backend that keeps it past the vfunc — which is every backend that shows a
 * dialog — references it like anything else. Dropping it without answering is
 * not a hang: an unanswered request dismisses itself when it goes.
 *
 * The default implementations dismiss the request, so a backend that only
 * implements part of the contract degrades into "the user said no" rather than
 * crashing — which is the safe answer everywhere: nothing is migrated, and no
 * passphrase is set, so an encrypted history stays locked rather than being
 * loaded empty and overwritten.
 */
struct _GPastePromptInterface
{
    GTypeInterface parent_iface;

    void (*passphrase) (GPastePrompt        *self,
                        GPastePromptRequest *request);
    void (*migration)  (GPastePrompt        *self,
                        GPastePromptRequest *request);
};

/* What the prompt was asked. Only the getters matching the vfunc that received
 * the request are meaningful. */
gboolean              g_paste_prompt_request_get_confirm       (const GPastePromptRequest *self);
const gchar          *g_paste_prompt_request_get_error_message (const GPastePromptRequest *self);
GPasteStorageRemember g_paste_prompt_request_get_remember      (const GPastePromptRequest *self);
const GPasteStorage  *g_paste_prompt_request_get_offered       (const GPastePromptRequest *self,
                                                                guint                     *n_offered);
GPasteStorage         g_paste_prompt_request_get_current       (const GPastePromptRequest *self);

/* Answer it. Exactly one of these ends the request; later calls are ignored, so
 * a dialog that both replies and then reports its destruction is harmless. */
void g_paste_prompt_request_reply_passphrase (GPastePromptRequest  *self,
                                              const gchar          *passphrase,
                                              GPasteStorageRemember remember);
void g_paste_prompt_request_reply_migration  (GPastePromptRequest *self,
                                              GPasteStorage        chosen,
                                              gboolean             import,
                                              gboolean             cleanup);
void g_paste_prompt_request_dismiss          (GPastePromptRequest *self);

/* The answer comes back as a #GPastePassphrase — secure memory with its own
 * free function — so both of these are ordinary introspectable API.
 *
 * No #GCancellable on either of these, deliberately. A prompt ends when the
 * user answers it or when the backend takes it away (which answers it as a
 * dismissal); there is no third party to cancel it from. Accepting one and
 * doing nothing with it would be worse than not offering it: GTask checks the
 * cancellable in _finish(), so a cancelled-then-answered prompt would report a
 * perfectly good passphrase as %NULL, which every caller here reads as "the
 * user said no" — and an encrypted history would silently load empty. */
void   g_paste_prompt_passphrase_async  (GPastePrompt         *self,
                                         gboolean              confirm,
                                         const gchar          *error_message,
                                         GPasteStorageRemember remember,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data);
GPastePassphrase *g_paste_prompt_passphrase_finish (GPastePrompt          *self,
                                                    GAsyncResult          *result,
                                                    GPasteStorageRemember *remember,
                                                    GError               **error);


void     g_paste_prompt_migration_async  (GPastePrompt        *self,
                                          const GPasteStorage *offered,
                                          guint                n_offered,
                                          GPasteStorage        current,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data);
gboolean g_paste_prompt_migration_finish (GPastePrompt   *self,
                                          GAsyncResult   *result,
                                          GPasteStorage  *chosen,
                                          gboolean       *import,
                                          gboolean       *cleanup,
                                          GError        **error);

/* The backends the migration prompt should offer, in display order: whichever
 * flavors this build can actually construct. Shared so the feature gating and
 * the labels live here rather than in each prompt backend. */
const GPasteStorage *g_paste_prompt_list_storage_backends (guint *n_offered);
const gchar         *g_paste_prompt_storage_label         (GPasteStorage storage_kind);

/* The migration prompt's live rules, shared for the same reason: "import" only
 * makes sense when moving from a backend that stored something into a different
 * one that also stores something, and there is only old data to delete once we
 * actually leave a backend that stored something. */
gboolean g_paste_prompt_can_import      (GPasteStorage current,
                                         GPasteStorage chosen);
gboolean g_paste_prompt_backend_changes (GPasteStorage current,
                                         GPasteStorage chosen);

/* Whether this build can remember a passphrase at all (libsecret), and whether
 * one is currently remembered. A prompt asks the first to decide whether to
 * offer the switch, and the second for where it starts. Both live here because
 * the keyring is a feature-gated, non-introspected part of the daemon library,
 * and a prompt backend written in JS has no other way to ask. */
gboolean g_paste_prompt_keyring_available      (void);
gboolean g_paste_prompt_keyring_has_passphrase (void);

/* Where the "Remember this passphrase" switch should start, and whether leaving
 * it off means anything. Both answers come from one keyring lookup, and both
 * backends ask here rather than working it out again: this is the subtlest rule
 * in the prompt, and a copy of it per language is a copy that drifts. */
void g_paste_prompt_remember_state (GPasteStorageRemember requested,
                                    gboolean             *starts_on,
                                    gboolean             *can_forget);

/* The scale g_paste_prompt_passphrase_strength() rates on, so a meter is built
 * against the contract rather than against a number copied out of it. */
#define G_PASTE_PROMPT_STRENGTH_MAX 4

/* Whether the prompt would accept what has been typed: a passphrase, and — when
 * setting a new one — a confirmation that matches. Shared so the two backends
 * cannot come to disagree about it, and so a stricter rule later (a minimum
 * length, a "must differ from the current one" check) lands in one place rather
 * than letting one prompt enable OK for something the other would refuse. */
gboolean g_paste_prompt_passphrase_is_complete (const gchar *passphrase,
                                                const gchar *confirmation);

/* Whether this build can rate a passphrase at all (libpwquality). A prompt that
 * cannot must say so rather than show a meter pinned at zero: someone choosing a
 * passphrase should know it is not being judged, instead of reading a silent
 * zero as a verdict. */
gboolean g_paste_prompt_pwquality_available (void);

/* Rate @passphrase on a 0-4 scale, GNOME-style (libpwquality, as
 * gnome-control-center does), returning in @hint the rating word or
 * libpwquality's own advice. Built without libpwquality there is no rating to
 * give: the level is 0 and @hint is %NULL. */
guint g_paste_prompt_passphrase_strength (const gchar  *passphrase,
                                          gchar       **hint);

G_END_DECLS
