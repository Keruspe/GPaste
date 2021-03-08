/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-util.h>

#include <getopt.h>
#include <stdio.h>

typedef struct {
    GPasteClient *client;
    gint          argc;
    const gchar **args;
    gchar        *pipe_data;
    const gchar  *uuid;
    gboolean      help;
    gboolean      version;
    gboolean      oneline;
    gboolean      raw;
    gboolean      reverse;
    gboolean      use_index;
    gboolean      zero;
    const gchar  *decoration;
    const gchar  *separator;
} Context;

/*
 * Utility functions
 */

static gboolean
parse_cmdline (int     *argc,
               char   **argv[],
               Context *ctx)
{
    struct option long_options[] = {
        { "decoration", required_argument, NULL,  'd'  },
        { "help",       no_argument,       NULL,  'h'  },
        { "oneline",    no_argument,       NULL,  'o'  },
        { "raw",        no_argument,       NULL,  'r'  },
        { "reverse",    no_argument,       NULL,  'e'  },
        { "separator",  required_argument, NULL,  's'  },
        { "use-index",  no_argument,       NULL,  'i'  },
        { "version",    no_argument,       NULL,  'v'  },
        { "zero",       no_argument,       NULL,  'z'  },
        { NULL,         no_argument,       NULL,  '\0' }
    };
    gint64 c;

    while ((c = getopt_long(*argc, *argv, "d:hores:ivz", long_options, NULL)) != -1)
    {
        switch (c)
        {
        case 'd':
            ctx->decoration = optarg;
            break;
        case 'h':
            ctx->help = TRUE;
            break;
        case 'o':
            ctx->oneline = TRUE;
            break;
        case 'r':
            ctx->raw = TRUE;
            break;
        case 'e':
            ctx->reverse = TRUE;
            break;
        case 's':
            ctx->separator = optarg;
            break;
        case 'i':
            ctx->use_index = TRUE;
            break;
        case 'v':
            ctx->version = TRUE;
            break;
        case 'z':
            ctx->zero = TRUE;
            break;
        default:
            ctx->help = TRUE;
            return FALSE;
        }
    }

    *argc -= optind;
    *argv += optind;

    ctx->argc = *argc - 1;
    ctx->args = (const gchar **) *argv;
    ++ctx->args;

    return TRUE;
}

static gchar *
extract_pipe_data (void)
{
    if (isatty (STDIN_FILENO))
        return NULL; /* We're not being piped */

    g_autoptr (GString) data = g_string_new (NULL);
    gint64 c;

    while ((c = fgetc (stdin)) != EOF)
        data = g_string_append_c (data, (guchar)c);
    g_string_append_c (data, '\0');

    return (*data->str) ? g_strdup (data->str) : NULL;
}

static const gchar *
strip_newline (gchar *str)
{
    for (gchar *s = str; *s; ++s)
    {
        if (*s == '\n')
            *s = ' ';
    }
    return str;
}

static void
print_history_line (gchar       *line,
                    const gchar *uuid,
                    Context     *ctx)
{
    if (!ctx->raw)
        printf ("%s: ", uuid);
    printf ("%s%c", (ctx->oneline) ? strip_newline (line) : line, (ctx->zero) ? '\0' : '\n');
}

