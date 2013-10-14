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

#include "gpaste-history-private.h"

#include <gpaste-image-item.h>
#include <gpaste-text-item.h>
#include <gpaste-uris-item.h>

#include <glib/gi18n-lib.h>

#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>

struct _GPasteHistoryPrivate
{
    GPasteSettings *settings;
    GSList         *history;
    gsize           size;

    /* Note: we never track the first (active) item here */
    guint32         biggest_index;
    gsize           biggest_size;

    gulong          changed_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteHistory, g_paste_history, G_TYPE_OBJECT)

enum
{
    CHANGED,
    SELECTED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
g_paste_history_private_elect_new_biggest (GPasteHistoryPrivate *priv)
{
    priv->biggest_index = 0;
    priv->biggest_size = 0;

    GSList *history = priv->history;

    if (history)
    {
        guint32 index = 1;

        for (history = g_slist_next (history); history; history = g_slist_next (history), ++index)
        {
            GPasteItem *item = history->data;
            gsize size = g_paste_item_get_size (item);

            if (size > priv->biggest_size)
            {
                priv->biggest_index = index;
                priv->biggest_size = size;
            }
        }
    }
}

static GSList *
g_paste_history_private_remove (GPasteHistoryPrivate *priv,
                                GSList               *elem,
                                gboolean              remove_leftovers)
{
    GPasteItem *item = elem->data;

    priv->size -= g_paste_item_get_size (item);

    if (remove_leftovers)
    {
        if (G_PASTE_IS_IMAGE_ITEM (item))
        {
            GFile *image = g_file_new_for_path (g_paste_item_get_value (item));
            g_file_delete (image,
                           NULL, /* cancellable */
                           NULL); /* error */
            g_object_unref (image);
        }
        g_object_unref (item);
    }
    return g_slist_delete_link (elem, elem);
}

static void
g_paste_history_changed (GPasteHistory *self)
{
    g_paste_history_save (self);

    g_signal_emit (self,
                   signals[CHANGED],
                   0, /* detail */
                   NULL);
}

static void
g_paste_history_selected (GPasteHistory *self,
                          GPasteItem    *item)
{
    g_signal_emit (self,
                   signals[SELECTED],
                   0, /* detail */
                   item,
                   NULL);
}

static void
g_paste_history_activate_first (GPasteHistory *self,
                                gboolean       select)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteItem *first = priv->history->data;

    priv->size -= g_paste_item_get_size (first);
    g_paste_item_set_state (first, G_PASTE_ITEM_STATE_ACTIVE);
    priv->size += g_paste_item_get_size (first);

    if (select)
        g_paste_history_selected (self, first);
}

static void
g_paste_history_private_check_memory_usage (GPasteHistoryPrivate *priv)
{
    gsize max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;

    while (priv->size > max_memory && !priv->biggest_index)
    {
        GSList *prev = g_slist_nth (priv->history, priv->biggest_index - 1);
        prev->next = g_paste_history_private_remove (priv, g_slist_next (prev), TRUE);
        g_paste_history_private_elect_new_biggest (priv);
    }
}

static void
g_paste_history_private_check_size (GPasteHistoryPrivate *priv)
{
    GSList *history = priv->history;
    guint32 max_history_size = g_paste_settings_get_max_history_size (priv->settings);
    guint length = g_slist_length (history);

    if (length > max_history_size)
    {
        GSList *previous = g_slist_nth (history, max_history_size - 1);
        history = g_slist_next (previous);
        previous->next = NULL;

        for (GSList *_history = history; _history; _history = g_slist_next (_history))
            priv->size -= g_paste_item_get_size (_history->data);
        g_slist_free_full (history,
                           g_object_unref);
    }
}

