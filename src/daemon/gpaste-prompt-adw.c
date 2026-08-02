// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-prompt-adw.h"

#include <gpaste/gpaste-util.h>

/* The libadwaita half of the prompt contract: everything here is widgets and
 * the answers they produce. What to do with those answers — which passphrase
 * unlocks what, what gets imported, when the keyring is written — stays in
 * gpaste-storage-migration.c, which never learns that any of this exists. */

struct _GPastePromptAdw
{
    GObject parent_instance;
};

typedef struct
{
    /* Anchors the dialogs. Owned by main(), which outlives us. */
    GtkApplication *application;
} GPastePromptAdwPrivate;

static void g_paste_prompt_adw_prompt_iface_init (GPastePromptInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (PromptAdw, prompt_adw, G_TYPE_OBJECT,
                                                G_PASTE_TYPE_PROMPT, g_paste_prompt_adw_prompt_iface_init)

/*
 * The migration dialog
 */

typedef struct
{
    GPastePromptRequest *request;

    GtkWindow           *window;
    AdwComboRow         *backend_row;
    AdwSwitchRow        *import_row;
    AdwSwitchRow        *cleanup_row;
    AdwBanner           *warning;

    GPasteStorage        current;

    /* The backends offered by the combo, in display order. */
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
    g_free (self);
}

/* The combo lists the backends in a built-at-runtime order; map both ways. */
static GPasteStorage
backend_for_index (MigrationDialog *self,
                   guint            index)
{
    return (index < self->n_backends) ? self->backends[index] : G_PASTE_STORAGE_NOOP;
}

static guint
index_for_backend (MigrationDialog *self,
                   GPasteStorage    storage_kind)
{
    for (guint i = 0; i < self->n_backends; ++i)
    {
        if (self->backends[i] == storage_kind)
            return i;
    }

    return 0;
}

static void
update_state (MigrationDialog *self)
{
    GPasteStorage chosen = backend_for_index (self, adw_combo_row_get_selected (self->backend_row));
    gboolean backend_changes = g_paste_prompt_backend_changes (self->current, chosen);
    gboolean import_possible = g_paste_prompt_can_import (self->current, chosen);

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

/* AdwComboRow ellipsizes the backend labels — both the dropdown rows and the
 * GtkInscription previewing the current selection — and offers no property to
 * stop it. Give it our own factory of plain GtkLabels instead, which it uses for
 * the dropdown rows and the selected-value preview alike, so the longer backend
 * names stay readable. */
static void
backend_label_setup (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                     GtkListItem              *item,
                     gpointer                  user_data G_GNUC_UNUSED)
{
    GtkWidget *label = gtk_label_new (NULL);

    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_list_item_set_child (item, label);
}

static void
backend_label_bind (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                    GtkListItem              *item,
                    gpointer                  user_data G_GNUC_UNUSED)
{
    GtkStringObject *string = gtk_list_item_get_item (item);

    gtk_label_set_label (GTK_LABEL (gtk_list_item_get_child (item)), gtk_string_object_get_string (string));
}

static void
on_apply (GtkButton *button G_GNUC_UNUSED,
          gpointer   user_data)
{
    MigrationDialog *self = user_data;

    g_paste_prompt_request_reply_migration (self->request,
                                            backend_for_index (self, adw_combo_row_get_selected (self->backend_row)),
                                            adw_switch_row_get_active (self->import_row),
                                            adw_switch_row_get_active (self->cleanup_row));

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
    const GPastePromptAdwPrivate *priv = _g_paste_prompt_adw_get_instance_private (G_PASTE_PROMPT_ADW (prompt));
    MigrationDialog *self = g_new0 (MigrationDialog, 1);

    self->request = g_object_ref (request);
    self->current = g_paste_prompt_request_get_current (request);
    self->backends = g_paste_prompt_request_get_offered (request, &self->n_backends);

    GtkWidget *window = adw_application_window_new (priv->application);

    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, _("Storage migration"));
    gtk_window_set_icon_name (self->window, G_PASTE_ICON_NAME);
    gtk_window_set_default_size (self->window, 480, -1);
    gtk_window_set_modal (self->window, TRUE);

    GtkWidget *apply = gtk_button_new_with_label (_("Apply"));

    gtk_widget_add_css_class (apply, "suggested-action");
    g_signal_connect (apply, "clicked", G_CALLBACK (on_apply), self);

    GtkWidget *header = adw_header_bar_new ();

    adw_header_bar_pack_end (ADW_HEADER_BAR (header), apply);

    GtkWidget *warning = adw_banner_new (_("The old data will be deleted without being imported first"));

    self->warning = ADW_BANNER (warning);

    g_autoptr (GtkStringList) backends = gtk_string_list_new (NULL);

    for (guint i = 0; i < self->n_backends; ++i)
        gtk_string_list_append (backends, g_paste_prompt_storage_label (self->backends[i]));

    GtkWidget *backend_row = adw_combo_row_new ();

    self->backend_row = ADW_COMBO_ROW (backend_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (backend_row), _("Storage backend"));
    adw_combo_row_set_model (self->backend_row, G_LIST_MODEL (backends));

    adw_combo_row_set_selected (self->backend_row, index_for_backend (self, self->current));

    g_autoptr (GtkListItemFactory) factory = gtk_signal_list_item_factory_new ();

    g_signal_connect (factory, "setup", G_CALLBACK (backend_label_setup), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (backend_label_bind), NULL);
    adw_combo_row_set_factory (self->backend_row, factory);

    GtkWidget *import_row = adw_switch_row_new ();

    self->import_row = ADW_SWITCH_ROW (import_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (import_row), _("Import existing data"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (import_row), _("Copy the current history into the new backend"));

    GtkWidget *cleanup_row = adw_switch_row_new ();

    self->cleanup_row = ADW_SWITCH_ROW (cleanup_row);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (cleanup_row), _("Delete old data afterwards"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (cleanup_row), _("Remove the previous on-disk history once done"));

    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                           _("Choose where GPaste should store your clipboard history. "
                                             "Nothing is kept on disk unless you pick a storing backend here."));
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), backend_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), import_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), cleanup_row);

    GtkWidget *page = adw_preferences_page_new ();

    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_append (GTK_BOX (content), warning);
    gtk_box_append (GTK_BOX (content), page);

    GtkWidget *toolbar = adw_toolbar_view_new ();

    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), content);

    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);

    g_signal_connect (backend_row, "notify::selected", G_CALLBACK (on_state_changed), self);
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

    /* Only built when setting a new passphrase (confirm): the strength meter and
     * the row carrying its rating/hint. */
    GtkLevelBar         *strength;
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
    if (self->strength && editable == self->entry)
    {
        g_autofree gchar *hint = NULL;
        guint strength = g_paste_prompt_passphrase_strength (gtk_editable_get_text (self->entry), &hint);

        gtk_level_bar_set_value (self->strength, strength);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->strength_row), hint ? hint : "");
    }

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
    const GPastePromptAdwPrivate *priv = _g_paste_prompt_adw_get_instance_private (G_PASTE_PROMPT_ADW (prompt));
    PassphraseDialog *self = g_new0 (PassphraseDialog, 1);
    gboolean confirm = g_paste_prompt_request_get_confirm (request);
    const gchar *error_message = g_paste_prompt_request_get_error_message (request);

    self->request = g_object_ref (request);

    GtkWidget *window = adw_application_window_new (priv->application);

    self->window = GTK_WINDOW (window);
    gtk_window_set_title (self->window, _("Encrypted history"));
    gtk_window_set_icon_name (self->window, G_PASTE_ICON_NAME);
    gtk_window_set_default_size (self->window, 420, -1);
    gtk_window_set_modal (self->window, TRUE);

    GtkWidget *ok = gtk_button_new_with_label (confirm ? _("Set passphrase") : _("Unlock"));

    self->ok = ok;
    gtk_widget_add_css_class (ok, "suggested-action");
    gtk_widget_set_sensitive (ok, FALSE);
    g_signal_connect (ok, "clicked", G_CALLBACK (on_passphrase_ok), self);

    GtkWidget *header = adw_header_bar_new ();

    adw_header_bar_pack_end (ADW_HEADER_BAR (header), ok);

    GtkWidget *entry = adw_password_entry_row_new ();

    self->entry = GTK_EDITABLE (entry);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (entry), _("Passphrase"));
    g_signal_connect (entry, "changed", G_CALLBACK (on_passphrase_changed), self);
    /* Enter submits, as it does in the shell prompt: an unlock that only takes
     * the mouse is an unlock the daemon waits on for no reason. */
    g_signal_connect (entry, "entry-activated", G_CALLBACK (on_passphrase_activated), self);

    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), entry);

    /* Re-prompt after a wrong passphrase: flag the entry. What went wrong is
     * said in the description below, alongside rather than instead of the
     * standing explanation — a confirmed prompt used to overwrite it. */
    if (error_message)
        gtk_widget_add_css_class (entry, "error");

    if (confirm)
    {
        GtkWidget *confirm_entry = adw_password_entry_row_new ();

        self->confirm_entry = GTK_EDITABLE (confirm_entry);
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (confirm_entry), _("Confirm passphrase"));
        g_signal_connect (confirm_entry, "changed", G_CALLBACK (on_passphrase_changed), self);
        /* Enter submits from here too: this is the field the user finishes
         * typing in, and the shell prompt takes it in both of its entries. */
        g_signal_connect (confirm_entry, "entry-activated", G_CALLBACK (on_passphrase_activated), self);
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), confirm_entry);

        GtkWidget *strength_row = adw_action_row_new ();

        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (strength_row), _("Passphrase strength"));

