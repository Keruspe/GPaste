// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-keybinding.h>

struct _GPasteKeybinding
{
    GObject parent_instance;

    GPasteKeybindingGetter getter;
    gchar                 *dconf_key;
    gchar                 *description;
    GPasteKeybindingFunc   callback;
    gpointer               user_data;
    gboolean               active;
};

G_PASTE_DEFINE_TYPE (Keybinding, keybinding, G_TYPE_OBJECT)

/**
 * g_paste_keybinding_get_dconf_key:
 * @self: a #GPasteKeybinding instance
 *
 * Get the dconf key for this keybinding
 *
 * Returns: the dconf key
 */
G_PASTE_VISIBLE const gchar *
g_paste_keybinding_get_dconf_key (const GPasteKeybinding *self)
{
    g_return_val_if_fail (_G_PASTE_IS_KEYBINDING ((gpointer) self), NULL);

    return self->dconf_key;
}

/**
 * g_paste_keybinding_get_description:
 * @self: a #GPasteKeybinding instance
 *
 * Get the human-readable description for this keybinding
 *
 * Returns: the description
 */
G_PASTE_VISIBLE const gchar *
g_paste_keybinding_get_description (const GPasteKeybinding *self)
{
    g_return_val_if_fail (_G_PASTE_IS_KEYBINDING ((gpointer) self), NULL);

    return self->description;
}

/**
 * g_paste_keybinding_get_accelerator:
 * @self: a #GPasteKeybinding instance
 *
 * Get the accelerator for this keybinding
 *
 * Returns: the accelerator
 */
G_PASTE_VISIBLE const gchar *
g_paste_keybinding_get_accelerator (const GPasteKeybinding *self,
                                    const GPasteSettings   *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_KEYBINDING ((gpointer) self), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS ((gpointer) settings), NULL);

    return self->getter (settings);
}

/**
 * g_paste_keybinding_activate:
 * @self: a #GPasteKeybinding instance
 * @settings: a #GPasteSettings instance
 *
 * Activate the keybinding
 */
G_PASTE_VISIBLE void
g_paste_keybinding_activate (GPasteKeybinding *self,
                             GPasteSettings   *settings)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDING (self));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));

    g_return_if_fail (!self->active);

    const gchar *binding = self->getter (settings);

    if (binding)
    {
        // Parse the accelerator without resolving hardware keycodes: the global
        // shortcuts are driven through the XDG portal from the accelerator
        // string, so we never need a GdkDisplay (which would not exist when the
        // daemon runs in-process inside gnome-shell, where gtk_init is not
        // called).
        guint keyval = 0;
        GdkModifierType modifiers;

        self->active = gtk_accelerator_parse (binding, &keyval, &modifiers) && keyval != 0;
    }
}

/**
 * g_paste_keybinding_deactivate:
 * @self: a #GPasteKeybinding instance
 *
 * Deactivate the keybinding
 */
G_PASTE_VISIBLE void
g_paste_keybinding_deactivate (GPasteKeybinding *self)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDING (self));

    g_return_if_fail (self->active);

    self->active = FALSE;
}

/**
 * g_paste_keybinding_is_active:
 * @self: a #GPasteKeybinding instance
 *
 * Check whether the keybinding is active or not
 *
 * Returns: true if the keybinding is active
 */
G_PASTE_VISIBLE gboolean
g_paste_keybinding_is_active (GPasteKeybinding *self)
{
    g_return_val_if_fail (_G_PASTE_IS_KEYBINDING (self), FALSE);

    return self->active;
}

/**
 * g_paste_keybinding_perform:
 * @self: a #GPasteKeybinding instance
 *
 * Runs the callback associated to the keybinding
 */
G_PASTE_VISIBLE void
g_paste_keybinding_perform (GPasteKeybinding *self)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDING (self));

    self->callback (self, self->user_data);
}

static void
g_paste_keybinding_dispose (GObject *object)
{
    GPasteKeybinding *self = G_PASTE_KEYBINDING (object);

    if (self->active)
        g_paste_keybinding_deactivate (self);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->dispose (object);
}

static void
g_paste_keybinding_finalize (GObject *object)
{
    const GPasteKeybinding *self = G_PASTE_KEYBINDING (object);

    g_free (self->dconf_key);
    g_free (self->description);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->finalize (object);
}

static void
g_paste_keybinding_class_init (GPasteKeybindingClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_keybinding_dispose;
    object_class->finalize = g_paste_keybinding_finalize;
}

static void
g_paste_keybinding_init (GPasteKeybinding *self)
{
    self->active = FALSE;
}

/**
 * g_paste_keybinding_new:
 * @dconf_key: the dconf key to watch
 * @description: a human-readable, translated description of the action
 * @getter: (scope notified): the getter to use to get the binding
 * @callback: (closure user_data) (scope notified): the callback to call when activated
 * @user_data: the data to pass to @callback
 *
 * Create a new instance of #GPasteKeybinding
 *
 * Returns: a newly allocated #GPasteKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_keybinding_new (const gchar           *dconf_key,
                         const gchar           *description,
                         GPasteKeybindingGetter getter,
                         GPasteKeybindingFunc   callback,
                         gpointer               user_data)
{
    g_return_val_if_fail (dconf_key, NULL);
    g_return_val_if_fail (getter, NULL);
    g_return_val_if_fail (callback, NULL);

    GPasteKeybinding *self = g_object_new (G_PASTE_TYPE_KEYBINDING, NULL);

    self->getter = getter;
    self->dconf_key = g_strdup (dconf_key);
    self->description = g_strdup (description);
    self->callback = callback;
    self->user_data = user_data;

    return self;
}
