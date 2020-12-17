/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_HEADER_H__
#define __G_PASTE_UI_HEADER_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_HEADER (g_paste_ui_header_get_type ())

G_PASTE_FINAL_TYPE (UiHeader, ui_header, UI_HEADER, GtkHeaderBar)

void g_paste_ui_header_show_prefs (const GPasteUiHeader *self);

GtkButton *g_paste_ui_header_get_search_button (const GPasteUiHeader *self);

GtkWidget *g_paste_ui_header_new (GtkWindow    *topwin,
                                  GPasteClient *client);

G_END_DECLS

#endif /*__G_PASTE_UI_HEADER_H__*/
