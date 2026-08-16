// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-item-enums.h>
#include <gpaste-daemon/gpaste-binary-data.h>

G_BEGIN_DECLS

typedef enum {
    G_PASTE_ITEM_STATE_IDLE,
    G_PASTE_ITEM_STATE_ACTIVE
} GPasteItemState;

#define G_PASTE_TYPE_ITEM (g_paste_item_get_type ())

G_PASTE_DERIVABLE_TYPE (Item, item, ITEM, GObject)

struct _GPasteItemClass
{
    GObjectClass parent_class;

    /*< virtual >*/
    const gchar * (*get_value) (GPasteItem *self);
    gboolean      (*equals)    (GPasteItem *self,
                                GPasteItem *other);
    void          (*set_state) (GPasteItem     *self,
                                GPasteItemState state);
    gboolean      (*secure)    (GPasteItem *self);

    /*< pure virtual >*/
    GPasteItemKind (*get_kind) (GPasteItem *self);
};

const gchar  *g_paste_item_get_uuid           (GPasteItem *self);
const gchar  *g_paste_item_get_value          (GPasteItem *self);
const gchar  *g_paste_item_get_real_value     (GPasteItem *self);
const GSList *g_paste_item_get_special_values (GPasteItem *self);
const gchar  *g_paste_item_get_display_string (GPasteItem *self);
gboolean      g_paste_item_equals             (GPasteItem *self,
                                               GPasteItem *other);
GPasteItemKind g_paste_item_get_kind          (GPasteItem *self);
gboolean      g_paste_item_is_favourite       (GPasteItem *self);
guint64       g_paste_item_get_size           (GPasteItem *self);

void g_paste_item_set_favourite (GPasteItem *self,
                                 gboolean    favourite);

void g_paste_item_set_state (GPasteItem     *self,
                             GPasteItemState state);
void g_paste_item_set_uuid  (GPasteItem     *self,
                             const gchar    *uuid);
void g_paste_item_set_value (GPasteItem     *self,
                             const gchar    *value);

void g_paste_item_set_display_string (GPasteItem       *self,
                                      gchar            *display_string);
void g_paste_item_add_special_value  (GPasteItem       *self,
                                      GPasteBinaryData *binary_data);

void g_paste_item_set_size    (GPasteItem *self,
                               guint64     size);
void g_paste_item_add_size    (GPasteItem *self,
                               guint64     size);
void g_paste_item_remove_size (GPasteItem *self,
                               guint64     size);

GPasteItem *g_paste_item_new (GType        type,
                              const gchar *value);
G_END_DECLS
