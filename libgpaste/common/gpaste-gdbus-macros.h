/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_GDBUS_MACROS_H__
#define __G_PASTE_GDBUS_MACROS_H__

G_BEGIN_DECLS

/*********************/
/* Custom Data Types */
/*********************/

#ifdef __G_PASTE_NEEDS_BS__
typedef struct
{
    gboolean b;
    gchar   *s;
} GPasteDBusBSResult;
#endif /* __G_PASTE_NEEDS_BS__ */

/*********************/
/* Custom Extractors */
/*********************/

#ifdef __G_PASTE_NEEDS_BS__
static GPasteDBusBSResult
g_paste_dbus_get_bs_result (GVariant *variant)
{
    GVariantIter iter;
    g_variant_iter_init (&iter, variant);

    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *b = g_variant_iter_next_value (&iter);
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *s = g_variant_iter_next_value (&iter);

    GPasteDBusBSResult r = {
        .b = g_variant_get_boolean (b),
        .s = g_variant_dup_string (s, NULL /* length */)
    };

    return r;
}
#endif /* __G_PASTE_NEEDS_BS__ */

#ifdef __G_PASTE_NEEDS_AU__
static guint32 *
g_paste_dbus_get_au_result (GVariant *variant,
                            guint32   n_results)
{
    GVariantIter iter;
    g_variant_iter_init (&iter, variant);
    GVariant *loop;
    guint32 *res = g_new (guint32, n_results);

    for (guint32 i = 0; (loop = g_variant_iter_next_value (&iter)) && i < n_results; ++i)
    {
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *v = loop;
        res[i] = g_variant_get_uint32 (v);
    }

    if (loop)
        g_warning ("Expected %u results but got more.", n_results);

    return res;
}
#endif /* __G_PASTE_NEEDS_AU__ */

/***********/
/* Methods */
/***********/

#define DBUS_CALL_FULL(TypeName, type_name, guard, decl, method, params, n_params, if_fail, extract_and_return_answer) \
    guard;                                                                                                             \
    TypeName##Private *priv = type_name##_get_instance_private (self);                                                 \
    decl;                                                                                                              \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *_result = g_dbus_proxy_call_sync (priv->proxy,                             \
                                                                              method,                                  \
                                                                              g_variant_new_tuple (params, n_params),  \
                                                                              G_DBUS_CALL_FLAGS_NONE,                  \
                                                                              -1,                                      \
                                                                              NULL, /* cancellable */                  \
                                                                              error);                                  \
    if (!_result)                                                                                                      \
        return if_fail;                                                                                                \
    extract_and_return_answer

#define DBUS_CALL_WITH_RETURN_FULL_BASE(TypeName, type_name, TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, pre_extract) \
    DBUS_CALL_FULL (TypeName, type_name, g_return_val_if_fail (TYPE_CHECKER (self), if_fail), decl, method, params, n_params, if_fail,            \
        pre_extract;                                                                                                                              \
        variant_extract)

#define DBUS_CALL_WITH_RETURN_RAW_BASE(TypeName, type_name, TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_FULL_BASE (TypeName, type_name, TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract,   \
        GVariant *variant = _result)

#define DBUS_CALL_WITH_RETURN_BASE(TypeName, type_name, TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract)   \
    DBUS_CALL_WITH_RETURN_FULL_BASE (TypeName, type_name, TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, \
        GVariantIter result_iter;                                                                                                 \
        g_variant_iter_init (&result_iter, _result);                                                                              \
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant = g_variant_iter_next_value (&result_iter))


#define DBUS_CALL_NO_RETURN_BASE(TypeName, type_name, TYPE_CHECKER, decl, method, params, n_params) \
    DBUS_CALL_FULL (TypeName, type_name, g_return_if_fail (TYPE_CHECKER (self)), decl, method, params, n_params, ;, {})

#define DBUS_CALL_PARAMS_RAW_BASE(TypeName, type_name, TYPE_CHECKER, method, params, n_params, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_RAW_BASE (TypeName, type_name, TYPE_CHECKER, {}, method, params, n_params, if_fail, variant_extract)

#define DBUS_CALL_ONE_PARAMV_RET_AU_BASE(TypeName, type_name, TYPE_CHECKER, method, paramv, n_items) \
    DBUS_CALL_PARAMS_RAW_BASE(TypeName, type_name, TYPE_CHECKER, method, &paramv, 1, FALSE, return g_paste_dbus_get_au_result (variant, n_items))

