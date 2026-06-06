// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-ui-item-skeleton.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_ITEM (g_paste_ui_item_get_type ())

G_PASTE_FINAL_TYPE (UiItem, ui_item, UI_ITEM, GPasteUiItemSkeleton)

gboolean  g_paste_ui_item_activate (GPasteUiItem *self);
void      g_paste_ui_item_refresh  (GPasteUiItem *self);

void      g_paste_ui_item_set_index (GPasteUiItem *self,
                                     guint64       index);

void      g_paste_ui_item_set_uuid (GPasteUiItem *self,
                                    const gchar  *uuid);

GtkWidget *g_paste_ui_item_new (GPasteClient   *client,
                                GPasteSettings *settings,
                                GtkWindow      *rootwin,
                                guint64         index);

G_END_DECLS

