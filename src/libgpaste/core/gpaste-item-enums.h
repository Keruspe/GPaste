/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_ITEM_ENUMS_H__
#define __G_PASTE_ITEM_ENUMS_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
    G_PASTE_ITEM_KIND_TEXT = 1,
    G_PASTE_ITEM_KIND_URIS,
    G_PASTE_ITEM_KIND_IMAGE,
    G_PASTE_ITEM_KIND_PASSWORD,
    G_PASTE_ITEM_KIND_INVALID = 0
} GPasteItemKind;

#define G_PASTE_TYPE_ITEM_KIND (g_paste_item_kind_get_type ())
GType g_paste_item_kind_get_type (void);

G_END_DECLS

#endif /*__G_PASTE_ITEM_ENUMS_H__*/
