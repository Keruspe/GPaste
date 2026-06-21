// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gpaste-gtk4/gpaste-gtk-macros.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

/* The storage-backend choice / migration dialog and the encrypted-history unlock
 * need gtk_init / adw_init, which the GNOME Shell extension cannot run in process
 * (gnome-shell never calls them). daemon.js spawns this helper instead, once per
 * concern:
 *
 *   gpaste-storage migrate   show the migration dialog (always, when asked)
 *   gpaste-storage decrypt   unlock an encrypted history (keyring or prompt)
 *
 * Because the helper runs in its own process, the passphrase it ends up with
 * cannot be shared with gnome-shell through the process-wide global; it is
 * written to stdout instead so daemon.js can set it before loading the history.
 * The standalone daemon (gpaste-daemon.c) runs both concerns in process and
 * needs none of this. */

typedef struct
{
    GApplication   *gapp;
    GPasteSettings *settings;
} StorageContext;

static void
on_storage_done (gpointer user_data)
{
    StorageContext *ctx = user_data;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Hand the unlocked passphrase back to the spawning gnome-shell daemon (which
     * cannot prompt itself); only meaningful while the history stays encrypted. */
    const gchar *passphrase = g_paste_storage_backend_get_passphrase ();

    if (passphrase && g_paste_settings_get_storage_backend (ctx->settings) == G_PASTE_STORAGE_ENCRYPTED_FILE)
    {
        /* Only hand the passphrase back over the pipe the spawning gnome-shell
         * daemon set up to read it. Launched as a desktop action, stdout is the
         * journal, where the cleartext passphrase must never end up. */
        struct stat st;

        if (fstat (STDOUT_FILENO, &st) == 0 && S_ISFIFO (st.st_mode))
        {
            fputs (passphrase, stdout);
            fflush (stdout);
        }
    }
#endif

    /* Release the hold taken in main() so g_application_run() returns. */
    g_application_release (ctx->gapp);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_GTK_INIT_APPLICATION ("Storage");

    /* We write the unlocked passphrase to stdout for the gnome-shell daemon (and
     * only when stdout is its pipe, see on_storage_done). The preferences button
     * spawns us with a pipe it never reads; ignore SIGPIPE so a write is harmless. */
    signal (SIGPIPE, SIG_IGN);

    const gchar *command = (argc > 1) ? argv[1] : "migrate";

    if (!g_paste_str_equal (command, "migrate") && !g_paste_str_equal (command, "decrypt"))
    {
        fprintf (stderr, "Usage: %s [migrate|decrypt]\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    StorageContext ctx = { .gapp = gapp, .settings = settings };

    /* Registering the application above already initialised libadwaita (through
     * AdwApplication's startup), so the dialog can be shown right away; it is
     * processed once g_application_run() spins the loop. Hold the application
     * across the async dialog since the helper has no lasting window. */
    g_application_hold (gapp);

    /* "migrate" always shows the dialog; "decrypt" only prompts when the keyring
     * did not already unlock the history (decryption_needed() applies it as a
     * side effect), otherwise there is nothing to do. */
    if (g_paste_str_equal (command, "migrate"))
        g_paste_storage_migration_show (app, settings, on_storage_done, &ctx);
    else if (g_paste_storage_decryption_needed (settings))
        g_paste_storage_decryption_show (app, settings, on_storage_done, &ctx);
    else
        on_storage_done (&ctx);

    /* No command line: GApplication would otherwise mistake the subcommand for a
     * file to open ("This application can not open files"). */
    return g_application_run (gapp, 0, NULL);
}
