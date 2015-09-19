/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_UI_PANEL_H__
#define __G_PASTE_UI_PANEL_H__

#include <gpaste-client.h>
#include <gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_PANEL (g_paste_ui_panel_get_type ())

G_PASTE_FINAL_TYPE (UiPanel, ui_panel, UI_PANEL, GtkBox)

void g_paste_ui_panel_update_history_length (GPasteUiPanel *self,
                                             const gchar   *history,
                                             guint64        length);

GtkWidget *g_paste_ui_panel_new (GPasteClient   *client,
                                 GPasteSettings *settings,
                                 GtkWindow      *rootwin,
                                 GtkSearchEntry *search_entry);

G_END_DECLS

#endif /*__G_PASTE_UI_PANEL_H__*/