/**
 * g_paste_history_add:
 * @self: a #GPasteHistory instance
 * @item: (transfer full): the #GPasteItem to add
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    gsize max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;

    g_return_if_fail (g_paste_item_get_size (item) < max_memory);

    GSList *history = priv->history;
    gboolean election_needed = FALSE;

    if (history)
    {
        GPasteItem *old_first = history->data;

        if (g_paste_item_equals (old_first, item))
            return;

        /* size may change when state is idle */
        priv->size -= g_paste_item_get_size (old_first);
        g_paste_item_set_state (old_first, G_PASTE_ITEM_STATE_IDLE);

        gsize size = g_paste_item_get_size (old_first);

        priv->size += size;

        if (size >= priv->biggest_size)
        {
            priv->biggest_index = 0; /* Current 0, will become 1 */
            priv->biggest_size = size;
        }

        GSList *prev = history;
        guint32 index = 1;
        for (history = g_slist_next (history); history; prev = history, history = g_slist_next (history), ++index)
        {
            if (g_paste_item_equals (history->data, item))
            {
                prev->next = g_paste_history_private_remove (priv, history, FALSE);
                if (index == priv->biggest_index)
                    election_needed = TRUE;
                break;
            }
        }

        ++priv->biggest_index;
    }

    priv->history = g_slist_prepend (priv->history, item);

    g_paste_history_activate_first (self, FALSE);
    priv->size += g_paste_item_get_size (item);

    g_paste_history_private_check_size (priv);

    if (election_needed)
        g_paste_history_private_elect_new_biggest (priv);

    g_paste_history_private_check_memory_usage (priv);
    g_paste_history_changed (self);
}

/**
 * g_paste_history_remove:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem to delete
 *
 * Delete a #GPasteItem from the #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_remove (GPasteHistory *self,
                        guint32        pos)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GSList *history = priv->history;

    g_return_if_fail (pos < g_slist_length (history));

    if (pos)
    {
        GSList *prev = g_slist_nth (history, pos - 1);
        prev->next = g_paste_history_private_remove (priv, g_slist_next (prev), TRUE);
    }
    else
    {
        priv->history = g_paste_history_private_remove (priv, history, TRUE);
        g_paste_history_activate_first (self, TRUE);
    }

    if (pos == priv->biggest_index)
        g_paste_history_private_elect_new_biggest (priv);
    else if (pos < priv->biggest_index)
        --priv->biggest_index;

    g_paste_history_changed (self);
}

static GPasteItem *
g_paste_history_private_get (GPasteHistoryPrivate *priv,
                             guint32               pos)
{
    GSList *history = priv->history;

    g_return_val_if_fail (pos < g_slist_length (history), NULL);

    return G_PASTE_ITEM (g_slist_nth_data (history, pos));
}

/**
 * g_paste_history_get:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 *
 * Returns: a read-only #GPasteItem
 */
G_PASTE_VISIBLE const GPasteItem *
g_paste_history_get (GPasteHistory *self,
                     guint32        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return g_paste_history_private_get (g_paste_history_get_instance_private (self), pos);
}

/**
 * g_paste_history_dup:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get a #GPasteItem from the #GPasteHistory
 * free it with g_object_unref
 *
 * Returns: (transfer full): a #GPasteItem
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_history_dup (GPasteHistory *self,
                     guint32        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    return g_object_ref (g_paste_history_private_get (g_paste_history_get_instance_private (self), pos));
}

/**
 * g_paste_history_get_value:
 * @self: a #GPasteHistory instance
 * @index: the index of the #GPasteItem
 *
 * Get the value of a #GPasteItem from the #GPasteHistory
 *
 * Returns: the read-only value of the #GPasteItem
 */
G_PASTE_VISIBLE const gchar *
g_paste_history_get_value (GPasteHistory *self,
                           guint32        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GPasteItem *item = g_paste_history_private_get (g_paste_history_get_instance_private (self), pos);

    g_return_val_if_fail (item != NULL, NULL);

    return g_paste_item_get_value (item);
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
                        guint32        pos)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GSList *history = priv->history;

    g_return_if_fail (pos < g_slist_length (history));

    GPasteItem *item = g_slist_nth_data (history, pos);

    g_paste_history_add (self, item);
    g_paste_history_selected (self, item);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_slist_free_full (priv->history,
                       g_object_unref);
    priv->history = NULL;
    priv->size = 0;

    g_paste_history_private_elect_new_biggest (priv);
    g_paste_history_changed (self);
}

static gchar *
g_paste_history_replace (const gchar *text,
                         const gchar *pattern,
                         const gchar *substitution)
{
    G_PASTE_CLEANUP_FREE gchar *regex_string = g_regex_escape_string (pattern, -1);
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

    return encoded_text;
}

static gchar *
g_paste_history_encode (const gchar *text)
{
    G_PASTE_CLEANUP_FREE gchar *_encoded_text = g_paste_history_replace (text, "&", "&amp;");
    return g_paste_history_replace (_encoded_text, ">", "&gt;");
}

static gchar *
g_paste_history_decode (const gchar *text)
{
    G_PASTE_CLEANUP_FREE gchar *_decoded_text = g_paste_history_replace (text, "&gt;", ">");
    return g_paste_history_replace (_decoded_text, "&amp;", "&");
}

