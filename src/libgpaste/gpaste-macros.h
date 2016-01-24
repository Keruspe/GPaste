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

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <stdlib.h>

G_BEGIN_DECLS

#ifdef G_HAVE_GNUC_VISIBILITY
#  define G_PASTE_VISIBLE __attribute__((visibility("default")))
#else
#  define G_PASTE_VISIBLE
#endif

#define G_PASTE_CONST_CHECKER(TYPE_NAME)            \
  static inline gboolean                            \
  _G_PASTE_IS_##TYPE_NAME (gconstpointer ptr) {     \
    return G_PASTE_IS_##TYPE_NAME ((gpointer) ptr); \
  }

#define G_PASTE_DERIVABLE_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName)                                           \
    G_PASTE_VISIBLE G_DECLARE_DERIVABLE_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName) \
    G_PASTE_CONST_CHECKER (TYPE_NAME)

#define G_PASTE_FINAL_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName)                                           \
    G_PASTE_VISIBLE G_DECLARE_FINAL_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName) \
    G_PASTE_CONST_CHECKER (TYPE_NAME)

#define G_PASTE_CONST_PRIV_ACCESSOR(TypeName, type_name)                             \
    static inline gconstpointer                                                      \
    _g_paste_##type_name##_get_instance_private (const GPaste##TypeName *self)       \
    {                                                                                \
      return g_paste_##type_name##_get_instance_private ((GPaste##TypeName *) self); \
    }

#define G_PASTE_DEFINE_TYPE(TypeName, type_name, ParentTypeName)          \
    G_DEFINE_TYPE (GPaste##TypeName, g_paste_##type_name, ParentTypeName) \
    G_PASTE_CONST_PRIV_ACCESSOR (TypeName, type_name)

#define G_PASTE_DEFINE_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName)          \
    G_DEFINE_TYPE_WITH_PRIVATE (GPaste##TypeName, g_paste_##type_name, ParentTypeName) \
    G_PASTE_CONST_PRIV_ACCESSOR (TypeName, type_name)

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
    }

#define G_PASTE_INIT_APPLICATION(name) \
    G_PASTE_INIT_APPLICATION_FULL (name, NULL)

G_END_DECLS

#endif /*__GPASTE_MACROS_H__*/
