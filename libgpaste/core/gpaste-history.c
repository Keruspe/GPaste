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
#include "gpaste-image-item.h"
#include "gpaste-text-item.h"
#include "gpaste-uris-item.h"

#include <glib/gi18n-lib.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>

struct _GPasteHistoryPrivate
{
    GPasteSettings *settings;
    GSList         *history;
    gsize           size;

    /* Note: we never track the first (active) item here */
    GPasteItem     *biggest_item;
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
g_paste_history_elect_new_biggest (GPasteHistory *self)
{
    GPasteHistoryPrivate *priv = self->priv;
    GPasteItem *old_biggest_item = priv->biggest_item;

    priv->biggest_item = NULL;
    priv->biggest_size = 0;

    GSList *history = priv->history;

    if (history)
    {
        /* slip first item */
        for (history = g_slist_next (history); history; history = g_slist_next (history))
        {
            GPasteItem *item = history->data;
            gsize size = g_paste_item_get_size (item);

            if (size > priv->biggest_size)
            {
                priv->biggest_item = item;
                priv->biggest_size = size;
            }
        }
    }
}

static GSList *
_g_paste_history_remove (GPasteHistory *self,
                         GSList        *elem,
                         gboolean       remove_leftovers,
                         gboolean      *was_biggest)
{
    GPasteHistoryPrivate *priv = self->priv;
    GPasteItem *item = elem->data;

    g_debug ("removing %p, biggest is %p", item, priv->biggest_item);

    if (was_biggest)
        *was_biggest = g_paste_item_equals (item, priv->biggest_item);

    priv->size -= g_paste_item_get_size (item);

    if (remove_leftovers && G_PASTE_IS_IMAGE_ITEM (item))
    {
        GFile *image = g_file_new_for_path (g_paste_item_get_value (item));
        g_file_delete (image,
                       NULL, /* cancellable */
                       NULL); /* error */
        g_object_unref (image);
    }

    g_object_unref (item);
    return g_slist_delete_link (elem, elem);
}

static void
g_paste_history_check_memory_usage (GPasteHistory *self,
                                    gsize          max_memory)
{
    GPasteHistoryPrivate *priv = self->priv;

    g_debug ("%zu VS %zu (max %zu)", priv->size, max_memory, priv->biggest_size);
    while (priv->size > max_memory && priv->biggest_item)
    {
        g_debug ("%zu VS %zu (max %zu)", priv->size, max_memory, priv->biggest_size);
        for (GSList *prev = priv->history, *history = g_slist_next (priv->history); history; prev = history, history = g_slist_next (history))
        {
            if (g_paste_item_equals (history->data, priv->biggest_item))
            {
                prev->next = _g_paste_history_remove (self, history, TRUE, NULL);
                GPasteItem *previous_biggest = priv->biggest_item;
                g_paste_history_elect_new_biggest (self);
                g_assert (priv->biggest_item != previous_biggest);
                break;
            }
        }
    }
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
    GPasteSettings *settings = priv->settings;

    gsize max_memory = g_paste_settings_get_max_memory_usage (priv->settings) * 1024 * 1024;
    gsize size = g_paste_item_get_size (item);
    g_return_if_fail (size < max_memory);
    priv->size += size;

    gboolean was_biggest = FALSE;
    if (history)
    {
        GPasteItem *old_first = history->data;
        if (g_paste_item_equals (old_first, item))
            return;

        /* size may change when state is idle */
        priv->size -= g_paste_item_get_size (old_first);
        g_paste_item_set_state (old_first, G_PASTE_ITEM_STATE_IDLE);

        gsize _size = g_paste_item_get_size (old_first);
        priv->size += _size;
        if (_size >= priv->biggest_size)
        {
            priv->biggest_item = item;
            priv->biggest_size = _size;
        }

        GSList *prev = history;
        for (history = g_slist_next (history); history; prev = history, history = g_slist_next (history))
        {
            if (g_paste_item_equals (history->data, item))
            {
                prev->next = _g_paste_history_remove (self, history, FALSE, &was_biggest);
                break;
            }
        }
    }

    gboolean fifo = g_paste_settings_get_fifo (settings);
    history = priv->history = fifo ?
        g_slist_append (priv->history, g_object_ref (item)) :
        g_slist_prepend (priv->history, g_object_ref (item));

    g_paste_item_set_state (item, G_PASTE_ITEM_STATE_ACTIVE);

    if (was_biggest)
        g_paste_history_elect_new_biggest (self);

    guint32 max_history_size = g_paste_settings_get_max_history_size (settings);
    guint length = g_slist_length (history);

    if (length > max_history_size)
    {
        if (fifo)
        {
            GSList *previous = g_slist_nth(history, length - max_history_size - 1);
            /* start the shortened list at the right place */
            priv->history = g_slist_next (previous);
            /* terminate the original list so that it can be freed (below) */
            previous->next = NULL;
        }
        else
        {
            history = g_slist_nth (history, max_history_size - 1);
            history->next = NULL;
            history = g_slist_next (history);
        }

        for (GSList *_history = history; _history; _history = g_slist_next (_history))
        {
            GPasteItem *_item = _history->data;

            if (g_paste_item_equals (_item, priv->biggest_item))
            {
                priv->size -= g_paste_item_get_size (_item);
                g_paste_history_elect_new_biggest (self);
            }
        }
        g_slist_free_full (history,
                           g_object_unref);
    }

    g_paste_history_check_memory_usage (self, max_memory);

    if (fifo)
        g_paste_history_select (self, 0);
    else
    {
        g_signal_emit (self,
                       signals[CHANGED],
                       0); /* detail */
    }
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

    GPasteHistoryPrivate *priv = self->priv;
    GSList *history = priv->history;

    g_return_if_fail (pos < g_slist_length (history));

    gboolean was_biggest;

    if (pos)
    {
        GSList *prev = g_slist_nth (history, pos - 1);
        prev->next = _g_paste_history_remove (self, g_slist_next (prev), TRUE, &was_biggest);
    }
    else
    {
        priv->history = _g_paste_history_remove (self, history, TRUE, &was_biggest);
        g_paste_history_select (self, 0);
    }

    if (was_biggest)
        g_paste_history_elect_new_biggest (self);

    g_signal_emit (self,
                   signals[CHANGED],
                   0); /* detail */
}

static GPasteItem *
_g_paste_history_get (GPasteHistory *self,
                      guint32        pos)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (self), NULL);

    GSList *history = self->priv->history;

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
    return _g_paste_history_get (self, pos);
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
    return g_object_ref (_g_paste_history_get (self, pos));
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
    GPasteItem *item = _g_paste_history_get (self, pos);

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
    priv->size = 0;

    g_paste_history_elect_new_biggest (self);

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

    gchar *history_file_name = g_strconcat (g_paste_settings_get_history_name (priv->settings), ".xml", NULL);
    gchar *history_file_path = g_build_filename (history_dir_path, history_file_name, NULL);
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
    g_free (history_file_name);
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

    GPasteHistoryPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;

    g_slist_free_full (priv->history,
                       g_object_unref);
    priv->history = NULL;

    gchar *history_file_name = g_strconcat (g_paste_settings_get_history_name (settings), ".xml", NULL);
    gchar *history_file_path = g_build_filename (g_get_user_data_dir (), "gpaste", history_file_name, NULL);
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
            if (!name || g_strcmp0 (name, "item") != 0)
                continue;

            ++i;

            gchar *kind = (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "kind");
            gchar *date = (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "date");
            gchar *raw_value = (gchar *) xmlTextReaderReadString (reader);
            gchar *value = g_paste_history_decode (raw_value);
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

            g_free (raw_value);
            g_free (value);
            g_free (date);
            g_free (kind);
        }

        xmlFreeTextReader (reader);
    }
    else
    {
        /* Create the empty file to be listed as an available history */
        g_paste_history_save (self);
    }

    g_object_unref (history_file);
    g_free (history_file_path);
    g_free (history_file_name);

    if (priv->history)
    {
        g_paste_item_set_state (priv->history->data, G_PASTE_ITEM_STATE_ACTIVE);
        g_paste_history_elect_new_biggest (self);
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

    g_paste_settings_set_history_name (self->priv->settings, name);
    g_paste_history_load (self);

    g_signal_emit (self,
                   signals[CHANGED],
                   0); /* detail */
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

    GPasteHistoryPrivate *priv = self->priv;

    gchar *history_file_name = g_strconcat (g_paste_settings_get_history_name (priv->settings), ".xml", NULL);
    gchar *history_file_path = g_build_filename (g_get_user_data_dir (), "gpaste", history_file_name, NULL);
    GFile *history_file = g_file_new_for_path (history_file_path);

    g_paste_history_empty (self);
    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        g_file_delete (history_file,
                       NULL, /* cancellable */
                       error);
    }

    g_object_unref (history_file);
    g_free (history_file_path);
    g_free (history_file_name);
}

