/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_TEXT_ITEM_H__
#define __G_PASTE_TEXT_ITEM_H__

#include <gpaste-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_TEXT_ITEM (g_paste_text_item_get_type ())

G_PASTE_DERIVABLE_TYPE (TextItem, text_item, TEXT_ITEM, GPasteItem)

struct _GPasteTextItemClass
{
    GPasteItemClass parent_class;
};

GPasteItem *g_paste_text_item_new (const gchar *text);

G_END_DECLS

#endif /*__G_PASTE_TEXT_ITEM_H__*/
