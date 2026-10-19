// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-prompt-adw.h"

#include <gpaste-3/gpaste-util.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

/* The libadwaita half of the prompt contract: everything here is widgets and
 * the answers they produce. What to do with those answers — which passphrase
 * unlocks what, what gets imported, when the keyring is written — stays in
 * gpaste-storage-migration.c, which never learns that any of this exists. */

struct _GPastePromptAdw
{
    GObject parent_instance;

    /* Anchors the dialogs. Owned by main(), which outlives us. */
    GtkApplication *application;
};

static void g_paste_prompt_adw_prompt_iface_init (GPastePromptInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (PromptAdw, prompt_adw, G_TYPE_OBJECT,
                                    G_PASTE_TYPE_PROMPT, g_paste_prompt_adw_prompt_iface_init)

/* Every text these dialogs show is plain -- the prompt's own wording in any
 * translation, a GError's message naming histories and paths -- while
 * libadwaita parses Pango markup in a group's title and description and in a
 * row's title and subtitle: one "&" or "<" and the label goes blank. The rows
 * and the banner are told their text is not markup; a group, which has no
 * such switch, is handed its text escaped. */
static void
set_group_text (GtkWidget   *group,
                const gchar *title,
                const gchar *description)
{
    if (title)
    {
        g_autofree gchar *escaped = g_markup_escape_text (title, -1);

        adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), escaped);
    }

    if (description)
    {
        g_autofree gchar *escaped = g_markup_escape_text (description, -1);

        adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), escaped);
    }
}

static void
set_row_title (GtkWidget   *row,
               const gchar *title)
{
    adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
}

/*
 * The migration dialog
 */

typedef struct
{
    GPastePromptRequest *request;

    GtkWindow           *window;
    GtkWidget           *apply;
    /* One radio per backend offered, in display order. */
    GtkCheckButton     **backend_checks;
    AdwSwitchRow        *import_row;
    AdwSwitchRow        *cleanup_row;
    AdwBanner           *warning;

    GPasteStorage        current;

    /* The backends offered, in display order. */
    const GPasteStorage *backends;
    guint                n_backends;
} MigrationDialog;

/* Answering from "destroy" is what makes every way of losing the window an
 * answer: gtk_window_destroy() does not emit close-request, and neither does the
 * GtkApplication shutdown that tears the last window down when the daemon loses
 * its D-Bus name — so a dialog up at that moment would never answer, and the
 * caller's `done` would never run. Replying already claimed the request, so this
 * is a no-op on that path.
 *
 * "destroy" and not the teardown below: a destroy notify runs from finalize, so
 * anything still holding a reference on the window at that point (an animation,
 * an a11y peer, a queued frame callback) would defer the answer for as long as
 * it holds it — with the whole daemon startup waiting on it. */
static void
migration_dialog_dismiss (GtkWindow *window G_GNUC_UNUSED,
                          gpointer   user_data)
{
    MigrationDialog *self = user_data;

    g_paste_prompt_request_dismiss (self->request);
}

/* The widgets that carry @self are gone by now, so nothing can reach it again. */
static void
migration_dialog_free (gpointer data)
{
    MigrationDialog *self = data;

    g_clear_object (&self->request);
    g_free (self->backend_checks);
    g_free (self);
}

/* Which backend is picked, into @chosen. None is only until the user picks one,
 * when the backend in use is not on the list: nothing is picked for them. */
static gboolean
chosen_backend (MigrationDialog *self,
                GPasteStorage   *chosen)
{
    for (guint i = 0; i < self->n_backends; ++i)
    {
        if (gtk_check_button_get_active (self->backend_checks[i]))
        {
            *chosen = self->backends[i];
            return TRUE;
        }
    }

    return FALSE;
}

