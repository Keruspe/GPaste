/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_XCB_WRAPPER_H__
#define __G_PASTE_XCB_WRAPPER_H__

#ifdef G_PASTE_COMPILATION
#include "config.h"
#endif

#include <glib-object.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_XCB_WRAPPER            (g_paste_xcb_wrapper_get_type ())
#define G_PASTE_XCB_WRAPPER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_XCB_WRAPPER, GPasteXcbWrapper))
#define G_PASTE_IS_XCB_WRAPPER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_XCB_WRAPPER))
#define G_PASTE_XCB_WRAPPER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_XCB_WRAPPER, GPasteXcbWrapperClass))
#define G_PASTE_IS_XCB_WRAPPER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_XCB_WRAPPER))
#define G_PASTE_XCB_WRAPPER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_XCB_WRAPPER, GPasteXcbWrapperClass))

typedef struct _GPasteXcbWrapper GPasteXcbWrapper;
typedef struct _GPasteXcbWrapperClass GPasteXcbWrapperClass;

typedef struct _GPasteConnection GPasteConnection;
typedef struct _GPasteScreen GPasteScreen;
typedef struct _GPasteKeySymbols GPasteKeySymbols;
typedef struct _GPasteKeycode GPasteKeycode;

#ifdef G_PASTE_COMPILATION
G_PASTE_VISIBLE
#endif
GType g_paste_xcb_wrapper_get_type (void);

GPasteConnection *g_paste_xcb_wrapper_get_connection (GPasteXcbWrapper *self);
GPasteScreen     *g_paste_xcb_wrapper_get_screen     (GPasteXcbWrapper *self);
GPasteKeySymbols *g_paste_xcb_wrapper_get_keysyms    (GPasteXcbWrapper *self);

GPasteXcbWrapper *g_paste_xcb_wrapper_new (void);

G_END_DECLS

#endif /*__G_PASTE_XCB_WRAPPER_H__*/
