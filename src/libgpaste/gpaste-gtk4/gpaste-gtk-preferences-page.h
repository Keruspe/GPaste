// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste-gtk4/gpaste-gtk-macros.h>

G_BEGIN_DECLS

/* Declared rather than included: the manager's header includes this one, and a
 * page is registered with a manager the moment it is built. */
typedef struct _GPasteGtkPreferencesManager GPasteGtkPreferencesManager;

#define G_PASTE_TYPE_GTK_PREFERENCES_PAGE (g_paste_gtk_preferences_page_get_type ())

G_PASTE_GTK_DERIVABLE_TYPE (PreferencesPage, preferences_page, PREFERENCES_PAGE, AdwPreferencesPage)

struct _GPasteGtkPreferencesPageClass
{
    AdwPreferencesPageClass parent_class;

    /*< pure virtual >*/
    void (*setting_changed) (GPasteGtkPreferencesPage *self,
                             GPasteSettings           *settings,
                             const gchar              *key);
};

void g_paste_gtk_preferences_page_setting_changed (GPasteGtkPreferencesPage *self,
                                                   GPasteSettings           *settings,
                                                   const gchar              *key);

GtkWidget *g_paste_gtk_preferences_page_register (GPasteGtkPreferencesPage    *self,
                                                  GPasteGtkPreferencesManager *manager);

G_END_DECLS
