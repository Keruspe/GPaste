// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>

/**
 * g_paste_error_quark:
 *
 * Get the #GQuark identifying the #GPasteError domain, registering the domain
 * with GDBus on the first call.
 *
 * Registering is what makes the mapping work in both directions: the daemon
 * returning a #GError in this domain puts the matching name on the wire, and a
 * client receiving that name gets the code back rather than having to parse the
 * message. Both sides link this library, so neither can drift from the other.
 *
 * Returns: the #GQuark
 */
G_PASTE_VISIBLE GQuark
g_paste_error_quark (void)
{
    static gsize quark = 0;

    /* The names clients see on the wire. They are API: renaming one breaks
     * whoever matches on it, so they are spelled out rather than derived. */
    static const GDBusErrorEntry entries[] = {
        { G_PASTE_ERROR_FAILED,           G_PASTE_BUS_NAME ".Error.Failed"          },
        { G_PASTE_ERROR_INVALID_ARGUMENT, G_PASTE_BUS_NAME ".Error.InvalidArgument" },
        { G_PASTE_ERROR_NOT_FOUND,        G_PASTE_BUS_NAME ".Error.NotFound"        },
        { G_PASTE_ERROR_INVALID_INDEX,    G_PASTE_BUS_NAME ".Error.InvalidIndex"    },
        { G_PASTE_ERROR_WRONG_ITEM_KIND,  G_PASTE_BUS_NAME ".Error.WrongItemKind"   },
        { G_PASTE_ERROR_ALREADY_EXISTS,   G_PASTE_BUS_NAME ".Error.AlreadyExists"   },
        { G_PASTE_ERROR_NOT_ENCRYPTED,    G_PASTE_BUS_NAME ".Error.NotEncrypted"    },
        { G_PASTE_ERROR_REJECTED,         G_PASTE_BUS_NAME ".Error.Rejected"        }
    };

    G_STATIC_ASSERT (G_N_ELEMENTS (entries) == G_PASTE_ERROR_REJECTED + 1);

    g_dbus_error_register_error_domain ("g-paste-error-quark",
                                        &quark,
                                        entries,
                                        G_N_ELEMENTS (entries));

    return (GQuark) quark;
}
