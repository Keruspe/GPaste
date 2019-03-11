/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const { St } = imports.gi;

const Signals = imports.signals;

class GPastePageItem {
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
