// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_PASSPHRASE (g_paste_passphrase_get_type ())

/**
 * GPastePassphrase:
 *
 * A passphrase held in gcr secure memory: non-pageable, and wiped when freed.
 *
 * The point of a type rather than a `gchar *` is that the memory it lives in is
 * not ordinary heap, so it cannot be released with g_free(). A boxed type
 * carries its own free function, which means the compiler enforces that through
 * g_autoptr() and language bindings get it right without a (skip) annotation —
 * where a bare `gchar *` in gcr memory could only be handed out by lying about
 * how to free it, or by not handing it out at all.
 *
 * It is deliberately opaque and has no setter: a passphrase is built from
 * cleartext once, read back to be used, and destroyed. Everything else GPaste
 * does with one — deriving a key, verifying it decrypts, remembering it in the
 * keyring — takes the string.
 */
typedef struct _GPastePassphrase GPastePassphrase;

G_PASTE_VISIBLE
GType g_paste_passphrase_get_type (void) G_GNUC_CONST;

/* Copies @cleartext into secure memory. %NULL, and the empty string — which is
 * no passphrase, not a passphrase that happens to be short — both give %NULL. */
GPastePassphrase *g_paste_passphrase_new  (const gchar            *cleartext);
GPastePassphrase *g_paste_passphrase_copy (const GPastePassphrase *self);
void              g_paste_passphrase_free (GPastePassphrase       *self);

/* The cleartext, owned by @self and valid until it is freed. Never %NULL for a
 * non-%NULL @self: an empty passphrase cannot be constructed. */
const gchar *g_paste_passphrase_peek (const GPastePassphrase *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPastePassphrase, g_paste_passphrase_free)

G_END_DECLS
