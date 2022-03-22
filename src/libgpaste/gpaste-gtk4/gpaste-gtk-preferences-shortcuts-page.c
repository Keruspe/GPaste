/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h>

struct _GPasteGtkPreferencesShortcutsPage
{
    GPasteGtkPreferencesPage parent_instance;
};

typedef struct
{
    GPasteGtkPreferencesManager *manager;

    GtkEntryBuffer              *launch_ui_entry;
    GtkEntryBuffer              *show_history_entry;

    GtkEntryBuffer              *make_password_entry;
    GtkEntryBuffer              *upload_entry;
    GtkEntryBuffer              *pop_entry;

    GtkEntryBuffer              *sync_clipboard_to_primary_entry;
    GtkEntryBuffer              *sync_primary_to_clipboard_entry;
} GPasteGtkPreferencesShortcutsPagePrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesShortcutsPage, preferences_shortcuts_page, G_PASTE_TYPE_GTK_PREFERENCES_PAGE)

static void
g_paste_gtk_preferences_shortcuts_page_setting_changed (GPasteGtkPreferencesPage *self,
                                                        GPasteSettings           *settings,
                                                        const gchar              *key)
{
    GPasteGtkPreferencesShortcutsPagePrivate *priv = g_paste_gtk_preferences_shortcuts_page_get_instance_private (G_PASTE_GTK_PREFERENCES_SHORTCUTS_PAGE (self));

    if (g_paste_str_equal (key, G_PASTE_LAUNCH_UI_SETTING))
        gtk_entry_buffer_set_text (priv->launch_ui_entry, g_paste_settings_get_launch_ui (settings), -1);
    else if (g_paste_str_equal (key, G_PASTE_MAKE_PASSWORD_SETTING))
        gtk_entry_buffer_set_text (priv->make_password_entry, g_paste_settings_get_make_password (settings), -1);
    else if (g_paste_str_equal (key, G_PASTE_POP_SETTING))
        gtk_entry_buffer_set_text (priv->pop_entry, g_paste_settings_get_pop (settings), -1);
    else if (g_paste_str_equal (key, G_PASTE_SHOW_HISTORY_SETTING))
        gtk_entry_buffer_set_text (priv->show_history_entry, g_paste_settings_get_show_history (settings), -1);
    else if (g_paste_str_equal (key, G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING))
        gtk_entry_buffer_set_text (priv->sync_clipboard_to_primary_entry, g_paste_settings_get_sync_clipboard_to_primary (settings), -1);
    else if (g_paste_str_equal (key, G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING))
        gtk_entry_buffer_set_text (priv->sync_primary_to_clipboard_entry, g_paste_settings_get_sync_primary_to_clipboard (settings), -1);
    else if (g_paste_str_equal (key, G_PASTE_UPLOAD_SETTING))
        gtk_entry_buffer_set_text (priv->upload_entry, g_paste_settings_get_upload (settings), -1);
}

static void
g_paste_gtk_preferences_shortcuts_page_dispose (GObject *object)
{
    GPasteGtkPreferencesShortcutsPagePrivate *priv = g_paste_gtk_preferences_shortcuts_page_get_instance_private (G_PASTE_GTK_PREFERENCES_SHORTCUTS_PAGE (object));

    if (priv->manager) /* first dispose call */
    {
        g_paste_gtk_preferences_manager_deregister (priv->manager, G_PASTE_GTK_PREFERENCES_PAGE (object));
        g_clear_object (&priv->manager);
    }

    G_OBJECT_CLASS (g_paste_gtk_preferences_shortcuts_page_parent_class)->dispose (object);
}

static void
g_paste_gtk_preferences_shortcuts_page_class_init (GPasteGtkPreferencesShortcutsPageClass *klass)
{
    G_PASTE_GTK_PREFERENCES_PAGE_CLASS (klass)->setting_changed = g_paste_gtk_preferences_shortcuts_page_setting_changed;

    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_preferences_shortcuts_page_dispose;
}

