/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SEARCH_BAR_H__
#define __G_PASTE_UI_SEARCH_BAR_H__

#include <gpaste/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

GtkSearchEntry *g_paste_ui_search_bar_get_entry (GtkSearchBar *self);

GtkWidget *g_paste_ui_search_bar_new (void);

G_END_DECLS

#endif /*__G_PASTE_UI_SEARCH_H__*/
