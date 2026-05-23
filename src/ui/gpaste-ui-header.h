// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_HEADER_H__
#define __G_PASTE_UI_HEADER_H__

#include <gpaste/gpaste-client.h>

#include <adwaita.h>

G_BEGIN_DECLS

void g_paste_ui_header_show_prefs   (AdwHeaderBar *self);
void g_paste_ui_header_set_subtitle (AdwHeaderBar *self,
                                     const gchar  *subtitle);

GtkToggleButton *g_paste_ui_header_get_search_button (AdwHeaderBar *self);

GtkWidget *g_paste_ui_header_new (GtkWindow    *topwin,
                                  GPasteClient *client);

G_END_DECLS

#endif /*__G_PASTE_UI_HEADER_H__*/
