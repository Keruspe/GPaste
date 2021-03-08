/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_GDBUS_MACROS_H__
#define __G_PASTE_GDBUS_MACROS_H__

#ifndef __GI_SCANNER__

#include <gpaste-gdbus-defines.h>
#include <gpaste-util.h>

G_BEGIN_DECLS

/***************/
/* Constructor */
/***************/

#define CUSTOM_PROXY_NEW_ASYNC(TYPE, BUS_ID, BUS_NAME)                                 \
    g_async_initable_new_async (G_PASTE_TYPE_##TYPE,                                   \
                                G_PRIORITY_DEFAULT,                                    \
                                NULL, /* cancellable */                                \
                                callback,                                              \
                                user_data,                                             \
                                "g-bus-type",       G_BUS_TYPE_SESSION,                \
                                "g-flags",          G_DBUS_PROXY_FLAGS_NONE,           \
                                "g-name",           BUS_NAME,                          \
                                "g-object-path",    G_PASTE_##BUS_ID##_OBJECT_PATH,    \
                                "g-interface-name", G_PASTE_##BUS_ID##_INTERFACE_NAME, \
                                NULL)

#define CUSTOM_PROXY_RET(TYPE) \
    if (_error)                \
    {                          \
        if (error)             \
        {                      \
            *error = _error;   \
            _error = NULL;     \
        }                      \
        return NULL;           \
    }                          \
    return (self) ? G_PASTE_##TYPE (self) : NULL

#define CUSTOM_PROXY_NEW_FINISH(TYPE)                                       \
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);                \
    g_return_val_if_fail (!error || !(*error), NULL);                       \
    g_autoptr (GObject) source = g_async_result_get_source_object (result); \
    g_autoptr (GError) _error = NULL;                                       \
    g_assert (source);                                                      \
    GObject *self = g_async_initable_new_finish (G_ASYNC_INITABLE (source), \
                                                 result,                    \
                                                 &_error);                  \
    CUSTOM_PROXY_RET (TYPE);

#define CUSTOM_PROXY_NEW(TYPE, BUS_ID, BUS_NAME)                                             \
    g_autoptr (GError) _error = NULL;                                                        \
    GInitable *self = g_initable_new (G_PASTE_TYPE_##TYPE,                                   \
                                      NULL, /* cancellable */                                \
                                      &_error,                                               \
                                      "g-bus-type",       G_BUS_TYPE_SESSION,                \
                                      "g-flags",          G_DBUS_PROXY_FLAGS_NONE,           \
                                      "g-name",           BUS_NAME,                          \
                                      "g-object-path",    G_PASTE_##BUS_ID##_OBJECT_PATH,    \
                                      "g-interface-name", G_PASTE_##BUS_ID##_INTERFACE_NAME, \
                                      NULL);                                                 \
    CUSTOM_PROXY_RET (TYPE);

/********************/
/* Methods / Common */
/********************/

#define DBUS_PREPARE_EXTRACTION(iter)                                  \
    g_autoptr (GVariant) _variant = NULL;                              \
    G_GNUC_UNUSED GVariant *variant;                                   \
    if (iter)                                                          \
    {                                                                  \
        GVariantIter result_iter;                                      \
        g_variant_iter_init (&result_iter, _result);                   \
        variant = _variant = g_variant_iter_next_value (&result_iter); \
    }                                                                  \
    else                                                               \
        variant = _result

#define DBUS_RETURN(if_fail, extract_and_return_answer) \
    if (!_result)                                       \
        return if_fail;                                 \
    extract_and_return_answer

/*****************************/
/* Methods / Async / General */
/*****************************/

#define DBUS_CALL_ASYNC_FULL(TYPE_CHECKER, decl, method, params, n_params) \
    g_return_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self));                  \
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

#define DBUS_CALL_THREE_PARAMS_ASYNC_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_ASYNC_FULL (TYPE_CHECKER, {}, method, params, 3)

/**************************************/
/* Methods / Async / General - Finish */
/**************************************/

#define DBUS_ASYNC_FINISH_FULL(guard, if_fail, extract_and_return_answer)         \
    guard;                                                                        \
    g_autoptr (GVariant) _result = g_dbus_proxy_call_finish (G_DBUS_PROXY (self), \
                                                             result,              \
                                                             error);              \
    DBUS_RETURN (if_fail, extract_and_return_answer)

#define DBUS_ASYNC_FINISH_WITH_RETURN_FULL(TYPE_CHECKER, if_fail, iter, extract_and_return_answer) \
    DBUS_ASYNC_FINISH_FULL (g_return_val_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self), if_fail);     \
                            g_return_val_if_fail (G_IS_ASYNC_RESULT (result), if_fail);            \
                            g_return_val_if_fail (!error || !(*error), if_fail),                   \
                            if_fail,                                                               \
                            DBUS_PREPARE_EXTRACTION(iter);                                         \
                            extract_and_return_answer)

