/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */
/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const St = imports.gi.St;

const Signals = imports.signals;

var GPastePageItem = class {
    constructor(page) {
        this.actor = new St.Button({
            reactive: true,
            can_focus: false,
            track_hover: true,
            style_class: 'calendar-day-base'
        });

        this.actor.child = new St.Label({ text: '' + page });

        this._page = page;

        this.actor.connect('clicked', () => {
            this.emit('switch', this._page);
        });
    }

    setActive(active) {
      if (active) {
          this.actor.add_style_pseudo_class('active');
          this.actor.set_style("font-weight: bold;");
      } else {
          this.actor.remove_style_pseudo_class('active');
          this.actor.set_style(null);
      }
    }

    destroy() {
        this.actor.destroy();
    }
};
Signals.addSignalMethods(GPastePageItem.prototype);
