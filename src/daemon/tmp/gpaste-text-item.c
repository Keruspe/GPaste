/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-text-item.h>

G_PASTE_DEFINE_TYPE (TextItem, text_item, G_PASTE_TYPE_ITEM)

static gboolean
g_paste_text_item_equals (const GPasteItem *self,
                          const GPasteItem *other)
{
    return (_G_PASTE_IS_TEXT_ITEM (other) &&
            G_PASTE_ITEM_CLASS (g_paste_text_item_parent_class)->equals (self, other));
}

static const gchar *
g_paste_text_item_get_kind (const GPasteItem *self G_GNUC_UNUSED)
{
    return "Text";
}

static void
g_paste_text_item_class_init (GPasteTextItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->equals = g_paste_text_item_equals;
    item_class->get_kind = g_paste_text_item_get_kind;
}

static void
g_paste_text_item_init (GPasteTextItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_text_item_new:
 * @text: the content of the desired #GPasteTextItem
 *
 * Create a new instance of #GPasteTextItem
 *
 * Returns: a newly allocated #GPasteTextItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_text_item_new (const gchar *text)
{
    g_return_val_if_fail (text, NULL);
    g_return_val_if_fail (g_utf8_validate (text, -1, NULL), NULL);

    return g_paste_item_new (G_PASTE_TYPE_TEXT_ITEM, text);
}
