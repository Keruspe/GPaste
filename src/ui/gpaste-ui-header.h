// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

void g_paste_ui_header_set_subtitle (AdwHeaderBar *self,
                                     const gchar  *subtitle);

GtkToggleButton *g_paste_ui_header_get_favourites_button (AdwHeaderBar *self);
GtkToggleButton *g_paste_ui_header_get_search_button     (AdwHeaderBar *self);

GtkWidget *g_paste_ui_header_get_merge_button  (AdwHeaderBar *self);
GtkWidget *g_paste_ui_header_get_cancel_button (AdwHeaderBar *self);
void g_paste_ui_header_set_selection_mode  (AdwHeaderBar *self,
                                            gboolean      selection_mode);
void g_paste_ui_header_set_selection_count (AdwHeaderBar *self,
                                            guint         count);

GtkWidget *g_paste_ui_header_new (void);

G_END_DECLS
