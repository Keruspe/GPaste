/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>

#include <gpaste-show-history-keybinding.h>

struct _GPasteShowHistoryKeybinding
{
    GPasteKeybinding parent_instance;
};

G_PASTE_DEFINE_TYPE (ShowHistoryKeybinding, show_history_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_show_history_keybinding_class_init (GPasteShowHistoryKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_show_history_keybinding_init (GPasteShowHistoryKeybinding *self G_GNUC_UNUSED)
{
}

static void
g_paste_show_history_keybinding_show_history (GPasteKeybinding *self G_GNUC_UNUSED,
                                              gpointer          data)
{
    GPasteDaemon *gpaste_daemon = data;

    g_paste_daemon_show_history (gpaste_daemon,
                                 NULL); /* error */
}

/**
 * g_paste_show_history_keybinding_new:
 * @gpaste_daemon: a #GPasteDaemon instance
 *
 * Create a new instance of #GPasteShowHistoryKeybinding
 *
 * Returns: a newly allocated #GPasteShowHistoryKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_show_history_keybinding_new (GPasteDaemon *gpaste_daemon)
{
    return g_paste_keybinding_new (G_PASTE_TYPE_SHOW_HISTORY_KEYBINDING,
                                   G_PASTE_SHOW_HISTORY_SETTING,
                                   g_paste_settings_get_show_history,
                                   g_paste_show_history_keybinding_show_history,
                                   gpaste_daemon);
}
