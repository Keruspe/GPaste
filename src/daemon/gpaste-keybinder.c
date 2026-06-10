// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-keybinder.h>

struct _GPasteKeybinder
{
    GObject parent_instance;
};

typedef struct
{
    GHashTable               *keybindings;  /* const gchar * (borrowed from _Keybinding) → _Keybinding * */

    GPasteSettings                *settings;
    GPasteGlobalShortcutClient *provider;
    GSignalGroup                  *provider_signals;
} GPasteKeybinderPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Keybinder, keybinder, G_TYPE_OBJECT)

/***********************************/
/* Wrapper around GPasteKeybinding */
/***********************************/

typedef struct
{
    GPasteKeybinder  *keybinder; /* not ref'd */
    GPasteKeybinding *binding;
    GPasteSettings   *settings;
    GSignalGroup     *signal_group;
} _Keybinding;

static void
_keybinding_activate (_Keybinding *k)
{
    if (!g_paste_keybinding_is_active (k->binding))
        g_paste_keybinding_activate (k->binding, k->settings);
}

static void
_keybinding_deactivate (_Keybinding *k)
{
    if (g_paste_keybinding_is_active (k->binding))
        g_paste_keybinding_deactivate (k->binding);
}

static void
_keybinding_rebind (_Keybinding    *k,
                    GPasteSettings *settings G_GNUC_UNUSED)
{
    g_paste_keybinder_deactivate_all (k->keybinder);
    g_paste_keybinder_activate_all (k->keybinder);
}

static _Keybinding *
_keybinding_new (GPasteKeybinder  *keybinder,
                 GPasteKeybinding *binding,
                 GPasteSettings   *settings)
{
    _Keybinding *k = g_new (_Keybinding, 1);

    k->keybinder = keybinder;
    k->binding = binding;
    k->settings = g_object_ref (settings);

    g_autofree gchar *detailed_signal = g_strdup_printf ("rebind::%s", g_paste_keybinding_get_dconf_key (binding));

    k->signal_group = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect_swapped (k->signal_group, detailed_signal, G_CALLBACK (_keybinding_rebind), k);
    g_signal_group_set_target (k->signal_group, settings);
    return k;
}

static void
_keybinding_free (gpointer data)
{
    _Keybinding *k = data;
    g_clear_object (&k->signal_group);
    g_object_unref (k->binding);
    g_object_unref (k->settings);
    g_free (k);
}

/**
 * g_paste_keybinder_add_keybinding:
 * @self: a #GPasteKeybinder instance
 * @binding: (transfer full): a #GPasteKeybinding instance
 *
 * Add a new keybinding
 */
G_PASTE_VISIBLE void
g_paste_keybinder_add_keybinding (GPasteKeybinder  *self,
                                  GPasteKeybinding *binding)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDER (self));
    g_return_if_fail (_G_PASTE_IS_KEYBINDING (binding));

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_hash_table_insert (priv->keybindings,
                         (gpointer) g_paste_keybinding_get_dconf_key (binding),
                         _keybinding_new (self, binding, priv->settings));
}

/**
 * g_paste_keybinder_activate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Activate all the managed keybindings
 */
G_PASTE_VISIBLE void
g_paste_keybinder_activate_all (GPasteKeybinder *self)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_autoptr (GList) values = g_hash_table_get_values (priv->keybindings);

    gsize n = 0;
    for (GList *l = values; l; l = g_list_next (l))
    {
        _Keybinding *k = l->data;
        _keybinding_activate (k);
        if (g_paste_keybinding_is_active (k->binding))
            n++;
    }

    g_autofree GPasteKeybindingAccelerator *accels = g_new (GPasteKeybindingAccelerator, n + 1);
    gsize i = 0;
    for (GList *l = values; l; l = g_list_next (l))
    {
        _Keybinding *k = l->data;
        if (g_paste_keybinding_is_active (k->binding))
        {
            accels[i++] = G_PASTE_KEYBINDING_ACCELERATOR (
                g_paste_keybinding_get_dconf_key (k->binding),
                g_paste_keybinding_get_accelerator (k->binding, k->settings),
                g_paste_keybinding_get_description (k->binding));
        }
    }
    accels[i].id = NULL;

    g_paste_global_shortcut_client_grab_all (priv->provider, accels);
}

/**
 * g_paste_keybinder_deactivate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Deactivate all the managed keybindings
 */
G_PASTE_VISIBLE void
g_paste_keybinder_deactivate_all (GPasteKeybinder *self)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_paste_global_shortcut_client_ungrab_all (priv->provider);

    g_autoptr (GList) values = g_hash_table_get_values (priv->keybindings);
    for (GList *l = values; l; l = g_list_next (l))
        _keybinding_deactivate (l->data);
}

static void
on_keybinding_activated (GPasteGlobalShortcutClient *provider G_GNUC_UNUSED,
                         const gchar                   *id,
                         gpointer                       user_data)
{
    GPasteKeybinderPrivate *priv = user_data;
    _Keybinding *k = g_hash_table_lookup (priv->keybindings, id);

    if (k && g_paste_keybinding_is_active (k->binding))
        g_paste_keybinding_perform (k->binding);
}

static void
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    if (priv->settings)
    {
        g_clear_object (&priv->settings);
        g_paste_global_shortcut_client_ungrab_all (priv->provider);
        g_clear_pointer (&priv->keybindings, g_hash_table_unref);
        g_clear_object (&priv->provider_signals);
        g_clear_object (&priv->provider);
    }

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_keybinder_dispose;
}

static void
g_paste_keybinder_init (GPasteKeybinder *self)
{
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->keybindings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, _keybinding_free);
}

/**
 * g_paste_keybinder_new:
 * @settings: a #GPasteSettings instance
 * @provider: a #GPasteGlobalShortcutClient instance
 *
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (GPasteSettings                *settings,
                       GPasteGlobalShortcutClient *provider)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_GLOBAL_SHORTCUT_CLIENT (provider), NULL);

    GPasteKeybinder *self = G_PASTE_KEYBINDER (g_object_new (G_PASTE_TYPE_KEYBINDER, NULL));
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->settings = g_object_ref (settings);
    priv->provider = g_object_ref (provider);

    GSignalGroup *provider_signals = priv->provider_signals = g_signal_group_new (G_PASTE_TYPE_GLOBAL_SHORTCUT_CLIENT);
    g_signal_group_connect (provider_signals, "keybinding-activated", G_CALLBACK (on_keybinding_activated), priv);
    g_signal_group_set_target (provider_signals, provider);

    return self;
}