#define DBUS_CALL_ONE_PARAM_RAW_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_RAW_BASE (TypeName, type_name, TYPE_CHECKER,                                                            \
                                    GVariant *parameter = g_variant_new_##param_type (param_name),                                \
                                    method, &parameter, 1, if_fail, variant_extract)

#define DBUS_CALL_ONE_PARAM_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TypeName, type_name, TYPE_CHECKER,                                                            \
                                GVariant *parameter = g_variant_new_##param_type (param_name),                                \
                                method, &parameter, 1, if_fail, variant_extract)

#define DBUS_CALL_ONE_PARAM_RET_BOOL_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method, FALSE, return g_variant_get_boolean (variant))

#define DBUS_CALL_ONE_PARAM_RET_STRING_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method, NULL, return g_variant_dup_string (variant, NULL /* length */))

#define DBUS_CALL_ONE_PARAM_RET_BS_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_RAW_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method, FALSE, GPasteDBusBSResult bs = g_paste_dbus_get_bs_result (variant))

#define DBUS_CALL_NO_PARAM_BASE(TypeName, type_name, TYPE_CHECKER, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TypeName, type_name, TYPE_CHECKER, {}, method, NULL, 0, if_fail, variant_extract)

#define DBUS_CALL_NO_PARAM_RET_STRV_BASE(TypeName, type_name, TYPE_CHECKER, method) \
    DBUS_CALL_NO_PARAM_BASE(TypeName, type_name, TYPE_CHECKER, method, NULL, return g_variant_dup_strv (variant, NULL)) \

#define DBUS_CALL_ONE_PARAM_NO_RETURN_BASE(TypeName, type_name, TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_NO_RETURN_BASE (TypeName, type_name, TYPE_CHECKER,                                              \
                              GVariant *parameter = g_variant_new_##param_type (param_name),                  \
                              method, &parameter, 1)

#define DBUS_CALL_NO_PARAM_NO_RETURN_BASE(TypeName, type_name, TYPE_CHECKER, method) \
    DBUS_CALL_NO_RETURN_BASE (TypeName, type_name, TYPE_CHECKER, {}, method, NULL, 0)

/**************/
/* Properties */
/**************/

/**
 * Getters
 */

#define DBUS_GET_PROPERTY_INIT(TypeName, type_name, property,  _default)                            \
    TypeName##Private *priv = type_name##_get_instance_private (self);                              \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *result = g_dbus_proxy_get_cached_property (priv->proxy, \
                                                                                       property);   \
    if (!result)                                                                                    \
        return _default

#define DBUS_GET_BOOLEAN_PROPERTY_BASE(TypeName, type_name, property) \
    DBUS_GET_PROPERTY_INIT (TypeName, type_name, property, FALSE);    \
    return g_variant_get_boolean (result)

#define DBUS_GET_STRING_PROPERTY_BASE(TypeName, type_name, property) \
    DBUS_GET_PROPERTY_INIT (TypeName, type_name, property, NULL);    \
    return g_variant_get_string (result, NULL)

/**
 * Setters
 */

#define DBUS_SET_GENERIC_PROPERTY_BASE(TypeName, type_name, iface, property, value, vtype)                          \
    TypeName##Private *priv = type_name##_get_instance_private (self);                                              \
    GVariant *prop[] = {                                                                                            \
        g_variant_new_string (iface),                                                                               \
        g_variant_new_string (property),                                                                            \
        g_variant_new_##vtype (value)                                                                               \
    };                                                                                                              \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *result = g_dbus_proxy_call_sync (priv->proxy,                           \
                                                                             "org.freedesktop.DBus.Properties.Set", \
                                                                             g_variant_new_tuple (prop, 3),         \
                                                                             G_DBUS_CALL_FLAGS_NONE,                \
                                                                             -1,                                    \
                                                                             NULL, /* cancellable */                \
                                                                             error);                                \
    return !!result

#define DBUS_SET_BOOLEAN_PROPERTY_BASE(TypeName, type_name, iface, property, value) \
    DBUS_SET_GENERIC_PROPERTY_BASE (TypeName, type_name, iface, property, value, boolean)

G_END_DECLS

#endif /*__G_PASTE_GDBUS_MACROS_H__*/