static gboolean
g_paste_history_self_changed (GPasteHistory *self,
                              gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_history_save (self);

    return TRUE;
}

static void
g_paste_history_dispose (GObject *object)
{
    GPasteHistory *self = G_PASTE_HISTORY (object);
    GPasteHistoryPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        g_signal_handler_disconnect (self, priv->changed_signal);
        g_object_unref (settings);
        priv->settings = NULL;
    }

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
    GPasteHistoryPrivate *priv = self->priv = g_paste_history_get_instance_private (self);

    priv->history = NULL;
    priv->size = 0;
    priv->biggest_item = NULL;
    priv->biggest_size = 0;

    priv->changed_signal = g_signal_connect (G_OBJECT (self),
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

/**
 * g_paste_history_list:
 * @error: a #GError
 *
 * Get the list of available histories
 *
 * Returns: (transfer full): The list of history names
 *                           free it with g_strfreev
 */
G_PASTE_VISIBLE gchar **
g_paste_history_list (GError **error)
{
    gchar *history_dir_path = g_build_filename (g_get_user_data_dir (), "gpaste", NULL);
    GFile *history_dir = g_file_new_for_path (history_dir_path);
    gchar **ret = NULL;
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

    ret = (gchar **) history_names->data;

file_err:
    g_object_unref (histories);
    g_array_free (history_names,
                  FALSE); /* free_segment */

dir_err:
    g_object_unref (history_dir);
    g_free (history_dir_path);

    return ret;
}
