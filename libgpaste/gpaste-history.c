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

#include "gpaste-history-private.h"

#include <glib/gi18n-lib.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>

#define G_PASTE_HISTORY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_HISTORY, GPasteHistoryPrivate))

#define HISTORY_FILE "history.xml"

G_DEFINE_TYPE (GPasteHistory, g_paste_history, G_TYPE_OBJECT)

struct _GPasteHistoryPrivate
{
    GPasteSettings *settings;
    GSList *history;
};

enum
{
    CHANGED,
    SELECTED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static GSList *
_g_paste_history_remove (GPasteHistory *self,
                         GSList        *link,
                         gboolean       remove_leftovers)
{
    GPasteItem *item = link->data;

    if (remove_leftovers && G_PASTE_IS_IMAGE_ITEM (item))
    {
        GFile *image = g_file_new_for_path (g_paste_item_get_value (item));
        g_file_delete (image,
                       NULL, /* cancellable */
                       NULL); /* error */
        g_object_unref (image);
    }

    g_object_unref (item);
    return g_slist_delete_link (self->priv->history,
                                link);
}

/**
 * g_paste_history_add:
 * @self: a GPasteHistory instance
 * @item: (transfer none): the GPasteItem to add
 *
 * Add a GPasteItem to the GPasteHistory
 *
 * Returns:
 */
void
g_paste_history_add (GPasteHistory *self,
                     GPasteItem    *item)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    GPasteHistoryPrivate *priv = self->priv;
    GSList *history = priv->history;

    if (history)
    {
        if (g_paste_item_equals (history->data, item))
            return;
        for (history = g_slist_next (history); history; history = g_slist_next (history))
        {
            if (g_paste_item_equals (history->data, item))
            {
                priv->history = _g_paste_history_remove (self, history, FALSE);
                break;
            }
        }
    }
    history = priv->history = g_slist_prepend (priv->history, g_object_ref (item));

    guint max_history_size = g_paste_settings_get_max_history_size (priv->settings);

    if (g_slist_length (history) > max_history_size)
    {
        for (guint i = 0; i < max_history_size - 1; ++i)
            history = g_slist_next (history);
        g_slist_free_full (g_slist_next (history),
                           g_object_unref);
        history->next = NULL;
    }

    g_signal_emit (self,
                   signals[CHANGED],
                   0); /* detail */
}

/**
 * g_paste_history_delete:
 * @self: a GPasteHistory instance
 * @index: the index of the GPasteItem to delete
 *
 * Delete a GPasteItem from the GPasteHistory
 *
 * Returns:
 */
void
g_paste_history_remove (GPasteHistory *self,
                        guint          index)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = self->priv;
    GSList *history = priv->history;

    g_return_if_fail (index < g_slist_length (history));

    for (guint i = 0; i < index; ++i)
        history = g_slist_next (history);
    priv->history = _g_paste_history_remove (self, history, TRUE);

    if (index == 0)
        g_paste_history_select (self, 0);

    g_signal_emit (self,
                   signals[CHANGED],
                   0); /* detail */
}

/**
 * g_paste_history_get_element_value:
 * @self: a GPasteHistory instance
 * @index: the index of the GPasteItem
 *
 * Get the value of a GPasteItem from the GPasteHistory
 *
 * Returns: the read-only value of the GPasteItem
 */
const gchar *
g_paste_history_get_element_value (GPasteHistory *self,
                                   guint          index)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GSList *history = self->priv->history;

    g_return_val_if_fail (index < g_slist_length (history), NULL);

    return g_paste_item_get_value (G_PASTE_ITEM (g_slist_nth_data (history, index)));
}

/**
 * g_paste_history_select:
 * @self: a GPasteHistory instance
 * @index: the index of the GPasteItem to select
 *
 * Select a GPasteItem from the GPasteHistory
 *
 * Returns:
 */
void
g_paste_history_select (GPasteHistory *self,
                        guint          index)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GSList *history = self->priv->history;

    g_return_if_fail (index < g_slist_length (history));

    g_signal_emit (self,
                   signals[SELECTED],
                   0, /* detail */
                   g_slist_nth_data (history, index));
}

