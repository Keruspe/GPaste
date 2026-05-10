/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>
#include <gpaste/gpaste-util.h>

#include <gpaste-ui-keybinding.h>

struct _GPasteUiKeybinding
{
    GPasteKeybinding parent_instance;
};

G_PASTE_DEFINE_TYPE (UiKeybinding, ui_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_ui_keybinding_class_init (GPasteUiKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_keybinding_init (GPasteUiKeybinding *self G_GNUC_UNUSED)
{
}

static void
launch_ui (GPasteKeybinding *self G_GNUC_UNUSED,
           gpointer          data G_GNUC_UNUSED)
{
    g_paste_util_spawn ("Ui");
}

/**
 * g_paste_ui_keybinding_new:
 *
 * Create a new instance of #GPasteUiKeybinding
 *
 * Returns: a newly allocated #GPasteUiKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_ui_keybinding_new (void)
{
    return g_paste_keybinding_new (G_PASTE_TYPE_UI_KEYBINDING,
                                   G_PASTE_LAUNCH_UI_SETTING,
                                   g_paste_settings_get_launch_ui,
                                   launch_ui,
                                   NULL);
}
