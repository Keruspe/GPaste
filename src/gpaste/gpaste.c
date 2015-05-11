/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-config.h>
#include <gpaste-client.h>

#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

static inline gboolean
has_applet(void)
{
    return g_file_test (PKGLIBEXECDIR "/gpaste-applet", G_FILE_TEST_IS_EXECUTABLE);
}

static inline gboolean
has_unity(void)
{
    return g_file_test (PKGLIBEXECDIR "/gpaste-app-indicator", G_FILE_TEST_IS_EXECUTABLE);
}

static void
show_help (void)
{
    const char *progname = g_get_prgname ();

    printf (_("Usage:\n"));
    /* Translators: help for gpaste history */
    printf ("  %s [history]: %s\n", progname, _("print the history with indexes"));
    /* Translators: help for gpaste history-size */
    printf ("  %s history-size: %s\n", progname, _("print the size of the history"));
    /* Translators: help for gpaste backup-history <name> */
    printf ("  %s backup-history <%s>: %s\n", progname, _("name"), _("backup current history"));
    /* Translators: help for gpaste switch-history <name> */
    printf ("  %s switch-history <%s>: %s\n", progname, _("name"), _("switch to another history"));
    /* Translators: help for gpaste delete-history <name> */
    printf ("  %s delete-history <%s>: %s\n", progname, _("name"),  _("delete a history"));
    /* Translators: help for gpaste list-histories */
    printf ("  %s list-histories: %s\n", progname, _("list available histories"));
    /* Translators: help for gpaste oneline-history */
    printf ("  %s oneline-history: %s\n", progname, _("print the history without newlines"));
    /* Translators: help for gpaste raw-history */
    printf ("  %s raw-history: %s\n", progname, _("print the history (raw) without indexes"));
    /* Translators: help for gpaste zero-history */
    printf ("  %s zero-history: %s\n", progname, _("print the history with NUL as separator"));
    /* Translators: help for gpaste add <text> */
    printf ("  %s add <%s>: %s\n", progname, _("text"), _("set text to clipboard"));
    /* Translators: help for gpaste add-password <name> <text> */
    printf ("  %s add-password <%s> <%s>: %s\n", progname, _("name"), _("password"), _("add the name - password couple to the clipboard"));
    /* Translators: help for gpaste rename-password <old name> <new name> */
    printf ("  %s rename-password <%s> <%s>: %s\n", progname, _("old name"), _("new name"), _("rename the password"));
    /* Translators: help for gpaste get <number> */
    printf ("  %s get <%s>: %s\n", progname, _("number"), _("get the <number>th item from the history"));
    /* Translators: help for gpaste get-raw <number> */
    printf ("  %s get-raw <%s>: %s\n", progname, _("number"), _("get the <number>th item from the history (raw)"));
    /* Translators: help for gpaste select <number> */
    printf ("  %s select <%s>: %s\n", progname, _("number"), _("set the <number>th item from the history to the clipboard"));
    /* Translators: help for gpaste set-password <number> <name> */
    printf ("  %s set-password <%s> <%s>: %s\n", progname, _("number"), _("name"), _("set the <number>th item from the history as a password named <name>"));
    /* Translators: help for gpaste delete <number> */
    printf ("  %s delete <%s>: %s\n", progname, _("number"), _("delete <number>th item of the history"));
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
    if (has_applet ())
    {
        /* Translators: help for gpaste applet */
        printf ("  %s applet: %s\n", progname, _("launch the applet"));
    }
    if (has_unity ())
    {
        /* Translators: help for gpaste app-indicator */
        printf ("  %s app-indicator: %s\n", progname, _("launch the unity application indicator"));
    }
    /* Translators: help for gpaste show-history */
    printf ("  %s show-history: %s\n", progname, _("make the applet or extension display the history"));
    /* Translators: help for gpaste version */
    printf ("  %s version: %s\n", progname, _("display the version"));
    /* Translators: help for gpaste daemon-version */
    printf ("  %s daemon-version: %s\n", progname, _("display the daemon version"));
    /* Translators: help for gpaste help */
    printf ("  %s help: %s\n", progname, _("display this help"));
    /* Translators: help for gpaste about */
    printf ("  %s about: %s\n", progname, _("display the about dialog"));

    printf("\n");
    printf(_("Display options:"));
    printf("\n");
    /* Translators: help for --oneline */
    printf("  --oneline: %s\n", _("display each item on only one line"));
    /* Translators: help for --oneline */
    printf("  --raw: %s\n", _("display each item raw (without line numbers)"));
    /* Translators: help for --oneline */
    printf("  --zero: %s\n", _("use a NUL character instead of a new line betweean each item"));
}

