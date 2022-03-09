/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <stdlib.h>

G_BEGIN_DECLS

#ifdef G_HAVE_GNUC_VISIBILITY
#  define G_PASTE_VISIBLE __attribute__((visibility("default")))
#else
#  define G_PASTE_VISIBLE
#endif

#define g_paste_str_equal(s1, s2) (!g_strcmp0 (s1, s2))

#define G_PASTE_CONST_CHECKER(TYPE_NAME)            \
  static inline gboolean                            \
  _G_PASTE_IS_##TYPE_NAME (gconstpointer ptr) {     \
    return G_PASTE_IS_##TYPE_NAME ((gpointer) ptr); \
  }

#define G_PASTE_CONST_CLASS_GETTER(TypeName, TYPE_NAME)      \
  static inline const GPaste##TypeName##Class *              \
  _G_PASTE_##TYPE_NAME##_GET_CLASS (gconstpointer ptr) {     \
    return G_PASTE_##TYPE_NAME##_GET_CLASS ((gpointer) ptr); \
  }

#define G_PASTE_CONST_CASTER(TypeName, TYPE_NAME) \
  static inline const GPaste##TypeName *          \
  _G_PASTE_##TYPE_NAME (gconstpointer ptr) {      \
    return G_PASTE_##TYPE_NAME ((gpointer) ptr);  \
  }

#define G_PASTE_CONST_FUNCS(TypeName, TYPE_NAME) \
    G_PASTE_CONST_CHECKER (TYPE_NAME)            \
    G_PASTE_CONST_CASTER (TypeName, TYPE_NAME)

#define G_PASTE_DERIVABLE_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName)                                           \
    G_PASTE_VISIBLE G_DECLARE_DERIVABLE_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName) \
    G_PASTE_CONST_CLASS_GETTER (TypeName, TYPE_NAME)                                                                     \
    G_PASTE_CONST_FUNCS (TypeName, TYPE_NAME)

#define G_PASTE_FINAL_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName)                                           \
    G_PASTE_VISIBLE G_DECLARE_FINAL_TYPE (GPaste##TypeName, g_paste_##type_name, G_PASTE, TYPE_NAME, ParentTypeName) \
    G_PASTE_CONST_FUNCS (TypeName, TYPE_NAME)

#define G_PASTE_CONST_PRIV_ACCESSOR(TypeName, type_name)                             \
    static inline gconstpointer                                                      \
    _g_paste_##type_name##_get_instance_private (const GPaste##TypeName *self)       \
    {                                                                                \
      return g_paste_##type_name##_get_instance_private ((GPaste##TypeName *) self); \
    }

#define G_PASTE_DEFINE_TYPE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE (GPaste##TypeName, g_paste_##type_name, ParentTypeName)

#define G_PASTE_DEFINE_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName)          \
    G_DEFINE_TYPE_WITH_PRIVATE (GPaste##TypeName, g_paste_##type_name, ParentTypeName) \
    G_PASTE_CONST_PRIV_ACCESSOR (TypeName, type_name)

#define G_PASTE_DEFINE_ABSTRACT_TYPE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_ABSTRACT_TYPE (GPaste##TypeName, g_paste_##type_name, ParentTypeName)

#define G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName)          \
    G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GPaste##TypeName, g_paste_##type_name, ParentTypeName) \
    G_PASTE_CONST_PRIV_ACCESSOR (TypeName, type_name)

#define G_PASTE_INIT_GETTEXT()                          \
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);        \
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8"); \
    textdomain (GETTEXT_PACKAGE)

G_END_DECLS