#define DBUS_ASYNC_FINISH_WITH_RETURN(TYPE_CHECKER, if_fail, extract_and_return_answer) \
    DBUS_ASYNC_FINISH_WITH_RETURN_FULL(TYPE_CHECKER, if_fail, TRUE, extract_and_return_answer)

/***********************************/
/* Methods / Async / Impl - Finish */
/***********************************/

#define DBUS_ASYNC_FINISH_NO_RETURN_BASE(TYPE_CHECKER)                           \
    DBUS_ASYNC_FINISH_FULL (g_return_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self)); \
                            g_return_if_fail (G_IS_ASYNC_RESULT (result));       \
                            g_return_if_fail (!error || !(*error)), ;, {})

#define DBUS_ASYNC_FINISH_RET_BOOL_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, FALSE, return g_variant_get_boolean (variant))

#define DBUS_ASYNC_FINISH_RET_UINT64_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, 0, return g_variant_get_uint64 (variant))

#define DBUS_ASYNC_FINISH_RET_UINT32_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, 0, return g_variant_get_uint32 (variant))

#define DBUS_ASYNC_FINISH_RET_STRING_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_variant_dup_string (variant, NULL))

#define DBUS_ASYNC_FINISH_RET_ITEM_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN_FULL (TYPE_CHECKER, NULL, FALSE, return g_paste_util_get_dbus_item_result (variant))

#define DBUS_ASYNC_FINISH_RET_STRV_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_variant_dup_strv (variant, NULL))

#define DBUS_ASYNC_FINISH_RET_ITEMS_BASE(TYPE_CHECKER) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_paste_util_get_dbus_items_result (variant))

#define DBUS_ASYNC_FINISH_RET_AU_BASE(TYPE_CHECKER, len) \
    DBUS_ASYNC_FINISH_WITH_RETURN (TYPE_CHECKER, NULL, return g_paste_util_get_dbus_au_result (variant, len))

/****************************/
/* Methods / Sync / General */
/****************************/

#define DBUS_CALL_FULL(guard, decl, method, params, n_params, if_fail, extract_and_return_answer)  \
    guard;                                                                                         \
    decl;                                                                                          \
    g_autoptr (GVariant) _result = g_dbus_proxy_call_sync (G_DBUS_PROXY (self),                    \
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
    DBUS_CALL_FULL (g_return_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self)), decl, method, params, n_params, ;, {})

/******************************************/
/* Methods / Sync / General - With return */
/******************************************/

#define DBUS_CALL_WITH_RETURN_FULL_BASE(TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, pre_extract)    \
    DBUS_CALL_FULL (g_return_val_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self), if_fail), decl, method, params, n_params, if_fail, \
                    pre_extract;                                                                                                \
                    variant_extract)

#define DBUS_CALL_WITH_RETURN_RAW_BASE(TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_FULL_BASE (TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, GVariant *variant = _result)

#define DBUS_CALL_WITH_RETURN_BASE_FULL(TYPE_CHECKER, decl, method, params, n_params, if_fail, iter, variant_extract) \
    DBUS_CALL_WITH_RETURN_FULL_BASE (TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract, DBUS_PREPARE_EXTRACTION(iter))

#define DBUS_CALL_WITH_RETURN_BASE(TYPE_CHECKER, decl, method, params, n_params, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE_FULL(TYPE_CHECKER, decl, method, params, n_params, if_fail, TRUE, variant_extract)

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

#define DBUS_CALL_THREE_PARAMS_NO_RETURN_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_NO_RETURN_BASE (TYPE_CHECKER, {}, method, params, 3)

/**************************************************/
/* Methods / Sync / Impl - With return - No param */
/**************************************************/

#define DBUS_CALL_NO_PARAM_BASE(TYPE_CHECKER, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, NULL, 0, if_fail, variant_extract)

#define DBUS_CALL_NO_PARAM_RET_STRING_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_NO_PARAM_BASE (TYPE_CHECKER, method, NULL, return g_variant_dup_string (variant, NULL))

#define DBUS_CALL_NO_PARAM_RET_STRV_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_NO_PARAM_BASE (TYPE_CHECKER, method, NULL, return g_variant_dup_strv (variant, NULL))

#define DBUS_CALL_NO_PARAM_RET_ITEMS_BASE(TYPE_CHECKER, method) \
    DBUS_CALL_NO_PARAM_BASE (TYPE_CHECKER, method, NULL, return g_paste_util_get_dbus_items_result (variant))