static void
show_version (void)
{
    printf ("%s\n", PACKAGE_STRING);
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
print_history_line (gchar   *line,
                    guint32  index,
                    gboolean oneline,
                    gboolean raw,
                    gboolean zero)
{
    if (!raw)
        printf ("%d: ", index);
    printf ("%s%c", (oneline) ? strip_newline (line) : line, (zero) ? '\0' : '\n');
}

static void
show_history (GPasteClient *client,
              gboolean      oneline,
              gboolean      raw,
              gboolean      zero,
              GError      **error)
{
    G_PASTE_CLEANUP_STRFREEV GStrv history = (raw) ?
        g_paste_client_get_raw_history_sync (client, error) :
        g_paste_client_get_history_sync (client, error);

    if (!*error)
    {
        guint32 i = 0;

        for (GStrv h = history; *h; ++h)
            print_history_line (*h, i++, oneline, raw, zero);
    }
}

static inline gboolean
is_version (const gchar *option)
{
    return (!g_strcmp0 (option, "v") ||
            !g_strcmp0 (option, "version"));
}

G_PASTE_NORETURN static void
failure_exit (GError *error)
{
    g_critical ("%s: %s\n", _("Couldn't connect to GPaste daemon"), (error) ? error->message: "unknwown error");
    exit (EXIT_FAILURE);
}

static int
spawn (const gchar *app,
       GError     **error)
{
    G_PASTE_CLEANUP_FREE gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    G_PASTE_CLEANUP_FREE gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);
    G_PASTE_CLEANUP_UNREF GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                             G_DBUS_PROXY_FLAGS_NONE,
                                                                             NULL,
                                                                             name,
                                                                             object,
                                                                             "org.freedesktop.Application",
                                                                             NULL,
                                                                             error);

    if (proxy)
    {
        GVariant *param = g_variant_new ("a{sv}", NULL);
        GVariant *params = g_variant_new_tuple (&param, 1);
        g_dbus_proxy_call_sync (proxy, "Activate", params, G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);
    }

    if (*error)
    {
        g_critical ("%s %s: %s", _("Couldn't spawn"), app, (*error)->message);
        g_clear_error (error);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static guint64
_strtoull (const gchar *str)
{
    return g_ascii_strtoull (str, NULL, 0);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_GETTEXT ();
    g_set_prgname (argv[0]);

    struct option long_options[] = {
        {"help",    no_argument, NULL,  'h' },
        {"oneline", no_argument, NULL,  'o' },
        {"raw"    , no_argument, NULL,  'r' },
        {"version", no_argument, NULL,  'v' },
        {"zero",    no_argument, NULL,  'z' },
        {0,         no_argument, NULL,  0 }
    };

    gboolean help = FALSE, version = FALSE;
    gboolean oneline = FALSE, raw = FALSE, zero = FALSE;
    gint c;

    while ((c = getopt_long(argc, argv, "horvz", long_options, NULL)) != -1)
    {
        switch (c)
        {
        case 'h':
            help = TRUE;
            break;
        case 'o':
            oneline = TRUE;
            break;
        case 'r':
            raw = TRUE;
            break;
        case 'v':
            version = TRUE;
            break;
        case 'z':
            zero = TRUE;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    argv += optind;
    argc -= optind;

    if (help || (argc > 0 && !g_strcmp0 (argv[0], "help")))
    {
        show_help ();
        return EXIT_SUCCESS;
    }
    else if (version || (argc > 0 && is_version (argv[0])))
    {
        show_version ();
        return EXIT_SUCCESS;
    }

    int status = EXIT_SUCCESS;

    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;
    G_PASTE_CLEANUP_UNREF GPasteClient *client = g_paste_client_new_sync (&error);

    if (!client)
        failure_exit (error);

    gboolean was_valid_pipe = FALSE;

    if (!isatty (fileno (stdin)))
    {
        /* We are being piped */
        G_PASTE_CLEANUP_STRING_FREE GString *data = g_string_new ("");

        while ((c = fgetc (stdin)) != EOF)
            data = g_string_append_c (data, (guchar)c);

        data->str[data->len - 1] = '\0';

        if (!argc)
        {
            g_paste_client_add_sync (client, data->str, &error);
            was_valid_pipe = TRUE;
        }
        else if (argc == 2)
        {
            const gchar *arg1 = argv[0];
            const gchar *arg2 = argv[1];

            if (!g_strcmp0 (arg1, "ap") ||
                !g_strcmp0 (arg1, "add-password"))
            {
                g_paste_client_add_password_sync (client, arg2, data->str, &error);
                was_valid_pipe = TRUE;
            }
        }
    }

    if (!was_valid_pipe)
    {
        const gchar *arg1, *arg2, *arg3;
        switch (argc)
        {
        case 0:
            show_history (client, oneline, raw, zero, &error);
            break;
        case 1:
            arg1 = argv[0];
            if (!g_strcmp0 (arg1, "about"))
                g_paste_client_about_sync (client, &error);
            else if (!g_strcmp0 (arg1, "dr") ||
                     !g_strcmp0 (arg1, "daemon-reexec"))
            {
                g_paste_client_reexecute_sync (client, &error);
                if (error && error->code == G_DBUS_ERROR_NO_REPLY)
                {
                    printf (_("Successfully reexecuted the daemon\n"));
                    return EXIT_SUCCESS;
                }
            }
            else if (!g_strcmp0 (arg1, "dv") ||
                     !g_strcmp0 (arg1, "daemon-version"))
            {
                G_PASTE_CLEANUP_FREE gchar *v = g_paste_client_get_version (client);
                printf ("%s\n", v);
            }
            else if (!g_strcmp0 (arg1, "e") ||
                     !g_strcmp0 (arg1, "empty"))
            {
                g_paste_client_empty_sync (client, &error);
            }
            else if (!g_strcmp0 (arg1, "h") ||
                     !g_strcmp0 (arg1, "history"))
            {
                show_history (client, oneline, raw, zero, &error);
            }
            else if (!g_strcmp0 (arg1, "hs") ||
                     !g_strcmp0 (arg1, "history-size"))
            {
                guint32 size = g_paste_client_get_history_size_sync (client, &error);
                if (!error)
                    printf ("%u\n", size);
            }
            else if (!g_strcmp0 (arg1, "lh") ||
                     !g_strcmp0 (arg1, "list-histories"))
            {
                G_PASTE_CLEANUP_STRFREEV GStrv histories = g_paste_client_list_histories_sync (client, &error);
                if (!error)
                {
                    for (GStrv h = histories; *h; ++h)
                        printf ("%s\n", *h);
                }
            }
            else if (!g_strcmp0 (arg1, "oh") ||
                     !g_strcmp0 (arg1, "oneline-history"))
            {
                show_history (client, TRUE, raw, zero, &error);
            }
            else if (!g_strcmp0 (arg1, "rh") ||
                     !g_strcmp0 (arg1, "raw-history"))
            {
                show_history (client, oneline, TRUE, zero, &error);
            }
            else if (!g_strcmp0 (arg1, "settings") ||
                     !g_strcmp0 (arg1, "p")        ||
                     !g_strcmp0 (arg1, "preferences"))
            {
                status = spawn ("Settings", &error);
            }
            else if (!g_strcmp0 (arg1, "show-history"))
            {
                g_paste_client_show_history_sync (client, &error);
            }
            else if (!g_strcmp0 (arg1, "start") ||
                     !g_strcmp0 (arg1, "d")     ||
                     !g_strcmp0 (arg1, "daemon"))
            {
                g_paste_client_track_sync (client, TRUE, &error);
            }
            else if (!g_strcmp0 (arg1, "stop") ||
                     !g_strcmp0 (arg1, "q")    ||
                     !g_strcmp0 (arg1, "quit"))
            {
                g_paste_client_track_sync (client, FALSE, &error);
            }
            else if (!g_strcmp0 (arg1, "zh") ||
                     !g_strcmp0 (arg1, "zero-history"))
            {
                show_history (client, oneline, raw, TRUE, &error);
            }
            else if (has_applet () && !g_strcmp0 (arg1, "applet"))
            {
                status = spawn ("Applet", &error);
            }
            else if (has_unity () && !g_strcmp0 (arg1, "app-indicator"))
            {
                status = spawn ("AppIndicator", &error);
            }
            else
            {
                show_help ();
                status = EXIT_FAILURE;
            }
            break;
        case 2:
            arg1 = argv[0];
            arg2 = argv[1];
            if (!g_strcmp0 (arg1, "a") ||
                !g_strcmp0 (arg1, "add"))
            {
                g_paste_client_add_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "bh") ||
                     !g_strcmp0 (arg1, "backup-history"))
            {
                g_paste_client_backup_history_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "d")      ||
                     !g_strcmp0 (arg1, "del")    ||
                     !g_strcmp0 (arg1, "delete") ||
                     !g_strcmp0 (arg1, "rm")     ||
                     !g_strcmp0 (arg1, "remove"))
            {
                g_paste_client_delete_sync (client, _strtoull (arg2), &error);
            }
            else if (!g_strcmp0 (arg1, "dp") ||
                     !g_strcmp0 (arg1, "delete-password"))
            {
                g_paste_client_delete_password_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "dh") ||
                     !g_strcmp0 (arg1, "delete-history"))
            {
                g_paste_client_delete_history_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "f") ||
                     !g_strcmp0 (arg1, "file"))
            {
                g_paste_client_add_file_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "g") ||
                     !g_strcmp0 (arg1, "get"))
            {
                if (!raw)
                    printf ("%s", g_paste_client_get_element_sync (client, _strtoull (arg2), &error));
                else
                    printf ("%s", g_paste_client_get_raw_element_sync (client, _strtoull (arg2), &error));
            }
            else if (!g_strcmp0 (arg1, "gr") ||
                     !g_strcmp0 (arg1, "get-raw"))
            {
                printf ("%s", g_paste_client_get_raw_element_sync (client, _strtoull (arg2), &error));
            }
            else if (!g_strcmp0 (arg1, "search"))
            {
                gsize hits;
                G_PASTE_CLEANUP_FREE guint32 *results = g_paste_client_search_sync (client, arg2, &hits, &error);

                if (!error)
                {
                    if (hits > 0)
                    {
                        for (gsize i = 0; i < hits; ++i)
                        {
                            guint32 index = results[i];
                            gchar *line = g_paste_client_get_element_sync (client, index, &error);

                            if (error)
                                break;

                            print_history_line (line, index, oneline, raw, zero);
                        }
                    }
                }
            }
            else if (!g_strcmp0 (arg1, "s")   ||
                     !g_strcmp0 (arg1, "set") ||
                     !g_strcmp0 (arg1, "select"))
            {
                g_paste_client_select_sync (client, _strtoull (arg2), &error);
            }
            else if (!g_strcmp0 (arg1, "sh") ||
                     !g_strcmp0 (arg1, "switch-history"))
            {
                g_paste_client_switch_history_sync (client, arg2, &error);
            }
            else
            {
                show_help ();
                status = EXIT_FAILURE;
            }
            break;
        case 3:
            arg1 = argv[0];
            arg2 = argv[1];
            arg3 = argv[2];
            if (!g_strcmp0 (arg1, "ap") ||
                !g_strcmp0 (arg1, "add-password"))
            {
                g_paste_client_add_password_sync (client, arg2, arg3, &error);
            }
            else if (!g_strcmp0 (arg1, "rp") ||
                     !g_strcmp0 (arg1, "rename-password"))
            {
                g_paste_client_rename_password_sync (client, arg2, arg3, &error);
            }
            else if (!g_strcmp0 (arg1, "sp")   ||
                     !g_strcmp0 (arg1, "set-password"))
            {
                g_paste_client_set_password_sync (client, _strtoull (arg2), arg3, &error);
            }
            else
            {
                show_help ();
                status = EXIT_FAILURE;
            }
            break;
        default:
            show_help ();
            status = EXIT_FAILURE;
            break;
        }
    }

    if (error)
        failure_exit (error);

    return status;
}
