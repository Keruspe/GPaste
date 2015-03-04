/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __GPASTE_MACROS_H__
#define __GPASTE_MACROS_H__

#include <gpaste-util.h>

#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <stdlib.h>

G_BEGIN_DECLS

#define G_PASTE_VISIBLE  __attribute__((visibility("default")))
#define G_PASTE_NORETURN __attribute__((noreturn))

#define G_PASTE_DERIVABLE_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_DERIVABLE_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName)

#define G_PASTE_FINAL_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_FINAL_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName)

#define G_PASTE_INIT_GETTEXT()                          \
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);        \
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8"); \
    textdomain (GETTEXT_PACKAGE)

#define G_PASTE_INIT_APPLICATION_FULL(name, activate_cb)                                            \
    G_PASTE_INIT_GETTEXT ();                                                                        \
    gtk_init (&argc, &argv);                                                                        \
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL);    \
    GtkApplication *app = gtk_application_new ("org.gnome.GPaste." name, G_APPLICATION_FLAGS_NONE); \
    GApplication *gapp = G_APPLICATION (app);                                                       \
    g_autoptr (GError) error = NULL;                                                                \
    G_APPLICATION_GET_CLASS (gapp)->activate = activate_cb;                                         \
    g_application_register (gapp, NULL, &error);                                                    \
    if (error)                                                                                      \
    {                                                                                               \
        fprintf (stderr, "%s: %s\n", _("Failed to register the gtk application"), error->message);  \
        return EXIT_FAILURE;                                                                        \
    }                                                                                               \
    if (g_application_get_is_remote (gapp))                                                         \
    {                                                                                               \
        g_application_activate (gapp);                                                              \
        return EXIT_SUCCESS;                                                                        \
    }

#define G_PASTE_INIT_APPLICATION(name) \
    G_PASTE_INIT_APPLICATION_FULL (name, NULL)

static inline void
about_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    g_paste_util_show_about_dialog (GTK_WINDOW (gtk_application_get_windows (GTK_APPLICATION (user_data))->data));
}

static inline void
quit_activated (GSimpleAction *action    G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data)
{
    g_application_quit (G_APPLICATION (user_data));
}


#define G_PASTE_INIT_APPLICATION_WITH_WIN(name)                                                         \
    G_PASTE_INIT_APPLICATION_FULL (name, g_paste_util_show_win)                                         \
    GActionEntry app_entries[] = {                                                                      \
        { "about", about_activated, NULL, NULL, NULL, { 0 } },                                          \
        { "quit",  quit_activated,  NULL, NULL, NULL, { 0 } }                                           \
    };                                                                                                  \
    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app); \
    GMenu *menu = g_menu_new ();                                                                        \
    g_menu_append (menu, "About GPaste", "app.about");                                                  \
    g_menu_append (menu, "Quit", "app.quit");                                                           \
    gtk_application_set_app_menu (app, G_MENU_MODEL (menu))


#define G_PASTE_CLEANUP_STRING_FREE __attribute__((cleanup(g_paste_string_free_ptr)))

static inline void
g_paste_string_free_ptr (GString **ptr)
{
    if (*ptr)
        g_string_free (*ptr, TRUE);
}

G_END_DECLS

#endif /*__GPASTE_MACROS_H__*/
