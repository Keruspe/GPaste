/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-keybinding-provider.h>

enum
{
    KEYBINDING_ACTIVATED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_INTERFACE (GPasteKeybindingProvider, g_paste_keybinding_provider, G_TYPE_OBJECT)

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
 * g_paste_keybinding_provider_grab_all:
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