static void
update_state (MigrationDialog *self)
{
    GPasteStorage chosen = G_PASTE_STORAGE_NOOP;
    gboolean chosen_anything = chosen_backend (self, &chosen);
    gboolean backend_changes = chosen_anything && g_paste_prompt_backend_changes (self->current, chosen);
    gboolean import_possible = chosen_anything && g_paste_prompt_can_import (self->current, chosen);

    /* Nothing picked yet, which only the no-preselection case above can mean:
     * there is no answer to apply until the user names a backend. */
    gtk_widget_set_sensitive (self->apply, chosen_anything);

    gtk_widget_set_sensitive (GTK_WIDGET (self->import_row), import_possible);
    if (!import_possible)
        adw_switch_row_set_active (self->import_row, FALSE);

    gtk_widget_set_sensitive (GTK_WIDGET (self->cleanup_row), backend_changes);
    if (!backend_changes)
        adw_switch_row_set_active (self->cleanup_row, FALSE);

    /* Deleting the old data without importing it first throws it away. */
    adw_banner_set_revealed (self->warning,
                             adw_switch_row_get_active (self->cleanup_row) &&
                             !adw_switch_row_get_active (self->import_row));
}

static void
on_state_changed (GObject    *object G_GNUC_UNUSED,
                  GParamSpec *pspec  G_GNUC_UNUSED,
                  gpointer    user_data)
{
    update_state (user_data);
}

/* A radio of the group activated, and only that: picking another first
 * unpicks the one before, and the moment in between, nothing picked, would
 * turn both switches off for good. */
static void
on_backend_toggled (GObject    *object,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
    if (gtk_check_button_get_active (GTK_CHECK_BUTTON (object)))
        on_state_changed (object, pspec, user_data);
}

static void
on_apply (GtkButton *button G_GNUC_UNUSED,
          gpointer   user_data)
{
    MigrationDialog *self = user_data;
    GPasteStorage chosen;

    /* Insensitive until a backend is picked (update_state ()). */
    if (!chosen_backend (self, &chosen))
        return;

    g_paste_prompt_request_reply_migration (self->request,
                                            chosen,
                                            adw_switch_row_get_active (self->import_row),
                                            adw_switch_row_get_active (self->cleanup_row));

    gtk_window_destroy (self->window);
}

/* "Nothing changes, and stop asking": the backend in use, no import, no
 * cleanup. A reply rather than the dismissal it sits next to, which is the whole
 * point -- a dismissal leaves the revision alone and the dialog comes back on
 * the next start. Deliberately answers with the *current* backend rather than
 * whichever is picked: this button undoes the fiddling too. */
static void
on_keep (GtkButton *button G_GNUC_UNUSED,
         gpointer   user_data)
{
    MigrationDialog *self = user_data;

    g_paste_prompt_request_reply_migration (self->request, self->current, FALSE, FALSE);

    gtk_window_destroy (self->window);
}

/* Escape closes the dialog like its close button; the teardown above is what
 * turns that into a dismissal. */
static gboolean
on_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                guint                  keyval,
                guint                  keycode    G_GNUC_UNUSED,
                GdkModifierType        state      G_GNUC_UNUSED,
                gpointer               user_data)
{
    MigrationDialog *self = user_data;

    if (keyval != GDK_KEY_Escape)
        return GDK_EVENT_PROPAGATE;

    gtk_window_close (self->window);
    return GDK_EVENT_STOP;
}

