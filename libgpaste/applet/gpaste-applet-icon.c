/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-applet-icon-private.h"

G_DEFINE_ABSTRACT_TYPE (GPasteAppletIcon, g_paste_applet_icon, G_TYPE_OBJECT)

static void
g_paste_applet_icon_class_init (GPasteAppletIconClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_applet_icon_init (GPasteAppletIcon *self G_GNUC_UNUSED)
{
}
