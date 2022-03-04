/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-keybinding.h>

typedef struct _GPasteKeybindingPrivate
{
    GPasteKeybindingGetter getter;
    gchar                 *dconf_key;
    GPasteKeybindingFunc   callback;
    gpointer               user_data;
    gboolean               active;
    GdkModifierType        modifiers;
    guint32               *keycodes;
} GPasteKeybindingPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (Keybinding, keybinding, G_TYPE_OBJECT)

/**
 * g_paste_keybinding_get_modifiers:
 * @self: a #GPasteKeybinding instance
 *
 * Get the modifiers for this keybinding
 *
 * Returns: the modifiers
 */
G_PASTE_VISIBLE GdkModifierType
g_paste_keybinding_get_modifiers (const GPasteKeybinding *self)
{
    g_return_val_if_fail (_G_PASTE_IS_KEYBINDING (self), 0);

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    return priv->modifiers;
}

/**
 * g_paste_keybinding_get_keycodes:
 * @self: a #GPasteKeybinding instance
 *
 * Get the keycodes for this keybinding
 *
 * Returns: the keycodes
 */
G_PASTE_VISIBLE const guint32 *
g_paste_keybinding_get_keycodes (const GPasteKeybinding *self)
{
    g_return_val_if_fail (_G_PASTE_IS_KEYBINDING (self), NULL);

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    return priv->keycodes;
}

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

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    return priv->dconf_key;
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

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    return priv->getter (settings);
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

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    g_return_if_fail (!priv->active);

    const gchar *binding = priv->getter (settings);

    if (binding)
    {
        gtk_accelerator_parse_with_keycode (binding, NULL, &priv->keycodes, &priv->modifiers);

        priv->active = priv->keycodes != NULL;
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

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    g_return_if_fail (priv->active);

    priv->active = FALSE;
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

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    return priv->active;
}

static gboolean
g_paste_keybinding_private_match (const GPasteKeybindingPrivate *priv,
                                  GdkModifierType                modifiers,
                                  guint64                        keycode)
{
    if (priv->keycodes && priv->modifiers == (priv->modifiers & modifiers))
    {
        for (guint32 *_keycode = priv->keycodes; *_keycode; ++_keycode)
        {
            if (keycode == *_keycode)
                return TRUE;
        }
    }

    return FALSE;
}

/**
 * g_paste_keybinding_notify:
 * @self: a #GPasteKeybinding instance
 * @modifiers: The modifiers of the current event
 * @keycode: the keycode of the current event
 *
 * Runs the callback associated to the keybinding if needed
 */
G_PASTE_VISIBLE void
g_paste_keybinding_notify (GPasteKeybinding *self,
                           GdkModifierType   modifiers,
                           guint64           keycode)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDING (self));

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    if (keycode && g_paste_keybinding_private_match (priv, modifiers, keycode))
        priv->callback (self, priv->user_data);
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

    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    priv->callback (self, priv->user_data);
}

static void
g_paste_keybinding_dispose (GObject *object)
{
    GPasteKeybinding *self = G_PASTE_KEYBINDING (object);
    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (self);

    if (priv->active)
        g_paste_keybinding_deactivate (self);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->dispose (object);
}

static void
g_paste_keybinding_finalize (GObject *object)
{
    const GPasteKeybindingPrivate *priv = _g_paste_keybinding_get_instance_private (G_PASTE_KEYBINDING (object));

    g_free (priv->keycodes);
    g_free (priv->dconf_key);

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
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    priv->active = FALSE;
}

/**
 * g_paste_keybinding_new:
 * @type: the type of the subclass to instantiate
 * @dconf_key: the dconf key to watch
 * @getter: (scope notified): the getter to use to get the binding
 * @callback: (closure user_data) (scope notified): the callback to call when activated
 * @user_data: (closure): the data to pass to @callback, defaults to self/this
 *
 * Create a new instance of #GPasteKeybinding
 *
 * Returns: a newly allocated #GPasteKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_keybinding_new (GType                  type,
                         const gchar           *dconf_key,
                         GPasteKeybindingGetter getter,
                         GPasteKeybindingFunc   callback,
                         gpointer               user_data)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_KEYBINDING), NULL);
    g_return_val_if_fail (dconf_key, NULL);
    g_return_val_if_fail (getter, NULL);
    g_return_val_if_fail (callback, NULL);

    GPasteKeybinding *self = g_object_new (type, NULL);
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    priv->getter = getter;
    priv->dconf_key = g_strdup (dconf_key);
    priv->callback = callback;
    priv->user_data = user_data;
    priv->keycodes = NULL;

    return self;
}