static void
g_paste_prompt_adw_migration (GPastePrompt        *prompt,
                              GPastePromptRequest *request)
{
    MigrationDialog *self = g_new0 (MigrationDialog, 1);

    self->request = g_object_ref (request);
    self->current = g_paste_prompt_request_get_current (request);
    self->backends = g_paste_prompt_request_get_offered (request, &self->n_backends);

    GtkWidget *window = adw_application_window_new (G_PASTE_PROMPT_ADW (prompt)->application);

    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_TITLE));
    gtk_window_set_icon_name (self->window, G_PASTE_ICON_NAME);
    gtk_window_set_default_size (self->window, 480, -1);
    gtk_window_set_modal (self->window, TRUE);

    GtkWidget *apply = self->apply = gtk_button_new_with_label (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_ACCEPT));

    gtk_widget_add_css_class (apply, "suggested-action");
    g_signal_connect (apply, "clicked", G_CALLBACK (on_apply), self);

    GtkWidget *header = adw_header_bar_new ();

    /* Only when there is something to keep: the button answers with the backend
     * in use, and one this build cannot construct is not an answer the reply
     * would accept -- it would be turned into the very dismissal the button
     * exists to avoid. Offering nothing there is the honest version of that. */
    if (g_paste_prompt_request_can_keep (self->request))
    {
        GtkWidget *keep = gtk_button_new_with_label (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_MIGRATION_KEEP));

        g_signal_connect (keep, "clicked", G_CALLBACK (on_keep), self);

        /* Where a cancel button would sit, because that is where someone who
         * wants out of this dialog looks -- and getting out without losing the
         * history to a backend they never chose is what they want. */
        adw_header_bar_pack_start (ADW_HEADER_BAR (header), keep);
    }

    adw_header_bar_pack_end (ADW_HEADER_BAR (header), apply);

    GtkWidget *warning = adw_banner_new (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_CLEANUP_WARNING));

    /* Said, though libadwaita 1.10 starts a banner with markup off, against its
     * own property's default of on, which a later release may honour: no case
     * here can tell the two apart. */
    adw_banner_set_use_markup (ADW_BANNER (warning), FALSE);
    self->warning = ADW_BANNER (warning);

    GtkWidget *backends = adw_preferences_group_new ();

    set_group_text (backends,
                    g_paste_prompt_text (G_PASTE_PROMPT_TEXT_STORAGE_BACKEND),
                    g_paste_prompt_text (g_paste_prompt_request_can_keep (self->request) ? G_PASTE_PROMPT_TEXT_MIGRATION_DESCRIPTION
                                                                                         : G_PASTE_PROMPT_TEXT_MIGRATION_DESCRIPTION_UNKEPT));

    /* Radios rather than a combo, as the shell prompt's exclusive checkboxes
     * are: no preselection when the backend in use is not on the list, since
     * picking one for the user would have the dialog claim a backend they never
     * chose, and the first is as wrong a claim as any -- which an AdwComboRow,
     * always selecting something, cannot leave alone. Nothing is picked until
     * they say so, and Apply stays insensitive until they do. */
    self->backend_checks = g_new0 (GtkCheckButton *, self->n_backends);

    for (guint i = 0; i < self->n_backends; ++i)
    {
        GtkWidget *row = adw_action_row_new ();
        GtkWidget *check = gtk_check_button_new ();

        self->backend_checks[i] = GTK_CHECK_BUTTON (check);
        set_row_title (row, g_paste_prompt_storage_label (self->backends[i]));
        gtk_check_button_set_group (self->backend_checks[i], (i) ? self->backend_checks[0] : NULL);
        gtk_check_button_set_active (self->backend_checks[i], self->backends[i] == self->current);
        adw_action_row_add_prefix (ADW_ACTION_ROW (row), check);
        adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), check);
        g_signal_connect (check, "notify::active", G_CALLBACK (on_backend_toggled), self);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (backends), row);
    }

    GtkWidget *import_row = adw_switch_row_new ();

    self->import_row = ADW_SWITCH_ROW (import_row);
    set_row_title (import_row, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_IMPORT));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (import_row), g_paste_prompt_text (G_PASTE_PROMPT_TEXT_IMPORT_SUBTITLE));

    GtkWidget *cleanup_row = adw_switch_row_new ();

    self->cleanup_row = ADW_SWITCH_ROW (cleanup_row);
    set_row_title (cleanup_row, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_CLEANUP));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (cleanup_row), g_paste_prompt_text (G_PASTE_PROMPT_TEXT_CLEANUP_SUBTITLE));

    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), import_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), cleanup_row);

    GtkWidget *page = adw_preferences_page_new ();

    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (backends));
    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_append (GTK_BOX (content), warning);
    gtk_box_append (GTK_BOX (content), page);

    GtkWidget *toolbar = adw_toolbar_view_new ();

    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), content);

    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);

    g_signal_connect (import_row, "notify::active", G_CALLBACK (on_state_changed), self);
    g_signal_connect (cleanup_row, "notify::active", G_CALLBACK (on_state_changed), self);
    g_signal_connect (window, "destroy", G_CALLBACK (migration_dialog_dismiss), self);
    g_object_set_data_full (G_OBJECT (window), "gpaste-migration-dialog", self, migration_dialog_free);

    GtkEventController *key_controller = gtk_event_controller_key_new ();

    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (window, key_controller);

    update_state (self);

    gtk_window_present (self->window);
}

/*
 * The passphrase prompt
 */

