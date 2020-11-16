/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gsettings-keys.h>
#include <gpaste-make-password-keybinding.h>

struct _GPasteMakePasswordKeybinding
{
    GPasteKeybinding parent_instance;
};

G_PASTE_DEFINE_TYPE (MakePasswordKeybinding, make_password_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_make_password_keybinding_class_init (GPasteMakePasswordKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_make_password_keybinding_init (GPasteMakePasswordKeybinding *self G_GNUC_UNUSED)
{
}

static void
g_paste_make_password_keybinding_make_password (GPasteKeybinding *self G_GNUC_UNUSED,
                                                gpointer          data)
{
    GPasteHistory *history = data;
    const GPasteItem *first = g_paste_history_get (history, 0);

    if (!first)
        return;

    g_paste_history_set_password (history, g_paste_item_get_uuid (first), NULL);
}

/**
 * g_paste_make_password_keybinding_new:
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPasteMakePasswordKeybinding
 *
 * Returns: a newly allocated #GPasteMakePasswordKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_make_password_keybinding_new (GPasteHistory *history)
{
    return g_paste_keybinding_new (G_PASTE_TYPE_MAKE_PASSWORD_KEYBINDING,
                                   G_PASTE_MAKE_PASSWORD_SETTING,
                                   g_paste_settings_get_make_password,
                                   g_paste_make_password_keybinding_make_password,
                                   history);
}