#ifdef G_PASTE_ENABLE_PWQUALITY
        /* Rate the new passphrase as it is typed, with the rating or
         * libpwquality's advice as the subtitle and a colour-graded meter. */
        self->strength_row = strength_row;

        GtkWidget *strength = gtk_level_bar_new ();

        self->strength = GTK_LEVEL_BAR (strength);
        gtk_level_bar_set_min_value (self->strength, 0);
        gtk_level_bar_set_max_value (self->strength, G_PASTE_PROMPT_STRENGTH_MAX);
        /* Colour the meter red → orange → green as the rating climbs. */
        gtk_level_bar_add_offset_value (self->strength, GTK_LEVEL_BAR_OFFSET_LOW, 1);
        gtk_level_bar_add_offset_value (self->strength, GTK_LEVEL_BAR_OFFSET_HIGH, 3);
        gtk_level_bar_add_offset_value (self->strength, GTK_LEVEL_BAR_OFFSET_FULL, G_PASTE_PROMPT_STRENGTH_MAX);
        gtk_widget_set_valign (strength, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request (strength, 120, -1);
        adw_action_row_add_suffix (ADW_ACTION_ROW (strength_row), strength);
#else
        /* Built without libpwquality, so there is no rating to give. Say so
         * rather than leave the row out: someone choosing a passphrase should
         * know it is not being judged, instead of reading a silent absence as
         * approval. */
        adw_action_row_set_subtitle (ADW_ACTION_ROW (strength_row), _("Not available in this build"));
        gtk_widget_set_sensitive (strength_row, FALSE);
#endif

        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), strength_row);
    }

    /* Whatever the prompt has to say, said once. The standing explanation
     * depends on which question is being asked; an error joins it rather than
     * replacing it, and the shell backend shows both the same way. */
    const gchar *standing = confirm
        ? _("If you forget this passphrase, your stored history cannot be recovered.")
        : _("Enter the passphrase to unlock your clipboard history.");
    g_autofree gchar *description = error_message ? g_strconcat (error_message, "\n\n", standing, NULL)
                                                  : NULL;

    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                           description ? description : standing);

    /* No keyring in this build means nothing to offer to remember it in. */
    if (g_paste_prompt_keyring_available ())
    {
        GtkWidget *remember = adw_switch_row_new ();
        gboolean starts_on;

        self->remember = remember;
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (remember), _("Remember this passphrase"));
        adw_action_row_set_subtitle (ADW_ACTION_ROW (remember), _("Store it in the keyring so you are not asked again"));

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

static void
g_paste_prompt_adw_prompt_iface_init (GPastePromptInterface *iface)
{
    iface->passphrase = g_paste_prompt_adw_passphrase;
    iface->migration = g_paste_prompt_adw_migration;
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
    GPastePromptAdwPrivate *priv = g_paste_prompt_adw_get_instance_private (self);

    priv->application = application;

    return G_PASTE_PROMPT (self);
}
