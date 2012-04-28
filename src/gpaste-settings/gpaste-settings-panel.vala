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

namespace GPaste {
    public delegate void BooleanCallback (bool data);
    public delegate void MultiActionCallback (string action, string target);
    public delegate void RangeCallback (double data);
    public delegate void TextCallback (string data);

    public class Panel : Gtk.Grid {
        private int current_line;

        public Panel () {
            this.current_line = 0;
            this.margin = 10;
            this.set_column_spacing (10);
            this.set_row_spacing (10);
        }

        public Gtk.CheckButton add_boolean_setting (string label, bool value, BooleanCallback on_value_changed) {
            var button = new Gtk.CheckButton.with_mnemonic (label);
            button.set_active (value);
            button.toggled.connect (() => {
                on_value_changed (button.get_active ());
            });
            this.attach (button, 0, this.current_line++, 2, 1);
            return button;
        }

        public void add_separator () {
            this.attach (new Gtk.Separator (Gtk.Orientation.HORIZONTAL), 0, this.current_line++, 2, 1);
        }

        private Gtk.Label add_label (string label) {
            var button_label = new Gtk.Label (label);
            button_label.xalign = 0;
            this.attach (button_label, 0, this.current_line++, 1, 1);
            return button_label;
        }

        public Gtk.SpinButton add_range_setting (string label, double value, double min, double max, double step, RangeCallback on_value_changed) {
            var button_label = this.add_label (label);
            var button = new Gtk.SpinButton.with_range (min, max, step);
            button.set_value (value);
            button.value_changed.connect (() => {
                on_value_changed (button.get_value ());
            });
            this.attach_next_to (button, button_label, Gtk.PositionType.RIGHT, 1, 1);
            return button;
        }

        public Gtk.Entry add_text_setting (string label, string value, TextCallback on_value_changed) {
            var entry_label = this.add_label (label);
            var entry = new Gtk.Entry ();
            entry.set_text (value);
            entry.changed.connect (() => {
                on_value_changed (entry.get_text ());
            });
            this.attach_next_to (entry, entry_label, Gtk.PositionType.RIGHT, 1, 1);
            return entry;
        }

        private Gtk.Button add_confirm_button (string label, Gtk.Widget attach_to) {
            var button = new Gtk.Button.with_label (label);
            this.attach_next_to (button, attach_to, Gtk.PositionType.RIGHT, 1, 1);
            return button;
        }

        public Gtk.Entry add_text_confirm_setting (string label, string value, TextCallback on_value_changed, string confirm_label, TextCallback confirm_action) {
            var entry = this.add_text_setting (label, value, on_value_changed);
            var button = this.add_confirm_button (confirm_label, entry);
            button.pressed.connect (() => { confirm_action (entry.get_text ()); });
            return entry;
        }

        private Gtk.ComboBoxText make_combo_box_text (string[] labels, bool with_entry) {
            var combo_box = (with_entry) ? new Gtk.ComboBoxText.with_entry () : new Gtk.ComboBoxText ();
            foreach (string label in labels) {
                combo_box.append_text (label);
            }
            combo_box.active = 0;
            return combo_box;
        }

        public void add_multi_action_setting (string[] action_labels, string[] target_labels, string confirm_label, MultiActionCallback confirm_action) {
            var actions = this.make_combo_box_text (action_labels, false);
            var targets = this.make_combo_box_text (target_labels, true);
            this.attach (actions, 0, this.current_line++, 1, 1);
            this.attach_next_to (targets, actions, Gtk.PositionType.RIGHT, 1, 1);
            var button = this.add_confirm_button (confirm_label, targets);
            button.pressed.connect (() => { confirm_action (actions.get_active_text (), targets.get_active_text ()); });
        }
    }
}
