// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gio/gio.h>

/* Only our own sources: gi18n-lib.h refuses to be included without
 * GETTEXT_PACKAGE, which meson defines for this build but which a consumer of
 * the installed headers has no reason to define. It is here for the _() in our
 * .c files and for G_PASTE_INIT_GETTEXT below, neither of which is a caller's
 * concern. */
#ifdef G_PASTE_COMPILATION
#include <glib/gi18n-lib.h>
#endif

#include <stdlib.h>

G_BEGIN_DECLS

#ifdef G_HAVE_GNUC_VISIBILITY
#  define G_PASTE_VISIBLE __attribute__((visibility("default")))
#else
#  define G_PASTE_VISIBLE
#endif

#define g_paste_str_equal(s1, s2) (!g_strcmp0 (s1, s2))

#define G_PASTE_DERIVABLE_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_DERIVABLE_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName)

#define G_PASTE_FINAL_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_FINAL_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName)

#define G_PASTE_DEFINE_TYPE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE (GPaste##TypeName, g_paste_##type_name, ParentTypeName)

#define G_PASTE_DEFINE_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE_WITH_PRIVATE (GPaste##TypeName, g_paste_##type_name, ParentTypeName)

#define G_PASTE_DEFINE_ABSTRACT_TYPE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_ABSTRACT_TYPE (GPaste##TypeName, g_paste_##type_name, ParentTypeName)

#define G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GPaste##TypeName, g_paste_##type_name, ParentTypeName)

#define G_PASTE_DEFINE_TYPE_WITH_INTERFACE(TypeName, type_name, ParentTypeName, IFACE_TYPE, iface_init) \
    G_DEFINE_TYPE_WITH_CODE (GPaste##TypeName, g_paste_##type_name, ParentTypeName,                     \
        G_IMPLEMENT_INTERFACE (IFACE_TYPE, iface_init))

#define G_PASTE_INIT_GETTEXT()                          \
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);        \
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8"); \
    textdomain (GETTEXT_PACKAGE)

G_END_DECLS