typedef struct
{
    GPastePromptRequest *request;

    GtkWindow           *window;
    GtkEditable         *entry;
    GtkEditable         *confirm_entry;
    GtkWidget           *remember;
    /* Whether the keyring holds anything, i.e. whether an off switch is a
     * request to forget rather than just an absence. Not where the switch
     * started: a retry after a mistyped passphrase starts it off having
     * carried an explicit "forget" forward, and the two must not be confused. */
    gboolean             remembered;
    GtkWidget           *ok;

    /* Only built when setting a new passphrase (confirm): the row carrying the
     * strength meter and its rating/hint. */
    GtkWidget           *strength_row;
} PassphraseDialog;

/* See migration_dialog_dismiss(): answering from "destroy" is what makes every
 * way of losing the window an answer, without waiting on finalize. */
static void
passphrase_dialog_dismiss (GtkWindow *window G_GNUC_UNUSED,
                           gpointer   user_data)
{
    PassphraseDialog *self = user_data;

    g_paste_prompt_request_dismiss (self->request);
}

static void
passphrase_dialog_free (gpointer data)
{
    PassphraseDialog *self = data;

    g_clear_object (&self->request);
    g_free (self);
}

static void
passphrase_update_ok (PassphraseDialog *self)
{
    /* The confirmation entry only exists when one is being asked for, which is
     * exactly what the shared rule wants told apart. */
    gtk_widget_set_sensitive (self->ok,
                              g_paste_prompt_passphrase_is_complete (gtk_editable_get_text (self->entry),
                                                                     self->confirm_entry
                                                                     ? gtk_editable_get_text (self->confirm_entry)
                                                                     : NULL));
}

static void
on_passphrase_changed (GtkEditable *editable,
                       gpointer     user_data)
{
    PassphraseDialog *self = user_data;

    /* The red hint flags the previous wrong attempt; clear it as soon as the
     * user amends the passphrase so it does not bleed into the next try. */
    gtk_widget_remove_css_class (GTK_WIDGET (self->entry), "error");

    /* Reflect the strength of the new passphrase as it is typed — but only when
     * it is the passphrase that changed. Typing the confirmation re-rates a
     * string that did not move, and rating means a cracklib dictionary pass. */
    if (self->strength_row && editable == self->entry)
        g_paste_gtk_util_password_strength_row_rate (self->strength_row, self->entry);

    passphrase_update_ok (self);
}

static void on_passphrase_ok (GtkButton *button,
                              gpointer   user_data);

/* Enter is only an answer once the prompt would accept one, which is exactly
 * what the OK button's sensitivity tracks. */
static void
on_passphrase_activated (AdwEntryRow *row G_GNUC_UNUSED,
                         gpointer     user_data)
{
    PassphraseDialog *self = user_data;

    if (gtk_widget_get_sensitive (self->ok))
        on_passphrase_ok (NULL, self);
}

static void
on_passphrase_ok (GtkButton *button G_GNUC_UNUSED,
                  gpointer   user_data)
{
    PassphraseDialog *self = user_data;
    GPasteStorageRemember remember = G_PASTE_STORAGE_REMEMBER_UNCHANGED;

    /* Only report the choice: the keyring is written by whoever established that
     * this passphrase is the right one, which cannot be known from here. */
    if (self->remember && adw_switch_row_get_active (ADW_SWITCH_ROW (self->remember)))
        remember = G_PASTE_STORAGE_REMEMBER_YES;
    else if (self->remembered)
        remember = G_PASTE_STORAGE_REMEMBER_NO;

    /* Reply while the entry text is still alive; the reply copies it. */
    g_paste_prompt_request_reply_passphrase (self->request, gtk_editable_get_text (self->entry), remember);
    gtk_window_destroy (self->window);
}

/* Escape closes the prompt like its close button; the teardown is what turns
 * that into a dismissal. */
static gboolean
on_passphrase_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                           guint                  keyval,
                           guint                  keycode    G_GNUC_UNUSED,
                           GdkModifierType        state      G_GNUC_UNUSED,
                           gpointer               user_data)
{
    PassphraseDialog *self = user_data;

    if (keyval != GDK_KEY_Escape)
        return GDK_EVENT_PROPAGATE;

    gtk_window_close (self->window);
    return GDK_EVENT_STOP;
}

