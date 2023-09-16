/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPastePageItem = GObject.registerClass({
    Signals: {
        'switch': { param_types: [GObject.TYPE_UINT64] },
    },
}, class GPastePageItem extends St.Button {
    _init(page) {
        super._init({
            label: '' + page,
            reactive: true,
            can_focus: false,
            track_hover: true,
            style_class: 'calendar-day-base calendar-day'
        });

        this._page = page;

        this.connect('clicked', () => {
            this.emit('switch', this._page);
        });
    }

    setActive(active) {
      if (active) {
          this.add_style_pseudo_class('selected');
      } else {
          this.remove_style_pseudo_class('selected');
      }
    }
});
