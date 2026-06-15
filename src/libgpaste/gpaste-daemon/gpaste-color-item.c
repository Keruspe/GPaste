// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-color-item.h>

struct _GPasteColorItem
{
    GPasteItem parent_instance;
};

typedef struct
{
    GdkRGBA rgba;
} GPasteColorItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ColorItem, color_item, G_PASTE_TYPE_ITEM)

/**
 * g_paste_color_item_get_rgba:
 * @self: a #GPasteColorItem instance
 *
 * Get the #GdkRGBA stored in the #GPasteColorItem
 *
 * Returns: (transfer none): read-only #GdkRGBA
 */
G_PASTE_VISIBLE const GdkRGBA *
g_paste_color_item_get_rgba (const GPasteColorItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_COLOR_ITEM (self), NULL);

    const GPasteColorItemPrivate *priv = _g_paste_color_item_get_instance_private (self);

    return &priv->rgba;
}

static gboolean
g_paste_color_item_equals (const GPasteItem *self,
                           const GPasteItem *other)
{
    return (_G_PASTE_IS_COLOR_ITEM (other) &&
            G_PASTE_ITEM_CLASS (g_paste_color_item_parent_class)->equals (self, other));
}

static const gchar *
g_paste_color_item_get_kind (const GPasteItem *self G_GNUC_UNUSED)
{
    return "Color";
}

static void
g_paste_color_item_class_init (GPasteColorItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->equals = g_paste_color_item_equals;
    item_class->get_kind = g_paste_color_item_get_kind;
}

static void
g_paste_color_item_init (GPasteColorItem *self G_GNUC_UNUSED)
{
}

static GPasteItem *
_g_paste_color_item_new (const gchar   *str,
                         const GdkRGBA *rgba)
{
    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_COLOR_ITEM, str);
    GPasteColorItemPrivate *priv = g_paste_color_item_get_instance_private (G_PASTE_COLOR_ITEM (self));

    priv->rgba = *rgba;

    g_autofree gchar *display = g_strconcat (_("[Color]"), " ", str, NULL);
    g_paste_item_set_display_string (self, g_steal_pointer (&display));

    return self;
}

/**
 * g_paste_color_item_new:
 * @rgba: (transfer none): a #GdkRGBA from the clipboard
 *
 * Create a new instance of #GPasteColorItem
 *
 * Returns: a newly allocated #GPasteColorItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_color_item_new (const GdkRGBA *rgba)
{
    g_return_val_if_fail (rgba != NULL, NULL);

    g_autofree gchar *str = gdk_rgba_to_string (rgba);

    return _g_paste_color_item_new (str, rgba);
}

/**
 * g_paste_color_item_new_from_str:
 * @str: a string representation of the color
 *
 * Create a new instance of #GPasteColorItem from its string representation
 *
 * Returns: a newly allocated #GPasteColorItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_color_item_new_from_str (const gchar *str)
{
    g_return_val_if_fail (str != NULL, NULL);

    GdkRGBA rgba;

    if (!gdk_rgba_parse (&rgba, str))
        return NULL;

    return _g_paste_color_item_new (str, &rgba);
}
