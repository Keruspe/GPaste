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
