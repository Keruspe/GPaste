/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_PREFERENCES_WIDGET (g_paste_gtk_preferences_widget_get_type ())

G_PASTE_FINAL_TYPE (GtkPreferencesWidget, gtk_preferences_widget, GTK_PREFERENCES_WIDGET, GtkBox)

GtkWidget *g_paste_gtk_preferences_widget_new (void);

G_END_DECLS
