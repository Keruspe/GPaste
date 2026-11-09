// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The control center's keybindings file, as the build generated it: every
 * shortcut g_paste_keybindings () lists, in its order, under its key and with
 * its untranslated description, and nothing else. */

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-keybindings.h>

static const gchar *xml_path;

typedef struct
{
    GPtrArray *names;
    GPtrArray *descriptions;
    gchar     *schema;
} Entries;

static void
on_start_element (GMarkupParseContext *context G_GNUC_UNUSED,
                  const gchar         *element,
                  const gchar        **attribute_names,
                  const gchar        **attribute_values,
                  gpointer             user_data,
                  GError             **error)
{
    Entries *entries = user_data;

    if (g_str_equal (element, "KeyListEntries"))
    {
        const gchar *schema = NULL;

        if (g_markup_collect_attributes (element, attribute_names, attribute_values, error,
                                         G_MARKUP_COLLECT_STRING, "schema", &schema,
                                         G_MARKUP_COLLECT_STRING, "group", NULL,
                                         G_MARKUP_COLLECT_STRING, "name", NULL,
                                         G_MARKUP_COLLECT_STRING, "package", NULL,
                                         G_MARKUP_COLLECT_INVALID))
            g_set_str (&entries->schema, schema);
    }
    else if (g_str_equal (element, "KeyListEntry"))
    {
        const gchar *name = NULL;
        const gchar *description = NULL;

        if (g_markup_collect_attributes (element, attribute_names, attribute_values, error,
                                         G_MARKUP_COLLECT_STRING, "name", &name,
                                         G_MARKUP_COLLECT_STRING, "description", &description,
                                         G_MARKUP_COLLECT_INVALID))
        {
            g_ptr_array_add (entries->names, g_strdup (name));
            g_ptr_array_add (entries->descriptions, g_strdup (description));
        }
    }
    else
        g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT, "unexpected <%s>", element);
}

static void
test_keybindings_xml (void)
{
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    g_autoptr (GError) error = NULL;

    g_assert_true (g_file_get_contents (xml_path, &contents, &length, &error));
    g_assert_no_error (error);
    g_assert_true (g_utf8_validate (contents, (gssize) length, NULL));

    g_autoptr (GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
    g_autoptr (GPtrArray) descriptions = g_ptr_array_new_with_free_func (g_free);
    g_autofree gchar *schema = NULL;
    Entries entries = { names, descriptions, NULL };
    const GMarkupParser parser = { .start_element = on_start_element };
    g_autoptr (GMarkupParseContext) context = g_markup_parse_context_new (&parser, G_MARKUP_DEFAULT_FLAGS, &entries, NULL);

    g_assert_true (g_markup_parse_context_parse (context, contents, (gssize) length, &error) &&
                   g_markup_parse_context_end_parse (context, &error));
    g_assert_no_error (error);
    schema = entries.schema;
    g_assert_cmpstr (schema, ==, G_PASTE_SETTINGS_NAME);

    gsize n_keybindings = 0;
    const GPasteKeybindingInfo *keybindings = g_paste_keybindings (&n_keybindings);

    g_assert_cmpuint (names->len, ==, n_keybindings);
    for (gsize i = 0; i < n_keybindings; ++i)
    {
        g_assert_cmpstr (g_ptr_array_index (names, i), ==, keybindings[i].key);
        g_assert_cmpstr (g_ptr_array_index (descriptions, i), ==, keybindings[i].description);
    }
}

gint
main (gint   argc,
      gchar *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_assert_cmpint (argc, ==, 2);
    xml_path = argv[1];

    g_test_add_func ("/keybindings-xml/entries", test_keybindings_xml);

    return g_test_run ();
}
