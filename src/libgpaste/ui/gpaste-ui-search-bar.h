/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SEARCH_BAR_H__
#define __G_PASTE_UI_SEARCH_BAR_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_SEARCH_BAR (g_paste_ui_search_bar_get_type ())

G_PASTE_FINAL_TYPE (UiSearchBar, ui_search_bar, UI_SEARCH_BAR, GtkSearchBar)

GtkSearchEntry *g_paste_ui_search_bar_get_entry (const GPasteUiSearchBar *self);

GtkWidget *g_paste_ui_search_bar_new (void);

G_END_DECLS

#endif /*__G_PASTE_UI_SEARCH_H__*/