static void
g_paste_gtk_preferences_shortcuts_page_init (GPasteGtkPreferencesShortcutsPage *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_shortcuts_page_new:
 * @manager: a #GPasteGtkPreferencesManager instance
 *
 * Create a new instance of #GPasteGtkPreferencesShortcutsPage
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesShortcutsPage
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_shortcuts_page_new (GPasteGtkPreferencesManager *manager)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (manager), NULL);

    GPasteGtkPreferencesShortcutsPage *self = G_PASTE_GTK_PREFERENCES_SHORTCUTS_PAGE (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_SHORTCUTS_PAGE,
                                                                                                    "name", "shortcuts",
                                                                                                    "title", _("Keyboard shortcuts"),
                                                                                                    "icon-name", "preferences-desktop-keyboard-shortcuts",
                                                                                                    NULL));
    GPasteGtkPreferencesShortcutsPagePrivate *priv = g_paste_gtk_preferences_shortcuts_page_get_instance_private (self);
    GPasteSettings *settings = g_paste_gtk_preferences_manager_get_settings (manager);
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE (self);

    priv->manager = g_object_ref (manager);

    g_paste_gtk_preferences_manager_register (manager, G_PASTE_GTK_PREFERENCES_PAGE (self));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("History access"));
    /* translators: Keyboard shortcut to launch the graphical tool */
    priv->launch_ui_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                            _("Launch the graphical tool"),
                                                                            g_paste_settings_get_launch_ui (settings),
                                                                            g_paste_settings_set_launch_ui,
                                                                            g_paste_settings_reset_launch_ui,
                                                                            settings);
    /* translators: Keyboard shortcut to display the history */
    priv->show_history_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                               _("Display the history"),
                                                                               g_paste_settings_get_show_history (settings),
                                                                               g_paste_settings_set_show_history,
                                                                               g_paste_settings_reset_show_history,
                                                                               settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Active element manipulation"));
    /* translators: Keyboard shortcut to mark the active item as being a password */
    priv->make_password_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                                _("Mark the active item as being a password"),
                                                                                g_paste_settings_get_make_password (settings),
                                                                                g_paste_settings_set_make_password,
                                                                                g_paste_settings_reset_make_password,
                                                                                settings);
    /* translators: Keyboard shortcut to upload the active item from history to a pastebin service */
    priv->upload_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                         _("Upload the active item to a pastebin service"),
                                                                         g_paste_settings_get_upload (settings),
                                                                         g_paste_settings_set_upload,
                                                                         g_paste_settings_reset_upload,
                                                                         settings);
    /* translators: Keyboard shortcut to delete the active item from history */
    priv->pop_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                      _("Delete the active item from history"),
                                                                      g_paste_settings_get_pop (settings),
                                                                      g_paste_settings_set_pop,
                                                                      g_paste_settings_reset_pop,
                                                                      settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Clipboards synchronization"));
    /* translators: Keyboard shortcut to sync the clipboard to the primary selection */
    priv->sync_clipboard_to_primary_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                                            _("Sync the clipboard to the primary selection"),
                                                                                            g_paste_settings_get_sync_clipboard_to_primary (settings),
                                                                                            g_paste_settings_set_sync_clipboard_to_primary,
                                                                                            g_paste_settings_reset_sync_clipboard_to_primary,
                                                                                            settings);
    /* translators: Keyboard shortcut to sync the primary selection to the clipboard */
    priv->sync_primary_to_clipboard_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                                            _("Sync the primary selection to the clipboard"),
                                                                                            g_paste_settings_get_sync_primary_to_clipboard (settings),
                                                                                            g_paste_settings_set_sync_primary_to_clipboard,
                                                                                            g_paste_settings_reset_sync_primary_to_clipboard,
                                                                                            settings);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    return GTK_WIDGET (self);
}