static gchar *
g_paste_history_get_history_dir_path (void)
{
    return g_build_filename (g_get_user_data_dir (), "gpaste", NULL);
}

static GFile *
g_paste_history_get_history_dir (void)
{
    G_PASTE_CLEANUP_FREE gchar *history_dir_path = g_paste_history_get_history_dir_path ();
    return g_file_new_for_path (history_dir_path);
}

static gchar *
g_paste_history_get_history_file_path (GPasteSettings *settings)
{
    G_PASTE_CLEANUP_FREE gchar *history_dir_path = g_paste_history_get_history_dir_path ();
    G_PASTE_CLEANUP_FREE gchar *history_file_name = g_strconcat (g_paste_settings_get_history_name (settings), ".xml", NULL);
    //return g_build_filename (history_dir_path, history_file_name, NULL);
    return NULL;
}

static GFile *
g_paste_history_get_history_file (GPasteSettings *settings)
{
    G_PASTE_CLEANUP_FREE gchar *history_file_path = g_paste_history_get_history_file_path (settings);
    return g_file_new_for_path (history_file_path);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    GPasteSettings *settings = priv->settings;
    gboolean save_history = g_paste_settings_get_save_history (settings);
    GFile *history_dir = g_paste_history_get_history_dir ();
    G_PASTE_CLEANUP_FREE gchar *history_file_path = NULL;
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
            g_error ("%s: %s", _("Could not create history dir"), error->message);
            g_error_free (error);
            goto out;
        }
    }

    history_file_path = g_paste_history_get_history_file_path (settings);
    GFile *history_file = g_file_new_for_path (history_file_path);

    if (!save_history)
    {
        g_file_delete (history_file,
                       NULL, /* cancellable*/
                       NULL); /* error */
    }
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

            G_PASTE_CLEANUP_FREE gchar *data = g_paste_history_encode (g_paste_item_get_value (item));
            xmlTextWriterWriteString (writer, BAD_CAST data);

            xmlTextWriterEndCDATA (writer);
            xmlTextWriterEndElement (writer);
        }

        xmlTextWriterEndElement (writer);
        xmlTextWriterEndDocument (writer);

        xmlTextWriterFlush (writer);
        xmlFreeTextWriter (writer);
    }

    g_object_unref (history_file);
out:
    g_object_unref (history_dir);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteSettings *settings = priv->settings;

    g_slist_free_full (priv->history,
                       g_object_unref);
    priv->history = NULL;

    G_PASTE_CLEANUP_FREE gchar *history_file_path = g_paste_history_get_history_file_path (settings);
    GFile *history_file = g_file_new_for_path (history_file_path);

    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        LIBXML_TEST_VERSION

        xmlTextReaderPtr reader = xmlNewTextReaderFilename (history_file_path);
        guint32 max_history_size = g_paste_settings_get_max_history_size (settings);

        for (guint32 i = 0; i < max_history_size && xmlTextReaderRead (reader) == 1;)
        {
            if (xmlTextReaderNodeType (reader) != 1)
                continue;
            const gchar *name = (const gchar *) xmlTextReaderConstName (reader);
            if (!name || g_strcmp0 (name, "item"))
                continue;

            G_PASTE_CLEANUP_FREE gchar *kind = (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "kind");
            G_PASTE_CLEANUP_FREE gchar *date = (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "date");
            G_PASTE_CLEANUP_FREE gchar *raw_value = (gchar *) xmlTextReaderReadString (reader);
            G_PASTE_CLEANUP_FREE gchar *value = g_paste_history_decode (raw_value);
            GPasteItem *item = NULL;

            if (g_strcmp0 (kind, "Text") == 0)
                item = g_paste_text_item_new (value);
            else if (g_strcmp0 (kind, "Uris") == 0)
                item = g_paste_uris_item_new (value);
            else if (g_strcmp0 (kind, "Image") == 0)
            {
                if (g_paste_settings_get_images_support (settings))
                {
                    GDateTime *date_time = g_date_time_new_from_unix_local (g_ascii_strtoll (date,
                                                                                             NULL, /* end */
                                                                                             0)); /* base */
                    item = g_paste_image_item_new_from_file (value, date_time);
                    g_date_time_unref (date_time);
                }
                else
                {
                    GFile *img_file = g_file_new_for_path (value);

                    if (g_file_query_exists (img_file,
                                             NULL)) /* cancellable */
                    {
                        g_file_delete (img_file,
                                       NULL, /* cancellable */
                                       NULL); /* error */
                    }

                    g_object_unref (img_file);
                }
            }

            if (item)
            {
                priv->size += g_paste_item_get_size (item);
                priv->history = g_slist_append (priv->history, item);
            }

            ++i;
        }

        xmlFreeTextReader (reader);
    }
    else
    {
        /* Create the empty file to be listed as an available history */
        g_paste_history_save (self);
    }

    g_object_unref (history_file);

    if (priv->history)
    {
        g_paste_history_activate_first (self, TRUE);
        g_paste_history_private_elect_new_biggest (priv);
    }
}

