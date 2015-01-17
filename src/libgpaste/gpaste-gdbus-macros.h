/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
/* Custom Extractors */
/*********************/

#ifdef __G_PASTE_NEEDS_AU__
static guint32 *
g_paste_dbus_get_au_result (GVariant *variant,
                            gsize    *len)
{
    gsize _len;
    const guint32 *r = g_variant_get_fixed_array (variant, &_len, sizeof (guint32));
    guint32 *ret = g_memdup (r, _len * sizeof (guint32));

    if (len)
        *len = _len;

    return ret;
}
#endif

/***************/
/* Constructor */
/***************/

#define CUSTOM_PROXY_NEW_ASYNC(TYPE, BUS_ID)                                           \
    g_async_initable_new_async (G_PASTE_TYPE_##TYPE,                                   \
                                G_PRIORITY_DEFAULT,                                    \
                                NULL, /* cancellable */                                \
                                callback,                                              \
                                user_data,                                             \
                                "g-bus-type",       G_BUS_TYPE_SESSION,                \
                                "g-flags",          G_DBUS_PROXY_FLAGS_NONE,           \
                                "g-name",           G_PASTE_##BUS_ID##_BUS_NAME,       \
                                "g-object-path",    G_PASTE_##BUS_ID##_OBJECT_PATH,    \
                                "g-interface-name", G_PASTE_##BUS_ID##_INTERFACE_NAME, \
                                NULL)

#define CUSTOM_PROXY_NEW_FINISH(TYPE)                                                  \
    G_PASTE_CLEANUP_UNREF GObject *source = g_async_result_get_source_object (result); \
    g_assert (source);                                                                 \
    GObject *self = g_async_initable_new_finish (G_ASYNC_INITABLE (source),            \
                                                 result,                               \
                                                 error);                               \
    return (self) ? G_PASTE_##TYPE (self) : NULL;

#define CUSTOM_PROXY_NEW(TYPE, BUS_ID)                                                       \
    GInitable *self = g_initable_new (G_PASTE_TYPE_##TYPE,                                   \
                                      NULL, /* cancellable */                                \
                                      error,                                                 \
                                      "g-bus-type",       G_BUS_TYPE_SESSION,                \
                                      "g-flags",          G_DBUS_PROXY_FLAGS_NONE,           \
                                      "g-name",           G_PASTE_##BUS_ID##_BUS_NAME,       \
                                      "g-object-path",    G_PASTE_##BUS_ID##_OBJECT_PATH,    \
                                      "g-interface-name", G_PASTE_##BUS_ID##_INTERFACE_NAME, \
                                      NULL);                                                 \
    return (self) ? G_PASTE_##TYPE (self) : NULL;

/********************/
/* Methods / Common */
/********************/

#define DBUS_PREPARE_EXTRACTION                      \
        GVariantIter result_iter;                    \
        g_variant_iter_init (&result_iter, _result); \
        G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant = g_variant_iter_next_value (&result_iter)

#define DBUS_RETURN(if_fail, extract_and_return_answer) \
    if (!_result)                                       \
        return if_fail;                                 \
    extract_and_return_answer

/*****************************/
/* Methods / Async / General */
/*****************************/

