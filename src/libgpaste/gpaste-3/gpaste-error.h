// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/**
 * GPasteError:
 * @G_PASTE_ERROR_FAILED: something went wrong and nothing more specific fits
 * @G_PASTE_ERROR_INVALID_ARGUMENT: a required argument was empty or missing
 * @G_PASTE_ERROR_NOT_FOUND: no item, password or history goes by that name/uuid
 * @G_PASTE_ERROR_INVALID_INDEX: the index is past the end of the history
 * @G_PASTE_ERROR_WRONG_ITEM_KIND: the item exists but is not of a kind this
 *                                 operation can act on
 * @G_PASTE_ERROR_ALREADY_EXISTS: something already goes by that name
 * @G_PASTE_ERROR_NOT_ENCRYPTED: the operation only means something on an
 *                               encrypted history, and this one is not
 * @G_PASTE_ERROR_REJECTED: the daemon's own policy refused the item; the
 *                          request was well formed and nothing went wrong
 *
 * The errors the daemon reports over D-Bus.
 *
 * This domain is registered with g_dbus_error_register_error_domain(), so a
 * remote error raised by the daemon comes back out of a #GPasteClient call as a
 * #GError in this domain rather than as an opaque
 * %G_DBUS_ERROR_REMOTE_EXCEPTION string: callers can switch on the code instead
 * of matching on the message.
 */
typedef enum {
    G_PASTE_ERROR_FAILED,
    G_PASTE_ERROR_INVALID_ARGUMENT,
    G_PASTE_ERROR_NOT_FOUND,
    G_PASTE_ERROR_INVALID_INDEX,
    G_PASTE_ERROR_WRONG_ITEM_KIND,
    G_PASTE_ERROR_ALREADY_EXISTS,
    G_PASTE_ERROR_NOT_ENCRYPTED,
    G_PASTE_ERROR_REJECTED
} GPasteError;

/**
 * G_PASTE_ERROR:
 *
 * The #GQuark identifying the #GPasteError domain.
 */
#define G_PASTE_ERROR (g_paste_error_quark ())

GQuark g_paste_error_quark (void);

G_END_DECLS