static void
g_paste_prompt_adw_passphrase (GPastePrompt        *prompt,
                               GPastePromptRequest *request)
{
    PassphraseDialog *self = g_new0 (PassphraseDialog, 1);
    gboolean confirm = g_paste_prompt_request_get_confirm (request);
    const gchar *error_message = g_paste_prompt_request_get_error_message (request);

    self->request = g_object_ref (request);

    GtkWidget *window = adw_application_window_new (G_PASTE_PROMPT_ADW (prompt)->application);

    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_TITLE));
    gtk_window_set_icon_name (self->window, G_PASTE_ICON_NAME);
    gtk_window_set_default_size (self->window, 420, -1);
    gtk_window_set_modal (self->window, TRUE);

    GtkWidget *ok = gtk_button_new_with_label (g_paste_prompt_text (confirm ? G_PASTE_PROMPT_TEXT_PASSPHRASE_CONFIRM_ACCEPT : G_PASTE_PROMPT_TEXT_PASSPHRASE_ACCEPT));

    self->ok = ok;
    gtk_widget_add_css_class (ok, "suggested-action");
    gtk_widget_set_sensitive (ok, FALSE);
    g_signal_connect (ok, "clicked", G_CALLBACK (on_passphrase_ok), self);

    GtkWidget *header = adw_header_bar_new ();

    adw_header_bar_pack_end (ADW_HEADER_BAR (header), ok);

    GtkWidget *entry = adw_password_entry_row_new ();

    self->entry = GTK_EDITABLE (entry);
    set_row_title (entry, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE));
    g_signal_connect (entry, "changed", G_CALLBACK (on_passphrase_changed), self);
    /* Enter submits, as it does in the shell prompt: an unlock that only takes
     * the mouse is an unlock the daemon waits on for no reason. */
    g_signal_connect (entry, "entry-activated", G_CALLBACK (on_passphrase_activated), self);

    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), entry);

    /* Re-prompt after a wrong passphrase: flag the entry. What went wrong is
     * said in the description below, alongside the standing explanation rather
     * than in place of it. */
    if (error_message)
        gtk_widget_add_css_class (entry, "error");

    if (confirm)
    {
        GtkWidget *confirm_entry = adw_password_entry_row_new ();

        self->confirm_entry = GTK_EDITABLE (confirm_entry);
        set_row_title (confirm_entry, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_CONFIRM));
        g_signal_connect (confirm_entry, "changed", G_CALLBACK (on_passphrase_changed), self);
        /* Enter submits from here too: this is the field the user finishes
         * typing in, and the shell prompt takes it in both of its entries. */
        g_signal_connect (confirm_entry, "entry-activated", G_CALLBACK (on_passphrase_activated), self);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), confirm_entry);

        /* Rate the new passphrase as it is typed. The same row the graphical
         * tool's password composer puts up, so the two cannot come to rate the
         * same string two ways. */
        self->strength_row = g_paste_gtk_util_password_strength_row_new (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_STRENGTH),
                                                                         g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_STRENGTH_UNAVAILABLE));

        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), self->strength_row);
    }

    /* Whatever the prompt has to say, said once. The standing explanation
     * depends on which question is being asked; an error joins it rather than
     * replacing it, and the shell backend shows both the same way. */
    const gchar *standing = confirm
        ? g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_CONFIRM_DESCRIPTION)
        : g_paste_prompt_text (G_PASTE_PROMPT_TEXT_PASSPHRASE_DESCRIPTION);
    g_autofree gchar *description = error_message ? g_strconcat (error_message, "\n\n", standing, NULL)
                                                  : NULL;

    set_group_text (group, NULL, description ? description : standing);

    /* No keyring in this build means nothing to offer to remember it in. */
    if (g_paste_prompt_keyring_available ())
    {
        GtkWidget *remember = adw_switch_row_new ();
        gboolean starts_on;

        self->remember = remember;
        set_row_title (remember, g_paste_prompt_text (G_PASTE_PROMPT_TEXT_REMEMBER));
        adw_action_row_set_subtitle (ADW_ACTION_ROW (remember), g_paste_prompt_text (G_PASTE_PROMPT_TEXT_REMEMBER_SUBTITLE));

        g_paste_prompt_remember_state (g_paste_prompt_request_get_remember (request),
                                       &starts_on, &self->remembered);

        adw_switch_row_set_active (ADW_SWITCH_ROW (remember), starts_on);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), remember);
    }

    GtkWidget *page = adw_preferences_page_new ();

    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *toolbar = adw_toolbar_view_new ();

    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), page);
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);

    g_signal_connect (window, "destroy", G_CALLBACK (passphrase_dialog_dismiss), self);
    g_object_set_data_full (G_OBJECT (window), "gpaste-passphrase-dialog", self, passphrase_dialog_free);

    GtkEventController *key_controller = gtk_event_controller_key_new ();

    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_passphrase_key_pressed), self);
    gtk_widget_add_controller (window, key_controller);

    gtk_window_present (self->window);
}

