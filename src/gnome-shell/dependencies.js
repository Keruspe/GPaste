/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import Gio from 'gi://Gio?version=2.0';
import GPaste from 'gi://GPaste?version=2';

import * as Config from 'resource:///org/gnome/shell/misc/config.js';

import gi from 'gi';

gi.require('Clutter', Config.LIBMUTTER_API_VERSION);
gi.require('St', Config.LIBMUTTER_API_VERSION);
