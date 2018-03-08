/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-shortcuts-window.h>

struct _GPasteUiShortcutsWindow
{
    GtkShortcutsWindow parent_instance;
};

G_PASTE_DEFINE_TYPE (UiShortcutsWindow, ui_shortcuts_window, GTK_TYPE_SHORTCUTS_WINDOW)

static void
g_paste_ui_shortcuts_window_class_init (GPasteUiShortcutsWindowClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_shortcuts_window_init (GPasteUiShortcutsWindow *self G_GNUC_UNUSED)
{
}

static inline void
add_shortcut (GtkContainer *group,
              const gchar  *title,
              const gchar  *accelerator)
{
    gtk_container_add (group, gtk_widget_new (GTK_TYPE_SHORTCUTS_SHORTCUT,
                                              "visible",     TRUE,
                                              "title",       title,
                                              "accelerator", accelerator,
                                              NULL));
}

/**
 * g_paste_ui_shortcuts_window_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteUiShortcutsWindow
 *
 * Returns: a newly allocated #GPasteUiShortcutsWindow
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_shortcuts_window_new (const GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_SHORTCUTS_WINDOW,
                                      "type",            GTK_WINDOW_TOPLEVEL,
                                      "window-position", GTK_WIN_POS_CENTER_ALWAYS,
                                      "modal",           TRUE,
                                      "resizable",       FALSE,
                                      NULL);
    GtkWidget *section = gtk_widget_new (GTK_TYPE_SHORTCUTS_SECTION,
                                         "section-name", "shortcuts",
                                         "visible",      TRUE,
                                         "max-height",   10,
                                         NULL);
    GtkWidget *general_group = gtk_widget_new (GTK_TYPE_SHORTCUTS_GROUP,
                                               "title",   _("General"),
                                               "visible", TRUE,
                                               NULL);
    GtkContainer *ggroup = GTK_CONTAINER (general_group);

    gtk_container_add (GTK_CONTAINER (self), section);
    gtk_container_add (GTK_CONTAINER (section), general_group);

    add_shortcut (ggroup, _("Delete the active item from history"),          g_paste_settings_get_pop (settings));
    add_shortcut (ggroup, _("Launch the graphical tool"),                    g_paste_settings_get_launch_ui (settings));
    add_shortcut (ggroup, _("Mark the active item as being a password"),     g_paste_settings_get_make_password (settings));
    add_shortcut (ggroup, _("Display the history"),                          g_paste_settings_get_show_history (settings));
    add_shortcut (ggroup, _("Sync the clipboard to the primary selection"),  g_paste_settings_get_sync_clipboard_to_primary (settings));
    add_shortcut (ggroup, _("Sync the primary selection to the clipboard"),  g_paste_settings_get_sync_primary_to_clipboard (settings));
    add_shortcut (ggroup, _("Upload the active item to a pastebin service"), g_paste_settings_get_upload (settings));

    return self;
}