/*
 * The report
 */

/* Escape closes it like its close button. Nothing is answered either way — a
 * report has no answer — so losing the window needs no teardown of its own. */
static gboolean
on_report_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                       guint                  keyval,
                       guint                  keycode    G_GNUC_UNUSED,
                       GdkModifierType        state      G_GNUC_UNUSED,
                       gpointer               user_data)
{
    if (keyval != GDK_KEY_Escape)
        return GDK_EVENT_PROPAGATE;

    gtk_window_close (user_data);
    return GDK_EVENT_STOP;
}

/* A window rather than an AdwAlertDialog, for the same reason the two prompts
 * above are windows: the daemon has no window to put a dialog inside. */
static void
g_paste_prompt_adw_report (GPastePrompt *prompt,
                           const gchar  *title,
                           const gchar  *message)
{
    GtkWidget *window = adw_application_window_new (G_PASTE_PROMPT_ADW (prompt)->application);

    gtk_window_set_title (GTK_WINDOW (window), title);
    gtk_window_set_icon_name (GTK_WINDOW (window), G_PASTE_ICON_NAME);
    gtk_window_set_default_size (GTK_WINDOW (window), 420, -1);
    gtk_window_set_modal (GTK_WINDOW (window), TRUE);

    GtkWidget *close = gtk_button_new_with_label (g_paste_prompt_text (G_PASTE_PROMPT_TEXT_CLOSE));

    gtk_widget_add_css_class (close, "suggested-action");
    g_signal_connect_swapped (close, "clicked", G_CALLBACK (gtk_window_destroy), window);

    GtkWidget *header = adw_header_bar_new ();

    adw_header_bar_pack_end (ADW_HEADER_BAR (header), close);

    GtkWidget *status = adw_status_page_new ();
    /* The description is Pango markup (the title is not), and @message carries a
     * GError's text: history names the user chose and paths from the filesystem.
     * One "&" in either makes the markup fail to parse, and the window would go
     * up with an empty description -- a report that reports nothing. */
    g_autofree gchar *escaped = g_markup_escape_text (message, -1);

    adw_status_page_set_icon_name (ADW_STATUS_PAGE (status), "dialog-warning-symbolic");
    adw_status_page_set_title (ADW_STATUS_PAGE (status), title);
    adw_status_page_set_description (ADW_STATUS_PAGE (status), escaped);

    GtkWidget *toolbar = adw_toolbar_view_new ();

    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), status);
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);

    GtkEventController *key_controller = gtk_event_controller_key_new ();

    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_report_key_pressed), window);
    gtk_widget_add_controller (window, key_controller);

    gtk_window_present (GTK_WINDOW (window));
}

static void
g_paste_prompt_adw_prompt_iface_init (GPastePromptInterface *iface)
{
    iface->passphrase = g_paste_prompt_adw_passphrase;
    iface->migration = g_paste_prompt_adw_migration;
    iface->report = g_paste_prompt_adw_report;
}

static void
g_paste_prompt_adw_class_init (GPastePromptAdwClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_prompt_adw_init (GPastePromptAdw *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_prompt_adw_new:
 * @application: the #GtkApplication to anchor the dialogs to
 *
 * Create a new libadwaita #GPastePrompt backend.
 *
 * Returns: (transfer full): a newly allocated #GPastePrompt
 *          free it with g_object_unref
 */
GPastePrompt *
g_paste_prompt_adw_new (GtkApplication *application)
{
    g_return_val_if_fail (GTK_IS_APPLICATION (application), NULL);

    GPastePromptAdw *self = g_object_new (G_PASTE_TYPE_PROMPT_ADW, NULL);

    self->application = application;

    return G_PASTE_PROMPT (self);
}
