// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-macros.h>
#include <gpaste-3/gpaste-item-enums.h>

G_PASTE_VISIBLE G_DEFINE_ENUM_TYPE (GPasteItemKind, g_paste_item_kind,
                                    G_DEFINE_ENUM_VALUE (G_PASTE_ITEM_KIND_TEXT,     "Text"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_ITEM_KIND_URIS,     "Uris"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_ITEM_KIND_IMAGE,    "Image"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_ITEM_KIND_PASSWORD, "Password"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_ITEM_KIND_COLOR,    "Color"))

/**
 * g_paste_item_kind_to_string:
 * @kind: a #GPasteItemKind
 *
 * Get the serialized form of @kind: the nick the storage backends write.
 *
 * Returns: (nullable): a read-only string, or %NULL for %G_PASTE_ITEM_KIND_INVALID
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_kind_to_string (GPasteItemKind kind)
{
    const GEnumValue *value = g_enum_get_value (g_type_class_get (G_PASTE_TYPE_ITEM_KIND), kind);

    return (value) ? value->value_nick : NULL;
}

/**
 * g_paste_item_kind_from_string:
 * @kind: the serialized form of a #GPasteItemKind
 *
 * Parse back what g_paste_item_kind_to_string() wrote.
 *
 * Returns: the matching #GPasteItemKind, or %G_PASTE_ITEM_KIND_INVALID
 */
G_PASTE_VISIBLE GPasteItemKind
g_paste_item_kind_from_string (const gchar *kind)
{
    if (!kind)
        return G_PASTE_ITEM_KIND_INVALID;

    const GEnumValue *value = g_enum_get_value_by_nick (g_type_class_get (G_PASTE_TYPE_ITEM_KIND), kind);

    return (value) ? (GPasteItemKind) value->value : G_PASTE_ITEM_KIND_INVALID;
}