static gint
spawn (const gchar *app)
{
    g_autoptr (GError) error = NULL;

    if (!g_paste_util_spawn_sync (app, &error))
    {
        g_critical ("%s %s: %s", _("Couldn't spawn"), app, error->message);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void
show_help (void)
{
    const char *progname = g_get_prgname ();

    printf (_("Usage:\n"));
    /* Translators: help for gpaste history */
    printf ("  %s [history]: %s\n", progname, _("print the history with UUIDs"));
    /* Translators: help for gpaste history-size */
    printf ("  %s history-size: %s\n", progname, _("print the size of the history"));
    /* Translators: help for gpaste get-history */
    printf ("  %s get-history: %s\n", progname, _("get the name of the current history"));
    /* Translators: help for gpaste backup-history <name> */
    printf ("  %s backup-history <%s>: %s\n", progname, _("name"), _("backup current history"));
    /* Translators: help for gpaste switch-history <name> */
    printf ("  %s switch-history <%s>: %s\n", progname, _("name"), _("switch to another history"));
    /* Translators: help for gpaste delete-history <name> */
    printf ("  %s delete-history <%s>: %s\n", progname, _("name"),  _("delete a history"));
    /* Translators: help for gpaste list-histories */
    printf ("  %s list-histories: %s\n", progname, _("list available histories"));
    /* Translators: help for gpaste add <text> */
    printf ("  %s add <%s>: %s\n", progname, _("text"), _("set text to clipboard"));
    /* Translators: help for gpaste add-password <name> <text> */
    printf ("  %s add-password <%s> <%s>: %s\n", progname, _("name"), _("password"), _("add the name - password couple to the clipboard"));
    /* Translators: help for gpaste rename-password <old name> <new name> */
    printf ("  %s rename-password <%s> <%s>: %s\n", progname, _("old name"), _("new name"), _("rename the password"));
    /* Translators: help for gpaste get <uuid> */
    printf ("  %s get <uuid>: %s\n", progname, _("get the item <uuid> from the history"));
    /* Translators: help for gpaste select <uuid> */
    printf ("  %s select <uuid>: %s\n", progname, _("set the item <uuid> from the history to the clipboard"));
    /* Translators: help for gpaste replace <uuid> <contents> */
    printf ("  %s replace <uuid>  <%s>: %s\n", progname, _("contents"), _("replace the contents of the item <uuid> from the history with the provided one"));
    /* Translators: help for gpaste merge <uuid> … <uuid> */
    printf ("  %s merge <uuid> … <uuid>: %s\n", progname, _("merge the items matching the UUIDs from the history and put the result in the clipboard"));
    /* Translators: help for gpaste set-password <uuid> <name> */
    printf ("  %s set-password <uuid> <%s>: %s\n", progname, _("name"), _("set the item <uuid> from the history as a password named <name>"));
    /* Translators: help for gpaste delete <uuid> */
    printf ("  %s delete <uuid>: %s\n", progname, _("delete item <uuid> from the history"));
    /* Translators: help for gpaste delete-passworf <name> */
    printf ("  %s delete-password <%s>: %s\n", progname, _("name"), _("delete the password <name> from the history"));
    /* Translators: help for gpaste file <path> */
    printf ("  %s file <%s>: %s\n", progname, _("path"), _("put the content of the file at <path> into the clipboard"));
    /* Translators: help for whatever | gpaste */
    printf ("  %s | %s: %s\n", _("whatever"), progname, _("set the output of whatever to clipboard"));
    /* Translators: help for gpaste empty */
    printf ("  %s empty: %s\n", progname, _("empty the history"));
    /* Translators: help for gpaste start */
    printf ("  %s start: %s\n", progname, _("start tracking clipboard changes"));
    /* Translators: help for gpaste stop */
    printf ("  %s stop: %s\n", progname, _("stop tracking clipboard changes"));
    /* Translators: help for gpaste quit */
    printf ("  %s quit: %s\n", progname, _("alias for stop"));
    /* Translators: help for gpaste daemon-reexec */
    printf ("  %s daemon-reexec: %s\n", progname, _("reexecute the daemon (after upgrading...)"));
    /* Translators: help for gpaste settings */
    printf ("  %s settings: %s\n", progname, _("launch the configuration tool"));
    /* Translators: help for gpaste ui */
    printf ("  %s ui: %s\n", progname, _("launch the graphical tool"));
    /* Translators: help for gpaste show-history */
    printf ("  %s show-history: %s\n", progname, _("make the applet or extension display the history"));
    /* Translators: help for gpaste upload */
    printf ("  %s upload <uuid>: %s\n", progname, _("upload the item <uuid> to a pastebin service"));
    /* Translators: help for gpaste version */
    printf ("  %s version: %s\n", progname, _("display the version"));
    /* Translators: help for gpaste daemon-version */
    printf ("  %s daemon-version: %s\n", progname, _("display the daemon version"));
    /* Translators: help for gpaste help */
    printf ("  %s help: %s\n", progname, _("display this help"));
    /* Translators: help for gpaste about */
    printf ("  %s about: %s\n", progname, _("display the about dialog"));

    printf("\n");
    printf(_("Convenience options:"));
    printf("\n");
    /* Translators: help for --use-index */
    printf("  --index: %s\n", _("use the index of the item instead of its UUID"));

    printf("\n");
    printf(_("Display options:"));
    printf("\n");
    /* Translators: help for --oneline */
    printf("  --oneline: %s\n", _("display each item on only one line"));
    /* Translators: help for --raw */
    printf("  --raw: %s\n", _("display each item raw (without line numbers)"));
    /* Translators: help for --reverse */
    printf("  --reverse: %s\n", _("display the items in reverse order"));
    /* Translators: help for --zero */
    printf("  --zero: %s\n", _("use a NUL character instead of a new line betweean each item"));

    printf("\n");
    printf(_("Merge options:"));
    printf("\n");
    /* Translators: help for --decoration <string> */
    printf("  --decoration <%s>: %s\n", _("string"), _("add the given decoration to the beginning and the end of each item before merging"));
    /* Translators: help for --separator <string> */
    printf("  --separator <%s>: %s\n", _("string"), _("add the given separator between each item when merging"));
}

static void
show_version (void)
{
    printf ("%s\n", PACKAGE_STRING);
}

/*
 * GPaste commands
 */

static gint
g_paste_help (Context *ctx   G_GNUC_UNUSED,
              GError **error G_GNUC_UNUSED)
{
    show_help ();

    return EXIT_SUCCESS;
}

static gint
g_paste_version (Context *ctx   G_GNUC_UNUSED,
                 GError **error G_GNUC_UNUSED)
{
    show_version ();

    return EXIT_SUCCESS;
}

static gint
g_paste_flag_action (Context *ctx,
                     GError **error)
{
    if (ctx->help)
        return g_paste_help (ctx, error);
    if (ctx->version)
        return g_paste_version (ctx, error);
    return -1;
}

static gint
g_paste_history (Context *ctx,
                 GError **error)
{
    GList *history = (ctx->raw) ?
        g_paste_client_get_raw_history_sync (ctx->client, error) :
        g_paste_client_get_history_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    for (const GList *i = (ctx->reverse ? g_list_last (history) : history); i; i = ctx->reverse ? i->prev : i->next)
    {
        const GPasteClientItem *item = i->data;
        g_autofree gchar *line = g_strdup (g_paste_client_item_get_value (item));
        print_history_line (line, g_paste_client_item_get_uuid (item), ctx);
    }

    g_list_free_full (history, g_object_unref);

    return EXIT_SUCCESS;
}

static gint
g_paste_about (Context *ctx,
               GError **error)
{
    g_paste_client_about_sync (ctx->client, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_daemon_reexec (Context *ctx,
                       GError **error)
{
    g_paste_client_reexecute_sync (ctx->client, error);

    gboolean success = (!*error || (*error)->code == G_DBUS_ERROR_NO_REPLY);

    g_clear_error (error);

#ifdef G_OS_UNIX
    if (!success)
    {
        GPid pid = g_paste_util_read_pid_file ("Daemon");

        if (pid != (GPid) -1)
            success = !kill (pid, SIGUSR1);
    }
#endif

    if (!success)
        return EXIT_FAILURE;

    printf (_("Successfully reexecuted the daemon\n"));

    return EXIT_SUCCESS;
}

static gint
g_paste_daemon_version (Context *ctx,
                        GError **error G_GNUC_UNUSED)
{
    g_autofree gchar *v = g_paste_client_get_version (ctx->client);

    printf ("%s\n", v);

    return EXIT_SUCCESS;
}

static gint
g_paste_empty (Context *ctx,
               GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : g_paste_client_get_history_name_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_empty_history_sync (ctx->client, name, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_get_history (Context *ctx,
                     GError **error)
{
    g_autofree gchar *name = g_paste_client_get_history_name_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    printf("%s\n", name);

    return EXIT_SUCCESS;
}

static gint
g_paste_history_size (Context *ctx,
                      GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : g_paste_client_get_history_name_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    guint64 size = g_paste_client_get_history_size_sync (ctx->client, name, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%" G_GUINT64_FORMAT "\n", size);

    return EXIT_SUCCESS;
}

static gint
g_paste_list_histories (Context *ctx,
                        GError **error)
{
    g_auto (GStrv) histories = g_paste_client_list_histories_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    for (GStrv h = histories; *h; ++h)
        printf ("%s\n", *h);

    return EXIT_SUCCESS;
}

static gint
g_paste_delete_history (Context *ctx,
                        GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : g_paste_client_get_history_name_sync (ctx->client, error);

    g_paste_client_delete_history_sync (ctx->client, name, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_settings (Context *ctx G_GNUC_UNUSED,
                  GError **error)
{
    if (!g_paste_util_activate_ui_sync ("prefs", NULL, error))
    {
        g_critical ("%s Ui: %s", _("Couldn't spawn"), (*error)->message);
        g_clear_error (error);

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static gint
g_paste_show_history (Context *ctx,
                      GError **error)
{
    g_paste_client_show_history_sync (ctx->client, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_start (Context *ctx,
               GError **error)
{
    g_paste_client_track_sync (ctx->client, TRUE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_stop (Context *ctx,
              GError **error)
{
    g_paste_client_track_sync (ctx->client, FALSE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_ui (Context *ctx   G_GNUC_UNUSED,
            GError **error G_GNUC_UNUSED)
{
    return spawn ("Ui");
}

static gint
g_paste_add (Context *ctx,
             GError **error)
{
    const gchar *data = (ctx->argc > 0) ? ctx->args[0] : ctx->pipe_data;

    if (!data)
        return -1;

    if (!g_utf8_validate(data, -1, NULL))
    {
        g_critical (_("Cannot add non utf8 data as text."));
        return EXIT_FAILURE;
    }

    g_paste_client_add_sync (ctx->client, data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_add_password (Context *ctx,
                      GError **error)
{
    const gchar *data = (ctx->argc > 1) ? ctx->args[1] : ctx->pipe_data;

    if (!data)
        return EXIT_FAILURE;

    g_paste_client_add_password_sync (ctx->client, ctx->args[0], data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_backup_history (Context *ctx   G_GNUC_UNUSED,
                        GError **error G_GNUC_UNUSED)
{
    guint64 index = 0;
    g_autofree gchar *name = (ctx->argc > 1) ? g_strdup (ctx->args[index++]) : g_paste_client_get_history_name_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_backup_history_sync (ctx->client, name, ctx->args[index], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_delete (Context *ctx,
                GError **error)
{
    g_paste_client_delete_sync (ctx->client, ctx->uuid, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_delete_password (Context *ctx,
                         GError **error)
{
    g_paste_client_delete_password_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_file (Context *ctx,
              GError **error)
{
    g_paste_client_add_file_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_get (Context *ctx,
             GError **error)
{
    g_autofree gchar *value = ((ctx->raw) ? g_paste_client_get_raw_element_sync : g_paste_client_get_element_sync) (ctx->client, ctx->uuid, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s", value);

    return EXIT_SUCCESS;
}

static gint
g_paste_replace (Context *ctx,
                 GError **error)
{
    const gchar *data = (ctx->argc > 1) ? ctx->args[1] : ctx->pipe_data;

    if (!data)
        return EXIT_FAILURE;

    g_paste_client_replace_sync (ctx->client, ctx->uuid, data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_search (Context *ctx,
                GError **error)
{
    g_auto (GStrv) results = g_paste_client_search_sync (ctx->client, ctx->args[0], error);

    if (*error)
        return EXIT_FAILURE;

    GList *items = g_paste_client_get_elements_sync (ctx->client, (const gchar **) results, -1, error);

    for (const GList *i = items; i; i = i->next)
    {
        const GPasteClientItem *item = i->data;
        g_autofree gchar *line = g_strdup (g_paste_client_item_get_value (item));
        print_history_line (line, g_paste_client_item_get_uuid (item), ctx);
    }

    g_list_free_full (items, g_object_unref);

    return EXIT_SUCCESS;
}

static gint
g_paste_select (Context *ctx,
                GError **error)
{
    g_paste_client_select_sync (ctx->client, ctx->uuid, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_switch_history (Context *ctx,
                        GError **error)
{
    g_paste_client_switch_history_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_upload (Context *ctx,
                GError **error)
{
    g_paste_client_upload_sync (ctx->client, ctx->uuid, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_rename_password (Context *ctx,
                         GError **error)
{
    g_paste_client_rename_password_sync (ctx->client, ctx->args[0], ctx->args[1], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_set_password (Context *ctx,
                      GError **error)
{
    g_paste_client_set_password_sync (ctx->client, ctx->uuid, ctx->args[1], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_merge (Context *ctx,
               GError **error)
{
    g_paste_client_merge_sync (ctx->client, ctx->decoration, ctx->separator, ctx->args, ctx->argc, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*
 * Main
 */

static gint
g_paste_dispatch (gint         argc,
                  const gchar *verb,
                  Context     *ctx,
                  GError     **error)
{
    static struct {
        gint         argc;
        const gchar *verb;
        gint         extra_args;
        gboolean     needs_client;
        gint       (*handler) (Context *ctx,
                               GError **error);
    } dispatch[] = {
        { 0, NULL,              G_MAXINT, FALSE, g_paste_flag_action     },
        { 0, NULL,              0,        TRUE,  g_paste_add             },
        { 0, NULL,              0,        TRUE,  g_paste_history         },
        { 1, "help",            0,        FALSE, g_paste_help            },
        { 1, "v",               0,        FALSE, g_paste_version         },
        { 1, "version",         0,        FALSE, g_paste_version         },
        { 1, "about",           0,        TRUE,  g_paste_about           },
        { 1, "dr",              0,        TRUE,  g_paste_daemon_reexec   },
        { 1, "daemon-reexec",   0,        TRUE,  g_paste_daemon_reexec   },
        { 1, "dv",              0,        TRUE,  g_paste_daemon_version  },
        { 1, "daemon-version",  0,        TRUE,  g_paste_daemon_version  },
        { 1, "e",               1,        TRUE,  g_paste_empty           },
        { 1, "empty",           1,        TRUE,  g_paste_empty           },
        { 1, "gh",              0,        TRUE,  g_paste_get_history     },
        { 1, "get-history",     0,        TRUE,  g_paste_get_history     },
        { 1, "h",               0,        TRUE,  g_paste_history         },
        { 1, "history",         0,        TRUE,  g_paste_history         },
        { 1, "hs",              1,        TRUE,  g_paste_history_size    },
        { 1, "history-size",    1,        TRUE,  g_paste_history_size    },
        { 1, "lh",              0,        TRUE,  g_paste_list_histories  },
        { 1, "list-histories",  0,        TRUE,  g_paste_list_histories  },
        { 1, "dh",              1,        TRUE,  g_paste_delete_history  },
        { 1, "delete-history",  1,        TRUE,  g_paste_delete_history  },
        { 1, "settings",        0,        FALSE, g_paste_settings        },
        { 1, "p",               0,        FALSE, g_paste_settings        },
        { 1, "preferences",     0,        FALSE, g_paste_settings        },
        { 1, "show-history",    0,        TRUE,  g_paste_show_history    },
        { 1, "start",           0,        TRUE,  g_paste_start           },
        { 1, "d",               0,        TRUE,  g_paste_start           },
        { 1, "daemon",          0,        TRUE,  g_paste_start           },
        { 1, "stop",            0,        TRUE,  g_paste_stop            },
        { 1, "q",               0,        TRUE,  g_paste_stop            },
        { 1, "quit",            0,        TRUE,  g_paste_stop            },
        { 1, "ui",              0,        FALSE, g_paste_ui              },
        { 1, "a",               1,        TRUE,  g_paste_add             },
        { 1, "add",             1,        TRUE,  g_paste_add             },
        { 2, "ap",              1,        TRUE,  g_paste_add_password    },
        { 2, "add-password",    1,        TRUE,  g_paste_add_password    },
        { 2, "bh",              1,        TRUE,  g_paste_backup_history  },
        { 2, "backup-history",  1,        TRUE,  g_paste_backup_history  },
        { 2, "d",               0,        TRUE,  g_paste_delete          },
        { 2, "del",             0,        TRUE,  g_paste_delete          },
        { 2, "delete",          0,        TRUE,  g_paste_delete          },
        { 2, "rm",              0,        TRUE,  g_paste_delete          },
        { 2, "remove",          0,        TRUE,  g_paste_delete          },
        { 2, "dp",              0,        TRUE,  g_paste_delete_password },
        { 2, "delete-password", 0,        TRUE,  g_paste_delete_password },
        { 2, "f",               0,        TRUE,  g_paste_file            },
        { 2, "file",            0,        TRUE,  g_paste_file            },
        { 2, "g",               0,        TRUE,  g_paste_get             },
        { 2, "get",             0,        TRUE,  g_paste_get             },
        { 2, "replace",         1,        TRUE,  g_paste_replace         },
        { 2, "search",          0,        TRUE,  g_paste_search          },
        { 2, "s",               0,        TRUE,  g_paste_select          },
        { 2, "set",             0,        TRUE,  g_paste_select          },
        { 2, "select",          0,        TRUE,  g_paste_select          },
        { 2, "sh",              0,        TRUE,  g_paste_switch_history  },
        { 2, "switch-history",  0,        TRUE,  g_paste_switch_history  },
        { 2, "u",               0,        TRUE,  g_paste_upload          },
        { 2, "upload",          0,        TRUE,  g_paste_upload          },
        { 3, "rp",              0,        TRUE,  g_paste_rename_password },
        { 3, "rename-password", 0,        TRUE,  g_paste_rename_password },
        { 3, "sp",              0,        TRUE,  g_paste_set_password    },
        { 3, "set-password",    0,        TRUE,  g_paste_set_password    },
        { 4, "m",               G_MAXINT, TRUE,  g_paste_merge           },
        { 4, "merge",           G_MAXINT, TRUE,  g_paste_merge           },
    };

    for (guint64 i = 0; i < G_N_ELEMENTS (dispatch); ++i)
    {
        if (argc == dispatch[i].argc || dispatch[i].extra_args == G_MAXINT || (argc > dispatch[i].argc && argc <= (dispatch[i].argc + dispatch[i].extra_args)))
        {
            if (argc > 0 && dispatch[i].verb && !g_paste_str_equal (verb, dispatch[i].verb))
                continue;

            if (dispatch[i].needs_client && !ctx->client)
                return EXIT_FAILURE;

            gint ret = dispatch[i].handler(ctx, error);
            if (ret >= 0)
            {
                if (!dispatch[i].needs_client && !ctx->client)
                    g_clear_error (error);

                return ret;
            }
        }
    }

    return -1;
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_GETTEXT ();
    g_set_prgname (argv[0]);

    g_autoptr (GError) error = NULL;
    Context ctx = { NULL, 0, NULL, NULL, NULL, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, NULL, NULL };
    gint status = EXIT_SUCCESS;

    if (parse_cmdline (&argc, &argv, &ctx))
    {
        g_autoptr (GPasteClient) client = ctx.client = g_paste_client_new_sync (&error);
        g_autofree gchar *pipe_data = ctx.pipe_data = extract_pipe_data ();
        g_autofree gchar *uuid = NULL;

        if (ctx.use_index && ctx.argc > 0)
        {
            g_autoptr (GPasteClientItem) item = g_paste_client_get_element_at_index_sync (ctx.client, g_ascii_strtoull (ctx.args[0], NULL, 10), &error);

            if (!error)
            {
                ctx.uuid = uuid = g_strdup (g_paste_client_item_get_uuid (item));
            }
        }
        else
        {
            ctx.uuid = ctx.args[0];
        }

        if (!error)
        {
            status = g_paste_dispatch (argc, (argc > 0) ? argv[0] : NULL, &ctx, &error);
        }

        if (error)
        {
            g_critical ("%s\n", (error) ? error->message : _("Couldn't connect to GPaste daemon"));
            status = EXIT_FAILURE;
        }
    }
    else
    {
        show_help ();
        status = EXIT_FAILURE;
    }

    return status;
}