/**
 * g_paste_history_switch:
 * @self: a #GPasteHistory instance
 * @name: the name of the new history
 *
 * Switch to a new history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_switch (GPasteHistory *self,
                        const gchar   *name)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (g_utf8_validate (name, -1, NULL));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    g_paste_settings_set_history_name (priv->settings, name);
    g_paste_history_load (self);
    g_paste_history_changed (self);
}

/**
 * g_paste_history_delete:
 * @self: a #GPasteHistory instance
 * @error: a #GError
 *
 * Delete the current #GPasteHistory
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_history_delete (GPasteHistory *self,
                        GError       **error)
{
    g_return_if_fail (G_PASTE_IS_HISTORY (self));

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    GFile *history_file = g_paste_history_get_history_file (priv->settings);

    g_paste_history_empty (self);
    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        g_file_delete (history_file,
                       NULL, /* cancellable */
                       error);
    }

    g_object_unref (history_file);
}

static void
g_paste_history_settings_changed (GPasteSettings *settings G_GNUC_UNUSED,
                                  const gchar    *key,
                                  gpointer        user_data)
{
    GPasteHistoryPrivate *priv = user_data;

    if (!g_strcmp0(key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        g_paste_history_private_check_size (priv);
    else if (!g_strcmp0 (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        g_paste_history_private_check_memory_usage (priv);
}

static void
g_paste_history_dispose (GObject *object)
{
    GPasteHistory *self = G_PASTE_HISTORY (object);
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->changed_signal);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_history_parent_class)->dispose (object);
}

static void
g_paste_history_finalize (GObject *object)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (G_PASTE_HISTORY (object));

    g_slist_free_full (priv->history,
                       g_object_unref);

    G_OBJECT_CLASS (g_paste_history_parent_class)->finalize (object);
}

static void
g_paste_history_class_init (GPasteHistoryClass *klass)
{
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

static void
g_paste_history_init (GPasteHistory *self)
{
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    priv->history = NULL;
    priv->size = 0;

    g_paste_history_private_elect_new_biggest (priv);
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

    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    return priv->history;
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
    GPasteHistoryPrivate *priv = g_paste_history_get_instance_private (self);

    priv->settings = g_object_ref (settings);
    priv->changed_signal = g_signal_connect (settings,
                                             "changed",
                                             G_CALLBACK (g_paste_history_settings_changed),
                                             priv);

    return self;
}

/**
 * g_paste_history_list:
 * @error: a #GError
 *
 * Get the list of available histories
 *
 * Returns: (transfer full): The list of history names
 *                           free it with g_strfreev
 */
G_PASTE_VISIBLE GStrv
g_paste_history_list (GError **error)
{
    GFile *history_dir = g_paste_history_get_history_dir ();
    GStrv ret = NULL;
    GFileEnumerator *histories = g_file_enumerate_children (history_dir,
                                                            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                                            G_FILE_QUERY_INFO_NONE,
                                                            NULL, /* cancellable */
                                                            error);
    if (error)
        goto dir_err;

    GArray *history_names = g_array_new (TRUE, /* zero-terminated */
                                         TRUE, /* clear */
                                         sizeof (gchar *));
    GFileInfo *history;

    while ((history = g_file_enumerator_next_file (histories,
                                                   NULL, /* cancellable */
                                                   error))) /* error */
    {
        if (error)
            goto file_err;

        const gchar *raw_name = g_file_info_get_display_name (history);

        if (g_str_has_suffix (raw_name, ".xml"))
        {
            gchar *name = g_strdup (raw_name);

            name[strlen (name) - 4] = '\0';
            g_array_append_val (history_names, name);
            g_object_unref (history);
        }
    }

    ret = (GStrv) history_names->data;

file_err:
    g_object_unref (histories);
    g_array_free (history_names,
                  FALSE); /* free_segment */

dir_err:
    g_object_unref (history_dir);

    return ret;
}
