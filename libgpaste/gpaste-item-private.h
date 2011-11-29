/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __G_PASTE_ITEM_PRIVATE_H__
#define __G_PASTE_ITEM_PRIVATE_H__

#include "gpaste-item.h"

/* GPaste Item */

/*< abstract >*/
struct _GPasteItem
{
    GObject parent_instance;

    /*< protected >*/
    GPasteItemPrivate *priv;
};

struct _GPasteItemClass
{
    GObjectClass parent_class;

    /*< virtual >*/
    gboolean (*equals) (GPasteItem *self, GPasteItem *other);

    /*< pure virtual >*/
    gboolean (*has_value) (GPasteItem *self);
    const gchar *(*get_kind) (GPasteItem *self);
};

/* GPaste TextItem */

struct _GPasteTextItem
{
    GPasteItem parent_instance;
};

struct _GPasteTextItemClass
{
    GPasteItemClass parent_class;
};

/* GPaste UrisItem */

struct _GPasteUrisItem
{
    GPasteTextItem parent_instance;

    /*< private >*/
    GPasteUrisItemPrivate *priv;
};

struct _GPasteUrisItemClass
{
    GPasteTextItemClass parent_class;
};

/* GPaste ImageItem */

struct _GPasteImageItem
{
    GPasteItem parent_instance;

    /*<private>*/
    GPasteImageItemPrivate *priv;
};

struct _GPasteImageItemClass
{
    GPasteItemClass parent_class;
};

#endif /*__G_PASTE_ITEM_PRIVATE_H__*/