#define DBUS_CALL_ONE_PARAMV_RET_AU_BASE(TYPE_CHECKER, method, paramv, len) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, &paramv, 1, NULL, return g_paste_util_get_dbus_au_result (variant, len))

#define DBUS_CALL_ONE_PARAMV_RET_ITEMS_BASE(TYPE_CHECKER, method, paramv) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, &paramv, 1, NULL, return g_paste_util_get_dbus_items_result (variant))

/******************************************************/
/* Methods / Sync / General - With return - One param */
/******************************************************/

#define DBUS_CALL_ONE_PARAM_RAW_BASE(TYPE_CHECKER, param_type, param_name, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_RAW_BASE (TYPE_CHECKER, GVariant *parameter = g_variant_new_##param_type (param_name), method, &parameter, 1, if_fail, variant_extract)

#define DBUS_CALL_ONE_PARAM_BASE_FULL(TYPE_CHECKER, param_type, param_name, method, if_fail, iter, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE_FULL (TYPE_CHECKER, GVariant *parameter = g_variant_new_##param_type (param_name), method, &parameter, 1, if_fail, iter, variant_extract)

#define DBUS_CALL_ONE_PARAM_BASE(TYPE_CHECKER, param_type, param_name, method, if_fail, variant_extract) \
    DBUS_CALL_ONE_PARAM_BASE_FULL (TYPE_CHECKER, param_type, param_name, method, if_fail, TRUE, variant_extract)

/***************************************************/
/* Methods / Sync / Impl - With return - One param */
/***************************************************/

#define DBUS_CALL_ONE_PARAM_RET_BOOL_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE (TYPE_CHECKER, param_type, param_name, method, FALSE, return g_variant_get_boolean (variant))

#define DBUS_CALL_ONE_PARAM_RET_UINT64_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE (TYPE_CHECKER, param_type, param_name, method, 0, return g_variant_get_uint64 (variant))

#define DBUS_CALL_ONE_PARAM_RET_STRING_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE (TYPE_CHECKER, param_type, param_name, method, NULL, return g_variant_dup_string (variant, NULL /* length */))

#define DBUS_CALL_ONE_PARAM_RET_STRV_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE (TYPE_CHECKER, param_type, param_name, method, NULL, return g_variant_dup_strv (variant, NULL))

#define DBUS_CALL_ONE_PARAM_RET_ITEM_BASE(TYPE_CHECKER, param_type, param_name, method) \
    DBUS_CALL_ONE_PARAM_BASE_FULL (TYPE_CHECKER, param_type, param_name, method, NULL, FALSE, return g_paste_util_get_dbus_item_result (variant))

/****************************************************/
/* Methods / Sync / Impl - With return - Two params */
/****************************************************/

#define DBUS_CALL_TWO_PARAMS_BASE(TYPE_CHECKER, params, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, params, 2, if_fail, variant_extract)

#define DBUS_CALL_TWO_PARAMS_RET_UINT64_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_TWO_PARAMS_BASE(TYPE_CHECKER, params, method, 0, return g_variant_get_uint64 (variant))

#define DBUS_CALL_THREE_PARAMS_BASE(TYPE_CHECKER, params, method, if_fail, variant_extract) \
    DBUS_CALL_WITH_RETURN_BASE (TYPE_CHECKER, {}, method, params, 3, if_fail, variant_extract)

#define DBUS_CALL_THREE_PARAMS_RET_UINT32_BASE(TYPE_CHECKER, params, method) \
    DBUS_CALL_THREE_PARAMS_BASE(TYPE_CHECKER, params, method, 0, return g_variant_get_uint32 (variant))

/************************/
/* Properties / Getters */
/************************/

#define DBUS_GET_PROPERTY_INIT(TYPE_CHECKER, property,  _default)                        \
    g_return_val_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self), _default);                  \
    g_autoptr (GVariant) result = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), \
                                                                    property);           \
    if (!result)                                                                         \
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

#define DBUS_SET_GENERIC_PROPERTY_BASE(TYPE_CHECKER, iface, property, value, vtype)              \
    g_return_val_if_fail (_G_PASTE_IS_##TYPE_CHECKER (self), FALSE);                             \
    GVariant *prop[] = {                                                                         \
        g_variant_new_string (iface),                                                            \
        g_variant_new_string (property),                                                         \
        g_variant_new_variant (g_variant_new_##vtype (value))                                    \
    };                                                                                           \
    g_autoptr (GVariant) result = g_dbus_proxy_call_sync (G_DBUS_PROXY (self),                   \
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

#endif /*__GI_SCANNER__ */
#endif /*__G_PASTE_GDBUS_MACROS_H__*/
