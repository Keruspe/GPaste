#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef enum
{
    BEGIN,
    IN_HISTORY,
    IN_ITEM,
    HAS_TEXT,
    END
} State;

typedef enum
{
    TEXT,
    IMAGE,
    URIS
} Type;

typedef struct
{
    State state;
    Type  type;
} Data;

#define DATA() ((Data *) user_data)
#define STATE() (DATA ()->state)
#define ASSERT_STATE(x) assert(STATE () == x)
#define SET_STATE(x) STATE () = x
#define SWITCH_STATE(x, y) ASSERT_STATE (x); SET_STATE (y)
#define TYPE() (DATA ()->type)
#define ASSERT_TYPE(x) assert(TYPE () == x)
#define SET_TYPE(x) TYPE () = x

static void
start_tag (GMarkupParseContext *context,
           const gchar         *element_name,
           const gchar        **attribute_names,
           const gchar        **attribute_values,
           gpointer             user_data,
           GError             **error)
{
    if (!g_strcmp0 (element_name, "history"))
    {
        SWITCH_STATE (BEGIN, IN_HISTORY);
    }
    else if (!g_strcmp0 (element_name, "item"))
    {
        SWITCH_STATE (IN_HISTORY, IN_ITEM);
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (!g_strcmp0 (*a, "kind"))
            {
                if (!g_strcmp0 (*v, "Text"))
                    SET_TYPE (TEXT);
                else if (!g_strcmp0 (*v, "Image"))
                    SET_TYPE (IMAGE);
                else if (!g_strcmp0 (*v, "Uris"))
                    SET_TYPE (URIS);
                else
                    assert (0);
            }
            else if (!g_strcmp0 (*a, "date"))
            {
                ASSERT_TYPE (IMAGE);
            }
            else
                assert (0);
        }
    }
    else
        assert (0);
}

static void
end_tag (GMarkupParseContext *context,
         const gchar         *element_name,
         gpointer             user_data,
         GError             **error)
{
    if (!g_strcmp0 (element_name, "history"))
    {
        SWITCH_STATE (IN_HISTORY, END);
    }
    else if (!g_strcmp0 (element_name, "item"))
    {
        SWITCH_STATE (HAS_TEXT, IN_HISTORY);
    }
    else
        assert (0);
}

static const gchar *
type_str (Type t)
{
    switch (t)
    {
    case TEXT:
        return "TEXT";
    case IMAGE:
        return "IMAGE";
    case URIS:
        return "URIS";
    }
}

static void
on_text (GMarkupParseContext *context,
         const gchar         *text,
         gsize                text_len,
         gpointer             user_data,
         GError             **error)
{
    gchar *txt = calloc (text_len + 1, 1);
    memcpy(txt, text, text_len);
    txt[text_len] = 0;
    switch (STATE ())
    {
    case IN_HISTORY:
    case HAS_TEXT:
        assert (!*g_strstrip (txt));
        break;
    case IN_ITEM:
        if (*g_strstrip (txt))
        {
            SWITCH_STATE (IN_ITEM, HAS_TEXT);
            g_print ("%s[%s]\n", type_str (TYPE ()), txt);
        }
        break;
    default:
        assert (0);
    }
}

static void on_error (GMarkupParseContext *context,
                      GError              *error,
                      gpointer             user_data)
{
    g_print ("error\n");
}

int
main (int argc, char *argv[])
{
    GMarkupParser parser = {
        start_tag,
        end_tag,
        on_text,
        NULL,
        on_error
    };
    Data data = {
        BEGIN,
        TEXT
    };
    GMarkupParseContext *ctx = g_markup_parse_context_new (&parser,
                                                           G_MARKUP_TREAT_CDATA_AS_TEXT,
                                                           &data,
                                                           NULL);
    gchar *text;
    gsize text_length;
    g_file_get_contents ("/home/keruspe/.local/share/gpaste/history.xml", &text, &text_length, NULL);
    g_markup_parse_context_parse (ctx, text, text_length, NULL);
    g_markup_parse_context_end_parse (ctx, NULL);
    assert (data.state == END);
    g_markup_parse_context_unref (ctx);
    return 0;
}
