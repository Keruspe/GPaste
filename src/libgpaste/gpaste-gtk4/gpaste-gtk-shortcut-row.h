// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-gtk4/gpaste-gtk-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_SHORTCUT_ROW (g_paste_gtk_shortcut_row_get_type ())

G_PASTE_GTK_FINAL_TYPE (ShortcutRow, shortcut_row, SHORTCUT_ROW, AdwActionRow)

const gchar *g_paste_gtk_shortcut_row_get_accelerator (GPasteGtkShortcutRow *self);
void         g_paste_gtk_shortcut_row_set_accelerator (GPasteGtkShortcutRow *self,
                                                       const gchar          *accelerator);

GtkWidget *g_paste_gtk_shortcut_row_new (const gchar *title);

G_END_DECLS
