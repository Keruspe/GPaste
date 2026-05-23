// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

G_BEGIN_DECLS

/**
 * GPasteKeybindingAccelerator:
 * @id: a unique string identifier for this shortcut (the dconf key)
 * @accelerator: the trigger string (e.g., "<Super>V")
 * @description: a human-readable, translated description shown in the portal UI
 *
 * Represents a global shortcut to be registered with a #GPasteKeybindingProvider.
 * Terminate an array of these with an entry whose @id is %NULL.
 */
typedef struct
{
    const gchar *id;
    const gchar *accelerator;
    const gchar *description;
} GPasteKeybindingAccelerator;

#define G_PASTE_KEYBINDING_ACCELERATOR(id, accelerator, description) \
    ((GPasteKeybindingAccelerator) { (id), (accelerator), (description) })

#define G_PASTE_TYPE_KEYBINDING_PROVIDER (g_paste_keybinding_provider_get_type ())

G_PASTE_VISIBLE G_DECLARE_INTERFACE (GPasteKeybindingProvider, g_paste_keybinding_provider, G_PASTE, KEYBINDING_PROVIDER, GObject)

struct _GPasteKeybindingProviderInterface
{
    GTypeInterface parent_iface;

    /**
     * GPasteKeybindingProviderInterface::grab_all:
     * @self: a #GPasteKeybindingProvider
     * @accels: (array): a %NULL-terminated (by @id) array of #GPasteKeybindingAccelerator
     *
     * Replace all currently registered shortcuts with @accels.
     */
    void (*grab_all)   (GPasteKeybindingProvider          *self,
                        const GPasteKeybindingAccelerator *accels);

    /**
     * GPasteKeybindingProviderInterface::ungrab_all:
     * @self: a #GPasteKeybindingProvider
     *
     * Release all currently registered shortcuts.
     */
    void (*ungrab_all) (GPasteKeybindingProvider *self);
};

void g_paste_keybinding_provider_grab_all                (GPasteKeybindingProvider          *self,
                                                          const GPasteKeybindingAccelerator *accels);
void g_paste_keybinding_provider_ungrab_all              (GPasteKeybindingProvider          *self);
void g_paste_keybinding_provider_emit_keybinding_activated (GPasteKeybindingProvider        *self,
                                                            const gchar                     *id);

G_END_DECLS
