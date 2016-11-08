/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_ITEM_H__
#define __G_PASTE_ITEM_H__

#include <gpaste-special-atom.h>

G_BEGIN_DECLS

typedef enum {
    G_PASTE_ITEM_STATE_IDLE,
    G_PASTE_ITEM_STATE_ACTIVE
} GPasteItemState;

typedef struct {
    GPasteSpecialAtom mime;
    gchar            *data;
} GPasteSpecialValue;

#define G_PASTE_TYPE_ITEM (g_paste_item_get_type ())

G_PASTE_DERIVABLE_TYPE (Item, item, ITEM, GObject)

struct _GPasteItemClass
{
    GObjectClass parent_class;

    /*< virtual >*/
    const gchar * (*get_value) (const GPasteItem *self);
    gboolean      (*equals)    (const GPasteItem *self,
                                const GPasteItem *other);
    void          (*set_state) (GPasteItem     *self,
                                GPasteItemState state);
    gboolean      (*secure)    (const GPasteItem *self);

    /*< pure virtual >*/
    const gchar *(*get_kind) (const GPasteItem *self);
};

const gchar  *g_paste_item_get_uuid           (const GPasteItem *self);
const gchar  *g_paste_item_get_value          (const GPasteItem *self);
const gchar  *g_paste_item_get_real_value     (const GPasteItem *self);
const GSList *g_paste_item_get_special_values (const GPasteItem *self);
const gchar  *g_paste_item_get_special_value  (const GPasteItem *self,
                                               GPasteSpecialAtom atom);
const gchar  *g_paste_item_get_display_string (const GPasteItem *self);
gboolean      g_paste_item_equals             (const GPasteItem *self,
                                               const GPasteItem *other);
const gchar  *g_paste_item_get_kind           (const GPasteItem *self);
guint64       g_paste_item_get_size           (const GPasteItem *self);

void g_paste_item_set_state (GPasteItem     *self,
                             GPasteItemState state);
void g_paste_item_set_uuid  (GPasteItem     *self,
                             const gchar    *uuid);

void g_paste_item_set_display_string (GPasteItem               *self,
                                      const gchar              *display_string);
void g_paste_item_add_special_value  (GPasteItem               *self,
                                      const GPasteSpecialValue *special_value);

void g_paste_item_set_size    (GPasteItem *self,
                               guint64     size);
void g_paste_item_add_size    (GPasteItem *self,
                               guint64     size);
void g_paste_item_remove_size (GPasteItem *self,
                               guint64     size);

GPasteItem *g_paste_item_new (GType        type,
                              const gchar *value);
G_END_DECLS

#endif /*__G_PASTE_ITEM_H__*/
