/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-clipboard.h>
#include <gpaste-image-item.h>
#include <gpaste-uris-item.h>

#include "gpaste-clipboard-common.h"

G_PASTE_VISIBLE void
g_paste_clipboard_get_clipboard_data (GtkClipboard     *clipboard G_GNUC_UNUSED,
                                      GtkSelectionData *selection_data,
                                      guint             info G_GNUC_UNUSED,
                                      gpointer          user_data_or_owner)
{
    g_return_if_fail (G_PASTE_IS_ITEM (user_data_or_owner));

    GPasteItem *item = G_PASTE_ITEM (user_data_or_owner);

    GdkAtom targets[1] = { gtk_selection_data_get_target (selection_data) };

    /* The content is requested as text */
    if (gtk_targets_include_text (targets, 1))
        gtk_selection_data_set_text (selection_data, g_paste_item_get_value (item), -1);
    else if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        if (gtk_targets_include_image (targets, 1, TRUE))
            gtk_selection_data_set_pixbuf (selection_data, g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item)));
    }
    /* The content is requested as uris */
    else
    {
        g_return_if_fail (G_PASTE_IS_URIS_ITEM (item));

        const gchar * const *uris = g_paste_uris_item_get_uris (G_PASTE_URIS_ITEM (item));

        if (gtk_targets_include_uri (targets, 1))
            gtk_selection_data_set_uris (selection_data, (GStrv) uris);
        /* The content is requested as special gnome-copied-files by nautilus */
        else
        {
            G_PASTE_CLEANUP_STRING_FREE GString *copy_string = g_string_new ("copy");
            guint length = g_strv_length ((GStrv) uris);

            for (guint i = 0; i < length; ++i)
            {
                g_string_append (g_string_append (copy_string,
                                                  "\n"),
                                 uris[i]);
            }

            gchar *str = copy_string->str;
            length = copy_string->len + 1;
            G_PASTE_CLEANUP_FREE guchar *copy_files_data = g_new (guchar, length);
            for (guint i = 0; i < length; ++i)
                copy_files_data[i] = (guchar) str[i];
            gtk_selection_data_set (selection_data, g_paste_clipboard_copy_files_target, 8, copy_files_data, length);
        }
    }
}
