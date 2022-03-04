/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>

#include <gpaste-pop-keybinding.h>

struct _GPastePopKeybinding
{
    GPasteKeybinding parent_instance;
};

typedef struct
{
    GPasteHistory *history;
} GPastePopKeybindingPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (PopKeybinding, pop_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_pop_keybinding_dispose (GObject *object)
{
    GPastePopKeybindingPrivate *priv = g_paste_pop_keybinding_get_instance_private (G_PASTE_POP_KEYBINDING (object));

    g_clear_object (&priv->history);

    G_OBJECT_CLASS (g_paste_pop_keybinding_parent_class)->dispose (object);
}

static void
g_paste_pop_keybinding_class_init (GPastePopKeybindingClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_pop_keybinding_dispose;
}

static void
g_paste_pop_keybinding_init (GPastePopKeybinding *self G_GNUC_UNUSED)
{
}

static void
pop (GPasteKeybinding *self,
     gpointer          data G_GNUC_UNUSED)
{
    const GPastePopKeybindingPrivate *priv = _g_paste_pop_keybinding_get_instance_private (G_PASTE_POP_KEYBINDING (self));

    g_paste_history_remove (priv->history, 0);
}

/**
 * g_paste_pop_keybinding_new:
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPastePopKeybinding
 *
 * Returns: a newly allocated #GPastePopKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_pop_keybinding_new (GPasteHistory *history)
{
    g_return_val_if_fail (_G_PASTE_IS_HISTORY (history), NULL);

    GPasteKeybinding *self = g_paste_keybinding_new (G_PASTE_TYPE_POP_KEYBINDING,
                                                     G_PASTE_POP_SETTING,
                                                     g_paste_settings_get_pop,
                                                     pop,
                                                     NULL);
    GPastePopKeybindingPrivate *priv = g_paste_pop_keybinding_get_instance_private (G_PASTE_POP_KEYBINDING (self));

    priv->history = g_object_ref (history);

    return self;
}
