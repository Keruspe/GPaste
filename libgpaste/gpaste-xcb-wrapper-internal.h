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

#ifndef __G_PASTE_XCB_WRAPPER_INTERNAL_H__
#define __G_PASTE_XCB_WRAPPER_INTERNAL_H__

#include "gpaste-xcb-wrapper.h"

G_BEGIN_DECLS

xcb_connection_t  *g_paste_xcb_wrapper_get_connection (GPasteXcbWrapper *self);
xcb_screen_t      *g_paste_xcb_wrapper_get_screen     (GPasteXcbWrapper *self);
xcb_key_symbols_t *g_paste_xcb_wrapper_get_keysyms    (GPasteXcbWrapper *self);

G_END_DECLS

#endif /*__G_PASTE_XCB_WRAPPER_INTERNAL_H__*/
