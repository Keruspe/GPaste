// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-macros.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-daemon/gpaste-storage-migration.h>

/* A thin wrapper around g_paste_storage_migration_prepare: the standalone daemon
 * (gpaste-daemon.c) runs the storage-backend choice / migration dialog and the
 * encrypted-history unlock in-process, but the GNOME Shell extension cannot
 * (gnome-shell never calls gtk_init / adw_init). daemon.js spawns this helper
 * instead and waits for it to finish before loading the history.
 *
 * Deciding whether the migration is needed is the launcher's job: daemon.js
 * checks the stored revision before spawning us, and a user can run this binary
 * by hand to (re)choose a backend at any time. So the dialog is always shown
 * here (force=TRUE) rather than re-checking the revision. */
gint
main (gint argc, gchar *argv[])
{
    G_PASTE_GTK_INIT_APPLICATION ("StorageMigration");

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_paste_storage_migration_prepare (app, settings, TRUE);

    return EXIT_SUCCESS;
}
