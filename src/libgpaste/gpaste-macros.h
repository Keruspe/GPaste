/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

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
    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;                                                \
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

#define G_PASTE_VISIBLE  __attribute__((visibility("default")))
#define G_PASTE_NORETURN __attribute__((noreturn))

#define G_PASTE_CLEANUP(fun) __attribute__((cleanup(fun)))

#define G_PASTE_CLEANUP_FREE            G_PASTE_CLEANUP (g_paste_free_ptr)
#define G_PASTE_CLEANUP_ARRAY_FREE      G_PASTE_CLEANUP (g_paste_array_free_ptr)
#define G_PASTE_CLEANUP_B_STRV_FREE     G_PASTE_CLEANUP (g_paste_b_strv_free_ptr)
#define G_PASTE_CLEANUP_ERROR_FREE      G_PASTE_CLEANUP (g_paste_error_free_ptr)
#define G_PASTE_CLEANUP_STRING_FREE     G_PASTE_CLEANUP (g_paste_string_free_ptr)
#define G_PASTE_CLEANUP_STRFREEV        G_PASTE_CLEANUP (g_paste_strfreev_ptr)
#define G_PASTE_CLEANUP_UNREF           G_PASTE_CLEANUP (g_paste_unref_ptr)
#define G_PASTE_CLEANUP_DATE_UNREF      G_PASTE_CLEANUP (g_paste_date_unref_ptr)
#define G_PASTE_CLEANUP_LOOP_UNREF      G_PASTE_CLEANUP (g_paste_loop_unref_ptr)
#define G_PASTE_CLEANUP_NODE_INFO_UNREF G_PASTE_CLEANUP (g_paste_node_info_unref_ptr)
#define G_PASTE_CLEANUP_REGEX_UNREF     G_PASTE_CLEANUP (g_paste_regex_unref_ptr)
#define G_PASTE_CLEANUP_GSCHEMA_UNREF   G_PASTE_CLEANUP (g_paste_gschema_unref_ptr)
#define G_PASTE_CLEANUP_TARGETS_UNREF   G_PASTE_CLEANUP (g_paste_targets_unref_ptr)
#define G_PASTE_CLEANUP_VARIANT_UNREF   G_PASTE_CLEANUP (g_paste_variant_unref_ptr)

#define G_PASTE_TRIVIAL_CLEANUP_FUN_FULL(name, type, fun, param_type) \
    static inline void                                                \
    g_paste_##name##_ptr (param_type ptr)                             \
    {                                                                 \
        g_clear_pointer ((type *) ptr, fun);                          \
    }

#define G_PASTE_TRIVIAL_CLEANUP_FUN(name, type, fun) \
    G_PASTE_TRIVIAL_CLEANUP_FUN_FULL (name, type, fun, type *)

#define G_PASTE_CLEANUP_FUN_WITH_ARG(name, type, fun, arg) \
    static inline void                                     \
    g_paste_##name##_ptr (type *ptr)                       \
    {                                                      \
        if (*ptr)                                          \
            fun (*ptr, arg);                               \
    }

#define G_PASTE_BOXED_FREE_REV(box, type) g_boxed_free (type, box)

G_PASTE_TRIVIAL_CLEANUP_FUN_FULL (free,            gpointer,           g_free,                gpointer)

G_PASTE_TRIVIAL_CLEANUP_FUN      (error_free,      GError *,           g_error_free)
G_PASTE_TRIVIAL_CLEANUP_FUN      (strfreev,        GStrv,              g_strfreev)

G_PASTE_TRIVIAL_CLEANUP_FUN_FULL (unref,           GObject *,          g_object_unref,        gpointer)

G_PASTE_TRIVIAL_CLEANUP_FUN      (date_unref,      GDateTime *,        g_date_time_unref)
G_PASTE_TRIVIAL_CLEANUP_FUN      (loop_unref,      GMainLoop *,        g_main_loop_unref)
G_PASTE_TRIVIAL_CLEANUP_FUN      (node_info_unref, GDBusNodeInfo *,    g_dbus_node_info_unref)
G_PASTE_TRIVIAL_CLEANUP_FUN      (regex_unref,     GRegex *,           g_regex_unref)
G_PASTE_TRIVIAL_CLEANUP_FUN      (gschema_unref,   GSettingsSchema *,  g_settings_schema_unref)
G_PASTE_TRIVIAL_CLEANUP_FUN      (targets_unref,   GtkTargetList *,    gtk_target_list_unref)
G_PASTE_TRIVIAL_CLEANUP_FUN      (variant_unref,   GVariant *,         g_variant_unref)

G_PASTE_CLEANUP_FUN_WITH_ARG     (array_free,      GArray *,           g_array_free,           FALSE)
G_PASTE_CLEANUP_FUN_WITH_ARG     (b_strv_free,     GStrv,              G_PASTE_BOXED_FREE_REV, G_TYPE_STRV)
G_PASTE_CLEANUP_FUN_WITH_ARG     (string_free,     GString *,          g_string_free,          TRUE)

G_END_DECLS

#endif /*__GPASTE_MACROS_H__*/