/**
 * g_paste_history_empty:
 * @self: a GPasteHistory instance
 *
 * Empty the GPasteHistory
 *
 * Returns:
 */
void
g_paste_history_empty (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = self->priv;

    g_slist_free_full (priv->history,
                       g_object_unref);
    priv->history = NULL;

    g_signal_emit (self,
                   signals[CHANGED],
                   0); /* detail */
}

/**
 * g_paste_history_save:
 * @self: a GPasteHistory instance
 *
 * Save the GPasteHistory to the history file
 *
 * Returns:
 */
void
g_paste_history_save (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = self->priv;

    gboolean save_history = g_paste_settings_get_save_history (priv->settings);
    gchar *history_dir_path = g_build_filename (g_get_user_data_dir (), "gpaste", NULL);
    GFile *history_dir = g_file_new_for_path (history_dir_path);
    GError *error = NULL;

    if (!g_file_query_exists (history_dir,
                              NULL)) /* cancellable */
    {
        if (!save_history)
            goto out;
        g_file_make_directory_with_parents (history_dir,
                                            NULL, /* cancellable */
                                            &error);
        if (error != NULL)
        {
            g_error (_("Could not create history dir"));
            g_error_free (error);
            goto out;
        }
    }

    gchar *history_file_path = g_build_filename (history_dir_path, HISTORY_FILE, NULL);
    GFile *history_file = g_file_new_for_path (history_file_path);

    if (!save_history)
        g_file_delete (history_file,
                       NULL, /* cancellable*/
                       NULL); /* error */
    else
    {
        LIBXML_TEST_VERSION

        xmlTextWriterPtr writer = xmlNewTextWriterFilename (history_file_path, 0);

        xmlTextWriterSetIndent (writer, TRUE);
        xmlTextWriterSetIndentString (writer, "  ");

        xmlTextWriterStartDocument (writer, "1.0", "UTF-8", NULL);
        xmlTextWriterStartElement (writer, BAD_CAST "history");

        for (GSList *history = priv->history; history; history = g_slist_next (history))
        {
            GPasteItem *item = history->data;

            xmlTextWriterStartElement (writer, BAD_CAST "item");
            xmlTextWriterWriteAttribute (writer, BAD_CAST "kind", BAD_CAST g_paste_item_get_kind (item));
            if (G_PASTE_IS_IMAGE_ITEM (item))
                xmlTextWriterWriteFormatAttribute (writer, BAD_CAST "date", "%ld",
                                                   g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (G_PASTE_IMAGE_ITEM (item))));
            xmlTextWriterStartCDATA (writer);
            xmlTextWriterWriteString (writer, BAD_CAST g_paste_item_get_value (item));
            xmlTextWriterEndCDATA (writer);
            xmlTextWriterEndElement (writer);
        }

        xmlTextWriterEndElement (writer);
        xmlTextWriterEndDocument (writer);

        xmlTextWriterFlush (writer);
        xmlFreeTextWriter (writer);
    }

    g_object_unref (history_file);
    g_free (history_file_path);
out:
    g_object_unref (history_dir);
    g_free (history_dir_path);
}

/**
 * g_paste_history_load:
 * @self: a GPasteHistory instance
 *
 * Load the GPasteHistory from the history file
 *
 * Returns:
 */
