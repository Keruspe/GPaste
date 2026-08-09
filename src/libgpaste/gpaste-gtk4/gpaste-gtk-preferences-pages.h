// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-gtk4/gpaste-gtk-macros.h>

G_BEGIN_DECLS

/* The four pages the preferences dialog and the preferences widget both show.
 * They carry no state of their own, so they are functions building an
 * AdwPreferencesPage rather than four GObject subclasses that would each add a
 * type, an empty class_init and an empty init for nothing. This header is not
 * installed: both callers live in this library. */

AdwPreferencesPage *g_paste_gtk_preferences_behaviour_page_new        (GPasteSettings *settings);
AdwPreferencesPage *g_paste_gtk_preferences_history_settings_page_new (GPasteSettings *settings);
AdwPreferencesPage *g_paste_gtk_preferences_images_page_new           (GPasteSettings *settings);
AdwPreferencesPage *g_paste_gtk_preferences_shortcuts_page_new        (GPasteSettings *settings);

/* Build every page, in the order they are shown. The %NULL-terminated array is
 * the caller's (free it with g_free); the pages themselves are floating, to be
 * consumed by whatever container is adding them. Having one list rather than
 * one per caller is what stops the dialog and the widget from drifting apart. */
AdwPreferencesPage **g_paste_gtk_preferences_pages_new (GPasteSettings *settings);

G_END_DECLS
