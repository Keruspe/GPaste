// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-util.h>

#include <getopt.h>
#include <stdio.h>

typedef struct
{
    GPasteClient *client;
    gint          argc;
    const gchar **args;
    gchar        *pipe_data;
    const gchar  *uuid;
    gboolean      help;
    gboolean      version;
    gboolean      favourites;
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
        { "favourites", no_argument,       NULL,  'f'  },
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

    while ((c = getopt_long (*argc, *argv, "d:fhores:ivz", long_options, NULL)) != -1)
    {
        switch (c)
        {
        case 'd':
            ctx->decoration = optarg;
            break;
        case 'f':
            ctx->favourites = TRUE;
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

    /* argv[0] is the verb and ctx->args the arguments after it. With no verb at
     * all (a plain "gpaste-client", or one only fed through a pipe) there is
     * nothing past it: stay on argv's own NULL terminator rather than stepping
     * over it, which ctx->args[0] would then read out of bounds. */
    ctx->argc = (*argc > 0) ? *argc - 1 : 0;
    ctx->args = (const gchar **) *argv + ((*argc > 0) ? 1 : 0);

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

static void
print_history_line (gchar       *line,
                    guint        index,
                    const gchar *uuid,
                    Context     *ctx)
{
    if (!ctx->raw)
    {
        if (ctx->use_index)
            printf ("%d: ", index);
        else
            printf ("%s: ", uuid);
    }
    g_autofree gchar *oneline = (ctx->oneline) ? g_paste_util_one_line (line) : NULL;

    printf ("%s%c", (oneline) ?: line, (ctx->zero) ? '\0' : '\n');
}

static gint
spawn (const gchar *app)
{
    g_autoptr (GError) error = NULL;

    if (!g_paste_util_spawn_sync (app, &error))
    {
        /* Translators: %s is the program GPaste tried to start, then the reason it could not. */
        g_critical (_("Couldn't spawn %s: %s"), app, error->message);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void show_help (void);

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
    /* The whole history even under --favourites, where GetFavourites would
     * answer the pinned items alone: what this prints beside each row is where
     * it sits in the history, which only a listing of the history knows. The
     * graphical front ends have no such number to print and do ask for the
     * short listing. */
    g_autolist (GPasteClientItem) history = g_paste_client_get_history_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    guint length = g_list_length (history);
    guint index = 0;

    for (const GList *i = (ctx->reverse ? g_list_last (history) : history); i; i = ctx->reverse ? i->prev : i->next)
    {
        GPasteClientItem *item = i->data;
        /* Where the item sits in the history, which is what --use-index takes
         * back: counted over every item, printed or not (a filtered listing
         * that renumbered its rows would name a different item on the way in),
         * and from the far end when the walk runs backwards. */
        guint position = (ctx->reverse) ? length - 1 - index : index;

        ++index;

        if (ctx->favourites && !g_paste_client_item_is_favourite (item))
            continue;

        g_autofree gchar *line = g_strdup (g_paste_client_item_get_value (item));
        print_history_line (line, position, g_paste_client_item_get_uuid (item), ctx);
    }

    return EXIT_SUCCESS;
}

/* Activating the UI app's own action, not a round trip through the daemon: the
 * daemon did nothing else with this than make the same call, and a client that
 * only wants the about dialog has no reason to need a daemon at all. */
static gint
g_paste_about (Context *ctx G_GNUC_UNUSED,
               GError **error)
{
    /* Synchronously: there is no main loop here to finish an asynchronous call
     * from, so the process would exit before anything reached the bus. */
    if (!g_paste_util_activate_ui_sync ("about", NULL, error))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* A daemon that honours the re-exec tears its D-Bus connection down before
 * replying, so @triggered is already TRUE for the expected missing reply; this
 * only covers a daemon too old for it, by signalling the pid it wrote (and then
 * dropping the D-Bus failure, since the re-exec did happen after all). */
static gboolean
reexec_fallback (gboolean triggered,
                 GError **error)
{
#ifdef G_OS_UNIX
    if (!triggered)
    {
        GPid pid = g_paste_util_read_pid_file ("Daemon");

        if (pid != (GPid) -1 && !kill (pid, SIGUSR1))
        {
            g_clear_error (error);
            triggered = TRUE;
        }
    }
#endif

    return triggered;
}

static gint
g_paste_daemon_reexec (Context *ctx,
                       GError **error)
{
    if (!reexec_fallback (g_paste_util_reexecute_daemon (ctx->client, error), error))
        return EXIT_FAILURE;

    printf ("%s\n", _("Successfully re-executed the daemon"));

    return EXIT_SUCCESS;
}

static gint
g_paste_migrate (Context *ctx,
                 GError **error)
{
    /* Trigger the migration through the daemon: the shared helper opens the gate
     * (reset the revision, sync) and then runs the very same re-exec as above. */
    if (!reexec_fallback (g_paste_util_trigger_storage_migration (ctx->client, error), error))
        return EXIT_FAILURE;

    printf ("%s\n", _("Successfully triggered the storage migration"));

    return EXIT_SUCCESS;
}

static gint
g_paste_change_passphrase (Context *ctx,
                           GError **error)
{
    /* The daemon owns the prompts (it is the one that can show them, through its
     * own GPastePrompt backend), so this only asks it to. The passphrases are
     * never sent over the bus. */
    g_paste_client_change_passphrase_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", _("Successfully triggered the passphrase change"));

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

/* The history a command acts on when none was named on the command line. It
 * comes off the proxy's cached property, which is empty until the daemon has
 * answered for it and again once it leaves the bus -- and a %NULL name must not
 * travel on to a call that expects one, where it would be built as
 * g_variant_new ("(s)", NULL). Reading it used to be a call of its own, so this
 * is the error that call would have raised. */
static gchar *
current_history (Context  *ctx,
                 GError  **error)
{
    gchar *name = g_paste_client_get_history_name (ctx->client);

    if (!name)
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND, "Couldn't get the name of the current history.");

    return name;
}

static gint
g_paste_empty (Context *ctx,
               GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_empty_history_sync (ctx->client, name, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_get_history (Context *ctx,
                     GError **error)
{
    g_autofree gchar *name = current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", name);

    return EXIT_SUCCESS;
}

static gint
g_paste_history_size (Context *ctx,
                      GError **error)
{
    guint64 size = g_paste_client_get_history_size_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%" G_GUINT64_FORMAT "\n", size);

    return EXIT_SUCCESS;
}

static gint
g_paste_list_histories (Context *ctx,
                        GError **error)
{
    g_autolist (GPasteClientHistory) histories = g_paste_client_list_histories_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    /* The names alone: this is what a script enumerating histories reads, and
     * the sizes the listing now carries have gpaste-client history-size and the
     * graphical panel for a home. */
    for (const GList *h = histories; h; h = h->next)
        printf ("%s\n", g_paste_client_history_get_name (h->data));

    return EXIT_SUCCESS;
}

static gint
g_paste_delete_history (Context *ctx,
                        GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_delete_history_sync (ctx->client, name, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_preferences (Context *ctx   G_GNUC_UNUSED,
                     GError **error G_GNUC_UNUSED)
{
    return spawn ("Preferences");
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
    g_paste_client_set_active_sync (ctx->client, TRUE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_stop (Context *ctx,
              GError **error)
{
    g_paste_client_set_active_sync (ctx->client, FALSE, error);

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

    if (!g_utf8_validate (data, -1, NULL))
    {
        g_critical (_("Cannot add non-UTF-8 data as text."));
        return EXIT_FAILURE;
    }

    g_autofree gchar *uuid = g_paste_client_add_text_sync (ctx->client, data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_add_password (Context *ctx,
                      GError **error)
{
    const gchar *data = (ctx->argc > 1) ? ctx->args[1] : ctx->pipe_data;

    if (!data)
        return EXIT_FAILURE;

    g_autofree gchar *uuid = g_paste_client_add_password_sync (ctx->client, ctx->args[0], data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_backup_history (Context *ctx,
                        GError **error)
{
    guint64 index = 0;
    g_autofree gchar *name = (ctx->argc > 1) ? g_strdup (ctx->args[index++]) : current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_backup_history_sync (ctx->client, name, ctx->args[index], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_delete (Context *ctx,
                GError **error)
{
    g_paste_client_delete_item_sync (ctx->client, ctx->uuid, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_favourite (Context *ctx,
                   GError **error)
{
    g_paste_client_set_favourite_sync (ctx->client, ctx->uuid, TRUE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_unfavourite (Context *ctx,
                     GError **error)
{
    g_paste_client_set_favourite_sync (ctx->client, ctx->uuid, FALSE, error);

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
    g_autofree gchar *uuid = g_paste_client_add_file_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_get (Context *ctx,
             GError **error)
{
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_sync (ctx->client, ctx->uuid, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s", g_paste_client_item_get_value (item));

    return EXIT_SUCCESS;
}

static gint
g_paste_replace (Context *ctx,
                 GError **error)
{
    const gchar *data = (ctx->argc > 1) ? ctx->args[1] : ctx->pipe_data;

    if (!data)
        return EXIT_FAILURE;

    g_autofree gchar *uuid = g_paste_client_replace_sync (ctx->client, ctx->uuid, data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_search (Context *ctx,
                GError **error)
{
    g_autolist (GPasteClientItem) items = g_paste_client_search_sync (ctx->client, ctx->args[0], error);

    if (*error)
        return EXIT_FAILURE;

    /* The rank of the line among the ones printed, not a place in the history:
     * Search answers the matching items, and where each of them sits is what
     * only a listing of the history knows. Which is why this one does renumber
     * under --favourites where the history listing deliberately does not — it
     * is numbering its own output, not naming items. */
    guint index = 0;

    for (const GList *i = items; i; i = i->next)
    {
        GPasteClientItem *item = i->data;

        if (ctx->favourites && !g_paste_client_item_is_favourite (item))
            continue;

        g_autofree gchar *line = g_strdup (g_paste_client_item_get_value (item));
        print_history_line (line, index++, g_paste_client_item_get_uuid (item), ctx);
    }

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
    g_autofree gchar *url = g_paste_client_upload_sync (ctx->client, ctx->uuid, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", url);

    return EXIT_SUCCESS;
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
    g_autofree gchar *uuid = g_paste_client_make_password_sync (ctx->client, ctx->uuid, ctx->args[1], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_merge (Context *ctx,
               GError **error)
{
    g_autofree gchar *uuid = g_paste_client_merge_sync (ctx->client, ctx->decoration, ctx->separator, ctx->args, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*
 * Main
 */

/* Every verb GPaste answers to, in the order --help lists them. Carrying the
 * usage and the description here rather than in a second list is what stops a
 * verb from being added without them; tests/completions checks the shell
 * completions and the man page against it. */
typedef struct
{
    gint         argc;         /* argv count, verb included */
    const gchar *verb;         /* canonical name, NULL for the verb-less forms */
    const gchar *aliases;      /* space separated, or NULL */
    gint         extra_args;
    gboolean     needs_client;
    const gchar *args;         /* what --help shows after the verb */
    const gchar *doc;          /* untranslated */
    gint       (*handler) (Context *ctx,
                           GError **error);
} Command;

static const Command commands[] = {
        /* The verb-less forms, first: a flag with no verb at all, then a piped-in
         * item or, with nothing at all, the history. */
        { 0, NULL, NULL, G_MAXINT, FALSE, NULL, NULL, g_paste_flag_action },
        { 0, NULL, NULL, 0,        TRUE,  NULL, NULL, g_paste_add         },
        { 0, NULL, NULL, 0,        TRUE,  NULL, NULL, g_paste_history     },

        { 1, "history",           "h",               0,        TRUE,  NULL,                    N_ ("print the history with UUIDs"),                                                            g_paste_history },
        { 1, "history-size",      "hs",              0,        TRUE,  NULL,                    N_ ("print the size of the current history"),                                                   g_paste_history_size },
        { 2, "search",            NULL,              0,        TRUE,  "<pattern>",             N_ ("print the items of the history matching <pattern>"),                                       g_paste_search },
        { 1, "get-history",       "gh",              0,        TRUE,  NULL,                    N_ ("get the name of the current history"),                                                     g_paste_get_history },
        { 2, "backup-history",    "bh",              1,        TRUE,  "<name>",                N_ ("back up the current history"),                                                             g_paste_backup_history },
        { 2, "switch-history",    "sh",              0,        TRUE,  "<name>",                N_ ("switch to another history"),                                                               g_paste_switch_history },
        { 1, "delete-history",    "dh",              1,        TRUE,  "<name>",                N_ ("delete a history"),                                                                        g_paste_delete_history },
        { 1, "list-histories",    "lh",              0,        TRUE,  NULL,                    N_ ("list available histories"),                                                                g_paste_list_histories },
        { 1, "add",               "a",               1,        TRUE,  "<text>",                N_ ("set text to clipboard"),                                                                   g_paste_add },
        { 2, "add-password",      "ap",              1,        TRUE,  "<name> <password>",     N_ ("add the <name> / <password> pair to the clipboard"),                                       g_paste_add_password },
        { 3, "rename-password",   "rp",              0,        TRUE,  "<old name> <new name>", N_ ("rename the password"),                                                                     g_paste_rename_password },
        { 2, "get",               "g",               0,        TRUE,  "<uuid>",                N_ ("get the item <uuid> from the history"),                                                    g_paste_get },
        { 2, "select",            "s set",           0,        TRUE,  "<uuid>",                N_ ("set the item <uuid> from the history to the clipboard"),                                   g_paste_select },
        { 2, "replace",           NULL,              1,        TRUE,  "<uuid> <contents>",     N_ ("replace the contents of the item <uuid> from the history with the provided one"),          g_paste_replace },
        { 4, "merge",             "m",               G_MAXINT, TRUE,  "<uuid> … <uuid>",       N_ ("merge the items matching the UUIDs from the history and put the result in the clipboard"), g_paste_merge },
        { 3, "set-password",      "sp",              0,        TRUE,  "<uuid> <name>",         N_ ("set the item <uuid> from the history as a password named <name>"),                         g_paste_set_password },
        { 2, "delete",            "d del rm remove", 0,        TRUE,  "<uuid>",                N_ ("delete item <uuid> from the history"),                                                     g_paste_delete },
        { 2, "favourite",         "fav",             0,        TRUE,  "<uuid>",                N_ ("pin item <uuid> so the history never drops it automatically"),                             g_paste_favourite },
        { 2, "unfavourite",       "unfav",           0,        TRUE,  "<uuid>",                N_ ("unpin item <uuid>, letting the history drop it again"),                                    g_paste_unfavourite },
        { 2, "delete-password",   "dp",              0,        TRUE,  "<name>",                N_ ("delete the password <name> from the history"),                                             g_paste_delete_password },
        { 2, "file",              "f",               0,        TRUE,  "<path>",                N_ ("put the content of the file at <path> into the clipboard"),                                g_paste_file },
        { 1, "empty",             "e",               1,        TRUE,  NULL,                    N_ ("empty the history"),                                                                       g_paste_empty },
        { 1, "start",             "d daemon",        0,        TRUE,  NULL,                    N_ ("start tracking clipboard changes"),                                                        g_paste_start },
        { 1, "stop",              "q quit",          0,        TRUE,  NULL,                    N_ ("stop tracking clipboard changes"),                                                         g_paste_stop },
        { 1, "daemon-reexec",     "dr",              0,        TRUE,  NULL,                    N_ ("re-execute the daemon (after upgrading it, for instance)"),                                g_paste_daemon_reexec },
        { 1, "migrate",           NULL,              0,        TRUE,  NULL,                    N_ ("migrate the history to a different storage backend"),                                      g_paste_migrate },
        { 1, "change-passphrase", NULL,              0,        TRUE,  NULL,                    N_ ("change the passphrase of the encrypted history"),                                          g_paste_change_passphrase },
        { 1, "preferences",       "p settings",      0,        FALSE, NULL,                    N_ ("launch the configuration tool"),                                                           g_paste_preferences },
        { 1, "ui",                NULL,              0,        FALSE, NULL,                    N_ ("launch the graphical tool"),                                                               g_paste_ui },
        { 1, "show-history",      NULL,              0,        TRUE,  NULL,                    N_ ("make the GNOME Shell extension display the history"),                                      g_paste_show_history },
        { 2, "upload",            "u",               0,        TRUE,  "<uuid>",                N_ ("upload the item <uuid> to a pastebin service"),                                            g_paste_upload },
        { 1, "version",           "v",               0,        FALSE, NULL,                    N_ ("display the version"),                                                                     g_paste_version },
        { 1, "daemon-version",    "dv",              0,        TRUE,  NULL,                    N_ ("display the daemon version"),                                                              g_paste_daemon_version },
        { 1, "help",              NULL,              0,        FALSE, NULL,                    N_ ("display this help"),                                                                       g_paste_help },
        { 1, "about",             NULL,              0,        FALSE, NULL,                    N_ ("display the about dialog"),                                                                g_paste_about },
};

/* @verb is the canonical name or any of the aliases. */
static gboolean
command_matches (const Command *command,
                 const gchar   *verb)
{
    if (g_paste_str_equal (verb, command->verb))
        return TRUE;

    if (!command->aliases)
        return FALSE;

    for (const gchar *a = command->aliases; *a; )
    {
        const gchar *end = strchr (a, ' ');
        gsize len = (end) ? (gsize) (end - a) : strlen (a);

        if (!strncmp (verb, a, len) && !verb[len])
            return TRUE;

        a = (end) ? end + 1 : a + len;
    }

    return FALSE;
}

static void
show_help (void)
{
    const char *progname = g_get_prgname ();

    printf (_("Usage:\n"));

    for (guint64 i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        const Command *c = &commands[i];

        if (!c->verb)
            continue;

        /* The history is what running with no verb at all does, so it is shown
         * the way it is actually reached. */
        if (g_paste_str_equal (c->verb, "history"))
            printf ("  %s [%s]: %s\n", progname, c->verb, _(c->doc));
        else if (c->args)
            printf ("  %s %s %s: %s\n", progname, c->verb, _(c->args), _(c->doc));
        else
            printf ("  %s %s: %s\n", progname, c->verb, _(c->doc));
    }

    /* Not a verb: the item comes in on stdin. */
    /* Translators: the metavariable standing for any command the user pipes into GPaste. */
    printf ("  %s | %s: %s\n", _("<command>"), progname, _("copy the output of <command> to the clipboard"));

    printf ("\n");
    printf (_("Convenience options:"));
    printf ("\n");
    /* Translators: help for --favourites */
    printf ("  --favourites: %s\n", _("only display the pinned items"));
    /* Translators: help for --use-index */
    printf ("  --use-index: %s\n", _("use the index of the item instead of its UUID"));

    printf ("\n");
    printf (_("Display options:"));
    printf ("\n");
    /* Translators: help for --oneline */
    printf ("  --oneline: %s\n", _("display each item on only one line"));
    /* Translators: help for --raw */
    printf ("  --raw: %s\n", _("display each item raw (without line numbers)"));
    /* Translators: help for --reverse */
    printf ("  --reverse: %s\n", _("display the items in reverse order"));
    /* Translators: help for --zero */
    printf ("  --zero: %s\n", _("use a NUL character instead of a new line between each item"));

    printf ("\n");
    printf (_("Merge options:"));
    printf ("\n");
    /* Translators: help for --decoration <string> */
    printf ("  --decoration <%s>: %s\n", _("string"), _("add the given decoration to the beginning and the end of each item before merging"));
    /* Translators: help for --separator <string> */
    printf ("  --separator <%s>: %s\n", _("string"), _("add the given separator between each item when merging"));
}

static gint
g_paste_dispatch (gint         argc,
                  const gchar *verb,
                  Context     *ctx,
                  GError     **error)
{
    for (guint64 i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        const Command *c = &commands[i];

        if (argc == c->argc || c->extra_args == G_MAXINT || (argc > c->argc && argc <= (c->argc + c->extra_args)))
        {
            if (argc > 0 && c->verb && !command_matches (c, verb))
                continue;

            if (c->needs_client && !ctx->client)
                return EXIT_FAILURE;

            gint ret = c->handler (ctx, error);
            if (ret >= 0)
                return ret;
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
    /* Every field starts empty, and says so once: a positional list has to be
     * recounted against the struct each time a member is added, and miscounting
     * it seeds a neighbouring flag instead — every field being a gboolean or a
     * pointer, nothing would say so. */
    Context ctx = { 0 };
    gint status = EXIT_SUCCESS;

    if (parse_cmdline (&argc, &argv, &ctx))
    {
        g_autoptr (GPasteClient) client = ctx.client = g_paste_client_new_sync (&error);
        g_autofree gchar *pipe_data = ctx.pipe_data = extract_pipe_data ();
        g_autofree gchar *uuid = NULL;

        /* Failing to reach the daemon is not fatal for every verb: "help",
         * "version" and the launchers are marked as needing no client. Move that
         * failure out of @error rather than carrying it into what follows: every
         * client call opens with g_return_val_if_fail (!error || !(*error)), so a
         * live one would trip the assert of whatever runs next. */
        g_autoptr (GError) connect_error = (ctx.client) ? NULL : g_steal_pointer (&error);

        /* Only worth resolving the index while there is a daemon to resolve it
         * in: without one this would hand a %NULL client to a call that refuses
         * it, and every verb that could use the uuid needs a client anyway. */
        if (ctx.client && ctx.use_index && ctx.argc > 0)
        {
            g_autoptr (GPasteClientItem) item = g_paste_client_get_item_at_index_sync (ctx.client, g_ascii_strtoull (ctx.args[0], NULL, 10), &error);

            if (!error)
                ctx.uuid = uuid = g_strdup (g_paste_client_item_get_uuid (item));
        }
        else
            ctx.uuid = ctx.args[0];

        /* Only a failure of our own lookup above (which did need a client) skips
         * the dispatch; a missing daemon does not, since the client-less verbs
         * still have work to do. */
        if (!error)
            status = g_paste_dispatch (argc, (argc > 0) ? argv[0] : NULL, &ctx, &error);

        /* Nothing ran, or what ran needed the daemon we never reached: now the
         * connection failure is the one worth reporting. */
        if (!error && status != EXIT_SUCCESS && connect_error)
            error = g_steal_pointer (&connect_error);

        if (error)
        {
            g_critical ("%s\n", error->message);
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
