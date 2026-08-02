// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-prompt.h>

#include <adwaita.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_PROMPT_ADW (g_paste_prompt_adw_get_type ())

G_PASTE_FINAL_TYPE (PromptAdw, prompt_adw, PROMPT_ADW, GObject)

/* The libadwaita #GPastePrompt backend: the dialogs the standalone daemon (and
 * only it — the gnome-shell-hosted one can run neither gtk_init nor adw_init,
 * and brings its own St backend) puts the storage questions to the user
 * through. @application anchors them and must outlive the prompt. */
GPastePrompt *g_paste_prompt_adw_new (GtkApplication *application);

G_END_DECLS
