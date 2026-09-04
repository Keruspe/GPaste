// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-keybinding-provider.h>

enum
{
    KEYBINDING_ACTIVATED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_INTERFACE (GPasteKeybindingProvider, g_paste_keybinding_provider, G_TYPE_OBJECT)

/**
 * g_paste_keybinding_accelerators_length: (skip)
 * @accels: (array): a %NULL-terminated (by id) array of #GPasteKeybindingAccelerator
 *
 * Count the shortcuts in @accels, its terminator aside.
 *
 * Returns: the number of shortcuts
 */
G_PASTE_VISIBLE gsize
g_paste_keybinding_accelerators_length (const GPasteKeybindingAccelerator *accels)
{
    g_return_val_if_fail (accels, 0);

    gsize n = 0;

    while (accels[n].id)
        ++n;

    return n;
}

/**
 * g_paste_keybinding_accelerators_copy: (skip)
 * @accels: (array): a %NULL-terminated (by id) array of #GPasteKeybindingAccelerator
 *
 * Copy @accels, strings and all: a provider is handed the set it is to register
 * for as long as the call lasts, and holds on to it for as long as it holds the
 * grabs -- which is what lets it tell a set that has not changed from a new one.
 *
 * Returns: (transfer full): a newly allocated array, %NULL-terminated by id
 *          free it with g_paste_keybinding_accelerators_free
 */
G_PASTE_VISIBLE GPasteKeybindingAccelerator *
g_paste_keybinding_accelerators_copy (const GPasteKeybindingAccelerator *accels)
{
    g_return_val_if_fail (accels, NULL);

    gsize n = g_paste_keybinding_accelerators_length (accels);
    GPasteKeybindingAccelerator *copy = g_new (GPasteKeybindingAccelerator, n + 1);

    for (gsize i = 0; i < n; ++i)
    {
        copy[i] = G_PASTE_KEYBINDING_ACCELERATOR (g_strdup (accels[i].id),
                                                  g_strdup (accels[i].accelerator),
                                                  g_strdup (accels[i].description));
    }

    copy[n] = G_PASTE_KEYBINDING_ACCELERATOR (NULL, NULL, NULL);

    return copy;
}

/**
 * g_paste_keybinding_accelerators_free: (skip)
 * @accels: (nullable) (transfer full): an array from g_paste_keybinding_accelerators_copy ()
 *
 * Free a copied array of #GPasteKeybindingAccelerator.
 */
G_PASTE_VISIBLE void
g_paste_keybinding_accelerators_free (GPasteKeybindingAccelerator *accels)
{
    if (!accels)
        return;

    for (GPasteKeybindingAccelerator *a = accels; a->id; ++a)
    {
        g_free ((gchar *) a->id);
        g_free ((gchar *) a->accelerator);
        g_free ((gchar *) a->description);
    }

    g_free (accels);
}

/**
 * g_paste_keybinding_accelerators_match: (skip)
 * @accels: (nullable) (array): a %NULL-terminated (by id) array of #GPasteKeybindingAccelerator
 * @others: (nullable) (array): the array to compare it with
 *
 * Whether both arrays name the same shortcuts, in the same order.
 *
 * GPasteSettings emits a rebind for any write to an accelerator key -- a write
 * of the value already stored included -- and every provider is handed the whole
 * set on every one of them. Asking the far side for the set it is already
 * holding costs a release and a grab with no global shortcut at all in between,
 * and, for the portal, the session whose permission the user has already given.
 *
 * Returns: %TRUE if they hold the same shortcuts
 */
G_PASTE_VISIBLE gboolean
g_paste_keybinding_accelerators_match (const GPasteKeybindingAccelerator *accels,
                                       const GPasteKeybindingAccelerator *others)
{
    if (!accels || !others)
        return accels == others;

    gsize i = 0;

    for (; accels[i].id; ++i)
    {
        if (!others[i].id ||
            !g_paste_str_equal (accels[i].id, others[i].id) ||
            !g_paste_str_equal (accels[i].accelerator, others[i].accelerator) ||
            !g_paste_str_equal (accels[i].description, others[i].description))
            return FALSE;
    }

    return !others[i].id;
}

static void
g_paste_keybinding_provider_default_init (GPasteKeybindingProviderInterface *iface G_GNUC_UNUSED)
{
    /**
     * GPasteKeybindingProvider::keybinding-activated:
     * @provider: the object on which the signal was emitted
     * @id: the id of the activated shortcut (its dconf key)
     *
     * The "keybinding-activated" signal is emitted when a registered shortcut
     * is pressed by the user.
     */
    signals[KEYBINDING_ACTIVATED] = g_signal_new ("keybinding-activated",
                                                   G_TYPE_FROM_INTERFACE (iface),
                                                   G_SIGNAL_RUN_LAST,
                                                   0,
                                                   NULL, /* accumulator */
                                                   NULL, /* accumulator data */
                                                   g_cclosure_marshal_VOID__STRING,
                                                   G_TYPE_NONE,
                                                   1,
                                                   G_TYPE_STRING);
}

/**
 * g_paste_keybinding_provider_emit_keybinding_activated:
 * @self: a #GPasteKeybindingProvider
 * @id: the id of the activated shortcut (its dconf key)
 *
 * Emit the "keybinding-activated" signal on @self.
 */
G_PASTE_VISIBLE void
g_paste_keybinding_provider_emit_keybinding_activated (GPasteKeybindingProvider *self,
                                                       const gchar              *id)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING_PROVIDER (self));
    g_return_if_fail (id);

    g_signal_emit (self, signals[KEYBINDING_ACTIVATED], 0, id);
}

/**
 * g_paste_keybinding_provider_grab_all: (skip)
 * @self: a #GPasteKeybindingProvider
 * @accels: (array): a %NULL-terminated (by id) array of #GPasteKeybindingAccelerator
 *
 * Replace all currently registered shortcuts with @accels.
 */
G_PASTE_VISIBLE void
g_paste_keybinding_provider_grab_all (GPasteKeybindingProvider          *self,
                                      const GPasteKeybindingAccelerator *accels)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING_PROVIDER (self));
    g_return_if_fail (accels);

    G_PASTE_KEYBINDING_PROVIDER_GET_IFACE (self)->grab_all (self, accels);
}

/**
 * g_paste_keybinding_provider_ungrab_all:
 * @self: a #GPasteKeybindingProvider
 *
 * Release all currently registered shortcuts.
 */
G_PASTE_VISIBLE void
g_paste_keybinding_provider_ungrab_all (GPasteKeybindingProvider *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING_PROVIDER (self));

    G_PASTE_KEYBINDING_PROVIDER_GET_IFACE (self)->ungrab_all (self);
}
