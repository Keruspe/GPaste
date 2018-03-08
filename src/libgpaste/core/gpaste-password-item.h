/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_PASSWORD_ITEM_H__
#define __G_PASTE_PASSWORD_ITEM_H__

#include <gpaste-text-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_PASSWORD_ITEM (g_paste_password_item_get_type ())

G_PASTE_FINAL_TYPE (PasswordItem, password_item, PASSWORD_ITEM, GPasteTextItem)

const gchar *g_paste_password_item_get_name (const GPastePasswordItem *self);

void g_paste_password_item_set_name (GPastePasswordItem *self,
                                     const char         *name);

GPasteItem *g_paste_password_item_new (const gchar *name,
                                       const gchar *password);

G_END_DECLS

#endif /*__G_PASTE_PASSWORD_ITEM_H__*/