void
g_paste_history_load (GPasteHistory *self)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    
    gchar *history_file_path = g_build_filename (g_get_user_data_dir (), "gpaste", HISTORY_FILE, NULL);
    GFile *history_file = g_file_new_for_path (history_file_path);

    if (g_file_query_exists (history_file,
                              NULL)) /* cancellable */
    {
        LIBXML_TEST_VERSION

        xmlTextReaderPtr reader = xmlNewTextReaderFilename (history_file_path);
        GPasteHistoryPrivate *priv = self->priv;
        guint max_history_size = g_paste_settings_get_max_history_size (priv->settings);

        for (guint i = 0; i < max_history_size && xmlTextReaderRead (reader) == 1; ++i)
        {
            if (xmlTextReaderNodeType (reader) != 1)
                continue;
            const gchar *name = xmlTextReaderConstName (reader);
            if (!name || g_strcmp0 (name, "item") != 0)
                continue;

            gchar *kind = xmlTextReaderGetAttribute (reader, "kind");
            gchar *date = xmlTextReaderGetAttribute (reader, "date");
            gchar *value = xmlTextReaderReadString (reader);

            if (g_strcmp0 (kind, "Text") == 0)
                priv->history = g_slist_append (priv->history, g_paste_text_item_new (value));
            else if (g_strcmp0 (kind, "Uris") == 0)
                priv->history = g_slist_append (priv->history, g_paste_uris_item_new (value));
            else if (g_strcmp0 (kind, "Image") == 0)
            {
                GDateTime *date_time = g_date_time_new_from_unix_local (g_ascii_strtoll (date,
                                                                                         NULL, /* end */
                                                                                         0)); /* base */
                GPasteImageItem *item = g_paste_image_item_new_from_file (value, date_time);

                if (g_paste_image_item_get_image (item) != NULL)
                    priv->history = g_slist_append (priv->history, item);
                else
                    g_object_unref (item);
                g_date_time_unref (date_time);
            }

            g_free (value);
            g_free (date);
            g_free (kind);
        }

        xmlFreeTextReader (reader);
    }

    g_object_unref (history_file);
    g_free (history_file_path);
}

static void
g_paste_history_dispose (GObject *object)
{
    GPasteHistoryPrivate *priv = G_PASTE_HISTORY (object)->priv;

    g_object_unref (priv->settings);

    G_OBJECT_CLASS (g_paste_history_parent_class)->dispose (object);
}

static void
g_paste_history_finalize (GObject *object)
{
    GPasteHistoryPrivate *priv = G_PASTE_HISTORY (object)->priv;

    g_slist_free_full (priv->history,
                       g_object_unref);

    G_OBJECT_CLASS (g_paste_history_parent_class)->finalize (object);
}

static void
g_paste_history_class_init (GPasteHistoryClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteHistoryPrivate));

    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_history_dispose;
    object_class->finalize = g_paste_history_finalize;

    signals[CHANGED] = g_signal_new ("changed",
                                     G_PASTE_TYPE_HISTORY,
                                     G_SIGNAL_RUN_LAST,
                                     0, /* class offset */
                                     NULL, /* accumulator */
                                     NULL, /* accumulator data */
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE,
                                     0); /* number of params */
    signals[SELECTED] = g_signal_new ("selected",
                                      G_PASTE_TYPE_HISTORY,
                                      G_SIGNAL_RUN_LAST,
                                      0, /* class offset */
                                      NULL, /* accumulator */
                                      NULL, /* accumulator data */
                                      g_cclosure_marshal_VOID__OBJECT,
                                      G_TYPE_NONE,
                                      1, /* number of params */
                                      G_PASTE_TYPE_ITEM);
}

static gboolean
g_paste_history_self_changed (GPasteHistory *self,
                              gpointer       user_data)
{
    /* Silence warning */
    user_data = user_data;

    g_paste_history_save (self);

    return TRUE;
}

static void
g_paste_history_init (GPasteHistory *self)
{
    self->priv = G_PASTE_HISTORY_GET_PRIVATE (self);

    g_signal_connect (G_OBJECT (self),
                      "changed",
                      G_CALLBACK (g_paste_history_self_changed),
                      NULL); /* user data */
}

/**
 * g_paste_history_get_history
 * @self: a GPasteHistory instance
 *
 * Get the inner history of a GPasteHistory
 *
 * Returns: (element-type GPasteItem): The inner history
 */
const GSList *
g_paste_history_get_history (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return self->priv->history;
}

/**
 * g_paste_history_new:
 * @settings: (transfer none): a GPasteSettings instance
 *
 * Create a new instance of GPasteHistory
 *
 * Returns: a newly allocated GPasteHistory
 *          free it with g_object_unref
 */
GPasteHistory *
g_paste_history_new (GPasteSettings *settings)
{
    GPasteHistory *self = g_object_new (G_PASTE_TYPE_HISTORY, NULL);
    GPasteHistoryPrivate *priv = self->priv;

    priv->settings = g_object_ref (settings);
    priv->history = NULL;

    return self;
}
