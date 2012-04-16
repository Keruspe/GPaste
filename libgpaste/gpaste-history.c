/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
    GSList         *history;
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
                         GSList        *elem,
                         gboolean       remove_leftovers)
{
    GPasteItem *item = elem->data;

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
                                elem);
}

/**
 * g_paste_history_add:
 * @self: a #GPasteHistory instance
 * @item: (transfer none): the #GPasteItem to add
 *
 * Add a #GPasteItem to the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
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
    gboolean fifo = g_paste_settings_get_fifo (priv->settings);
    history = priv->history = fifo ?
        g_slist_append (priv->history, g_object_ref (item)) :
        g_slist_prepend (priv->history, g_object_ref (item));

    guint max_history_size = g_paste_settings_get_max_history_size (priv->settings);

    if (g_slist_length (history) > max_history_size)
    {
        if (fifo) {
            /* start the shortened list at the right place */
            priv->history = g_slist_nth(history, g_slist_length(history) - max_history_size);
            /* terminate the original list so that it can be freed (below) */
            g_slist_nth(history, g_slist_length(history) - max_history_size - 1)->next = NULL;
        }
        else {
             for (guint i = 0; i < max_history_size - 1; ++i)
                 history = g_slist_next (history);
         }
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
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem to delete
 *
 * Delete a #GPasteItem from the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_remove (GPasteHistory *self,
                        guint          pos)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = self->priv;
    GSList *history = priv->history;

    g_return_if_fail (pos < g_slist_length (history));

    for (guint i = 0; i < pos; ++i)
        history = g_slist_next (history);
    priv->history = _g_paste_history_remove (self, history, TRUE);

    if (pos == 0)
        g_paste_history_select (self, 0);

    g_signal_emit (self,
                   signals[CHANGED],
                   0); /* detail */
}

/**
 * g_paste_history_get_element_value:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get the value of a #GPasteItem from the #GPasteHistory
 *
 * Returns: the read-only value of the #GPasteItem
 */
G_PASTE_VISIBLE const gchar *
g_paste_history_get_element_value (GPasteHistory *self,
                                   guint          pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GSList *history = self->priv->history;

    g_return_val_if_fail (pos < g_slist_length (history), NULL);

    return g_paste_item_get_value (G_PASTE_ITEM (g_slist_nth_data (history, pos)));
}

/**
 * g_paste_history_select:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem to select
 *
 * Select a #GPasteItem from the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_select (GPasteHistory *self,
                        guint          pos)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GSList *history = self->priv->history;

    g_return_if_fail (pos < g_slist_length (history));

    g_signal_emit (self,
                   signals[SELECTED],
                   0, /* detail */
                   g_slist_nth_data (history, pos));
}

/**
 * g_paste_history_empty:
 * @self: a #GPasteHistory instance
 *
 * Empty the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
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

static gchar *
g_paste_history_replace (const gchar *text,
                         const gchar *pattern,
                         const gchar *substitution)
{
    gchar *regex_string = g_regex_escape_string (pattern, -1);
    GRegex *regex = g_regex_new (regex_string,
                                 0, /* Compile options */
                                 0, /* Match options */
                                 NULL); /* Error */
    gchar *encoded_text = g_regex_replace_literal (regex,
                                                   text,
                                                   (gssize) -1,
                                                   0, /* Start position */
                                                   substitution,
                                                   0, /* Match options */
                                                   NULL); /* Error */
    g_regex_unref (regex);
    g_free (regex_string);

    return encoded_text;
}

static gchar *
g_paste_history_encode (const gchar *text)
{
    gchar *_encoded_text = g_paste_history_replace (text, "&", "&amp;");
    gchar *encoded_text = g_paste_history_replace (_encoded_text, ">", "&gt;");

    g_free (_encoded_text);

    return encoded_text;
}

static gchar *
g_paste_history_decode (const gchar *text)
{
    gchar *_decoded_text = g_paste_history_replace (text, "&gt;", ">");
    gchar *decoded_text = g_paste_history_replace (_decoded_text, "&amp;", "&");

    g_free (_decoded_text);

    return decoded_text;
}

/**
 * g_paste_history_save:
 * @self: a #GPasteHistory instance
 *
 * Save the #GPasteHistory to the history file
 *
 * Returns:
 */
G_PASTE_VISIBLE void
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
        xmlTextWriterSetIndentString (writer, BAD_CAST "  ");

        xmlTextWriterStartDocument (writer, "1.0", "UTF-8", NULL);
        xmlTextWriterStartElement (writer, BAD_CAST "history");
        xmlTextWriterWriteAttribute (writer, BAD_CAST "version", BAD_CAST "1.0");

        for (GSList *history = priv->history; history; history = g_slist_next (history))
        {
            GPasteItem *item = history->data;

            xmlTextWriterStartElement (writer, BAD_CAST "item");
            xmlTextWriterWriteAttribute (writer, BAD_CAST "kind", BAD_CAST g_paste_item_get_kind (item));
            if (G_PASTE_IS_IMAGE_ITEM (item))
                xmlTextWriterWriteFormatAttribute (writer, BAD_CAST "date", "%ld",
                                                   g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (G_PASTE_IMAGE_ITEM (item))));
            xmlTextWriterStartCDATA (writer);

            gchar *data = g_paste_history_encode (g_paste_item_get_value (item));
            xmlTextWriterWriteString (writer, BAD_CAST data);
            g_free (data);

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
 * @self: a #GPasteHistory instance
 *
 * Load the #GPasteHistory from the history file
 *
 * Returns:
 */
G_PASTE_VISIBLE void
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

        for (guint i = 0; i < max_history_size && xmlTextReaderRead (reader) == 1;)
        {
            if (xmlTextReaderNodeType (reader) != 1)
                continue;
            const gchar *name = (const gchar *) xmlTextReaderConstName (reader);
            if (!name || g_strcmp0 (name, "item") != 0)
                continue;

            ++i;

            gchar *kind = (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "kind");
            gchar *date = (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "date");
            gchar *raw_value = (gchar *) xmlTextReaderReadString (reader);
            gchar *value = g_paste_history_decode (raw_value);

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

                if (item != NULL)
                    priv->history = g_slist_append (priv->history, item);

                g_date_time_unref (date_time);
            }

            g_free (raw_value);
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
                              gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_history_save (self);

    return TRUE;
}

static void
g_paste_history_init (GPasteHistory *self)
{
    GPasteHistoryPrivate *priv = self->priv = G_PASTE_HISTORY_GET_PRIVATE (self);

    priv->history = NULL;

    g_signal_connect (G_OBJECT (self),
                      "changed",
                      G_CALLBACK (g_paste_history_self_changed),
                      NULL); /* user data */
}

/**
 * g_paste_history_get_history:
 * @self: a #GPasteHistory instance
 *
 * Get the inner history of a #GPasteHistory
 *
 * Returns: (element-type GPasteItem) (transfer none): The inner history
 */
G_PASTE_VISIBLE GSList *
g_paste_history_get_history (GPasteHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return self->priv->history;
}

/**
 * g_paste_history_new:
 * @settings: (transfer none): a #GPasteSettings instance
 *
 * Create a new instance of #GPasteHistory
 *
 * Returns: a newly allocated #GPasteHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteHistory *
g_paste_history_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    
    GPasteHistory *self = g_object_new (G_PASTE_TYPE_HISTORY, NULL);

    self->priv->settings = g_object_ref (settings);

    return self;
}