#define DBUS_CALL_ASYNC_FULL(TYPE_CHECKER, decl, method, params, n_params) \
    g_return_if_fail (G_PASTE_IS_##TYPE_CHECKER (self));                   \
    decl;                                                                  \
    g_dbus_proxy_call (G_DBUS_PROXY(self),                                 \
                       method,                                             \
                       g_variant_new_tuple (params, n_params),             \
                       G_DBUS_CALL_FLAGS_NONE,                             \
                       -1,                                                 \
                       NULL, /* cancellable */                             \
                       callback,                                           \
                       user_data)

/**************************/
/* Methods / Async / Impl */
/**************************/

#define DBUS_CALL_NO_PARAM_ASYNC_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_ASYNC_FULL (TYPE_CHECKER, {}, method, NULL, 0)

#define DBUS_CALL_ONE_PARAM_ASYNC_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ASYNC_FULL (TYPE_CHECKER, GVariant *parameter = g_variant_new_##param_type (param_name), method, &parameter, 1)

#define DBUS_CALL_ONE_PARAMV_ASYNC_BASE(TYPE_CHECKER, paramv, method) \
    DBUS_CALL_ASYNC_FULL (TYPE_CHECKER, {}, method, &paramv, 1)

#define DBUS_CALL_TWO_PARAMS_ASYNC_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_ASYNC_FULL (TYPE_CHECKER, {}, method, params, 2)

/**************************************/
/* Methods / Async / General - Finish */
/**************************************/

#define DBUS_ASYNC_FINISH_FULL(guard, if_fail, extract_and_return_answer)                            \
    guard;                                                                                           \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *_result = g_dbus_proxy_call_finish (G_DBUS_PROXY (self), \
                                                                                result,              \
                                                                                error);              \
    DBUS_RETURN (if_fail, extract_and_return_answer)

#define DBUS_ASYNC_FINISH_WITH_RETURN(TYPE_CHECKER, if_fail, extract_and_return_answer)                \
    DBUS_ASYNC_FINISH_FULL (g_return_val_if_fail (G_PASTE_IS_##TYPE_CHECKER (self), if_fail);          \
                            g_return_val_if_fail (G_IS_ASYNC_RESULT (result), if_fail);                \
                            g_return_val_if_fail (!error || !(*error), if_fail),                       \
                            if_fail,                                                                   \
                            DBUS_PREPARE_EXTRACTION;                                                   \
                            extract_and_return_answer)

/***********************************/
/* Methods / Async / Impl - Finish */
/***********************************/

#define DBUS_ASYNC_FINISH_NO_RETURN_BASE(TYPE_CHECKER)                           \
    DBUS_ASYNC_FINISH_FULL (g_return_if_fail (G_PASTE_IS_##TYPE_CHECKER (self)); \
                            g_return_if_fail (G_IS_ASYNC_RESULT (result));       \
                            g_return_if_fail (!error || !(*error)), ;, {})

#define DBUS_ASYNC_FINISH_RET_BOOL_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, FALSE, return g_variant_get_boolean (variant))

#define DBUS_ASYNC_FINISH_RET_UINT32_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, 0, return g_variant_get_uint32 (variant))

#define DBUS_ASYNC_FINISH_RET_STRING_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_variant_dup_string (variant, NULL))

#define DBUS_ASYNC_FINISH_RET_STRV_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_variant_dup_strv (variant, NULL))

#define DBUS_ASYNC_FINISH_RET_AU_BASE(TYPE_CHECKER, len) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_paste_dbus_get_au_result (variant, len))

/****************************/
/* Methods / Sync / General */
/****************************/

#define DBUS_CALL_FULL(guard, decl, method, params, n_params, if_fail, extract_and_return_answer)                     \
    guard;                                                                                                            \
    decl;                                                                                                             \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *_result = g_dbus_proxy_call_sync (G_DBUS_PROXY (self),                    \
                                                                              method,                                 \
                                                                              g_variant_new_tuple (params, n_params), \
                                                                              G_DBUS_CALL_FLAGS_NONE,                 \
                                                                              -1,                                     \
                                                                              NULL, /* cancellable */                 \
                                                                              error);                                 \
    DBUS_RETURN (if_fail, extract_and_return_answer)

/****************************************/
/* Methods / Sync / General - No return */
/****************************************/

#define DBUS_CALL_NO_RETURN_BASE(TYPE_CHECKER, decl, method, params, n_params) \
    DBUS_CALL_FULL (g_return_if_fail (G_PASTE_IS_##TYPE_CHECKER (self)), decl, method, params, n_params, ;, {})

/******************************************/
/* Methods / Sync / General - With return */
/******************************************/

#define DBUS_CALL_WITH_RETURN_FULL_BASE(TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, pre_extract)                        \
    DBUS_CALL_FULL (g_return_val_if_fail (G_PASTE_IS_##TYPE_CHECKER (self), if_fail), decl, method, params, n_params, if_fail, \
                    pre_extract;                                                                                                                    \
                    variant_extract)

#define DBUS_CALL_WITH_RETURN_RAW_BASE(TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_FULL_BASE (TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, GVariant *variant = _result)

#define DBUS_CALL_WITH_RETURN_BASE(TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_FULL_BASE (TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, DBUS_PREPARE_EXTRACTION)

/*************************************/
/* Methods / Sync / Impl - No return */
/*************************************/

#define DBUS_CALL_NO_PARAM_NO_RETURN_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_NO_RETURN_BASE (TYPE_CHECKER, {}, method, NULL, 0)

#define DBUS_CALL_ONE_PARAMV_NO_RETURN_BASE(TYPE_CHECKER, paramv, method) \
    DBUS_CALL_NO_RETURN_BASE (TYPE_CHECKER, {}, method, &paramv, 1)

#define DBUS_CALL_ONE_PARAM_NO_RETURN_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_NO_RETURN_BASE (TYPE_CHECKER, GVariant *parameter = g_variant_new_##param_type (param_name), method, &parameter, 1)

#define DBUS_CALL_TWO_PARAMS_NO_RETURN_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_NO_RETURN_BASE (TYPE_CHECKER, {}, method, params, 2)

/**************************************************/
/* Methods / Sync / Impl - With return - No param */
/**************************************************/

#define DBUS_CALL_NO_PARAM_BASE(TYPE_CHECKER, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, NULL, 0, if_fail, variant_extract)

#define DBUS_CALL_NO_PARAM_RET_STRV_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_NO_PARAM_BASE(TYPE_CHECKER, method, NULL, return g_variant_dup_strv (variant, NULL)) \

#define DBUS_CALL_NO_PARAM_RET_UINT32_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_NO_PARAM_BASE(TYPE_CHECKER, method, 0, return g_variant_get_uint32 (variant)) \

#define DBUS_CALL_ONE_PARAMV_RET_AU_BASE(TYPE_CHECKER, method, paramv, len) \
    DBUS_CALL_WITH_RETURN_BASE(TYPE_CHECKER, {}, method, &paramv, 1, NULL, return g_paste_dbus_get_au_result (variant, len))

/******************************************************/
/* Methods / Sync / General - With return - One param */
/******************************************************/

#define DBUS_CALL_ONE_PARAM_RAW_BASE(TYPE_CHECKER, param_type, param_name, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_RAW_BASE (TYPE_CHECKER, GVariant *parameter = g_variant_new_##param_type (param_name), method, &parameter, 1, if_fail, variant_extract)

#define DBUS_CALL_ONE_PARAM_BASE(TYPE_CHECKER, param_type, param_name, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, GVariant *parameter = g_variant_new_##param_type (param_name), method, &parameter, 1, if_fail, variant_extract)

/***************************************************/
/* Methods / Sync / Impl - With return - One param */
/***************************************************/

#define DBUS_CALL_ONE_PARAM_RET_BOOL_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE(TYPE_CHECKER, param_type, param_name, method, FALSE, return g_variant_get_boolean (variant))

#define DBUS_CALL_ONE_PARAM_RET_STRING_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE(TYPE_CHECKER, param_type, param_name, method, NULL, return g_variant_dup_string (variant, NULL /* length */))

#define DBUS_CALL_ONE_PARAM_RET_AU_BASE(TYPE_CHECKER, param_type, param_name, method, len) \
    DBUS_CALL_ONE_PARAM_BASE(TYPE_CHECKER, param_type, param_name, method, NULL, return g_paste_dbus_get_au_result (variant, len))

/****************************************************/
/* Methods / Sync / Impl - With return - Two params */
/****************************************************/

#define DBUS_CALL_TWO_PARAMS_BASE(TYPE_CHECKER, params, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, params, 2, if_fail, variant_extract)

#define DBUS_CALL_TWO_PARAMS_RET_UINT32_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_TWO_PARAMS_BASE(TYPE_CHECKER, params, method, 0, return g_variant_get_uint32 (variant))

/************************/
/* Properties / Getters */
/************************/

#define DBUS_GET_PROPERTY_INIT(TYPE_CHECKER, property,  _default)                                           \
    g_return_val_if_fail (G_PASTE_IS_##TYPE_CHECKER (self), _default);                                      \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *result = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), \
                                                                                       property);           \
    if (!result)                                                                                            \
        return _default

#define DBUS_GET_BOOLEAN_PROPERTY_BASE(TYPE_CHECKER, property) \
    DBUS_GET_PROPERTY_INIT (TYPE_CHECKER, property, FALSE);    \
    return g_variant_get_boolean (result)

#define DBUS_GET_STRING_PROPERTY_BASE(TYPE_CHECKER, property) \
    DBUS_GET_PROPERTY_INIT (TYPE_CHECKER, property, NULL);    \
    return g_variant_dup_string (result, NULL)

/************************/
/* Properties / Setters */
/************************/

#define DBUS_SET_GENERIC_PROPERTY_BASE(TYPE_CHECKER, iface, property, value, vtype)                                 \
    g_return_val_if_fail (G_PASTE_IS_##TYPE_CHECKER (self), FALSE);                                                 \
    GVariant *prop[] = {                                                                                            \
        g_variant_new_string (iface),                                                                               \
        g_variant_new_string (property),                                                                            \
        g_variant_new_variant (g_variant_new_##vtype (value))                                                       \
    };                                                                                                              \
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *result = g_dbus_proxy_call_sync (G_DBUS_PROXY (self),                   \
                                                                             "org.freedesktop.DBus.Properties.Set", \
                                                                             g_variant_new_tuple (prop, 3),         \
                                                                             G_DBUS_CALL_FLAGS_NONE,                \
                                                                             -1,                                    \
                                                                             NULL, /* cancellable */                \
                                                                             error);                                \
    return !!result

#define DBUS_SET_BOOLEAN_PROPERTY_BASE(TYPE_CHECKER, iface, property, value) \
    DBUS_SET_GENERIC_PROPERTY_BASE (TYPE_CHECKER, iface, property, value, boolean)

G_END_DECLS

#endif /*__G_PASTE_GDBUS_MACROS_H__*/
