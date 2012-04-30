/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
    public class Window : Gtk.Window {
        private Settings settings;

        private Gtk.CheckButton track_changes_button;
        private Gtk.CheckButton track_extension_state_button;
        private Gtk.CheckButton primary_to_history_button;
        private Gtk.CheckButton synchronize_clipboards_button;
        private Gtk.CheckButton save_history_button;
        private Gtk.CheckButton trim_items_button;
        private Gtk.CheckButton fifo_button;
        private Gtk.SpinButton max_history_size_button;
        private Gtk.SpinButton max_displayed_history_size_button;
        private Gtk.SpinButton element_size_button;
        private Gtk.SpinButton min_text_item_size_button;
        private Gtk.SpinButton max_text_item_size_button;
        private Gtk.Entry show_history_entry;
        private Gtk.Entry paste_and_pop_entry;
        private Gtk.ComboBoxText targets;

        public Window (Gtk.Application application) {
            GLib.Object (type: Gtk.WindowType.TOPLEVEL);

            this.settings = new Settings ();
            this.settings.changed.connect ((key) => {
                switch(key) {
                case "track-changes":
                    this.track_changes_button.set_active (this.settings.get_track_changes ());
                    break;
                case "track-extension-state":
                    this.track_extension_state_button.set_active (this.settings.get_track_extension_state ());
                    break;
                case "primary-to-history":
                    this.primary_to_history_button.set_active (this.settings.get_primary_to_history ());
                    break;
                case "synchronize_clipboards":
                    this.synchronize_clipboards_button.set_active (this.settings.get_synchronize_clipboards ());
                    break;
                case "save-history":
                    this.save_history_button.set_active (this.settings.get_save_history ());
                    break;
                case "trim-items":
                    this.trim_items_button.set_active (this.settings.get_trim_items ());
                    break;
                case "fifo":
                    this.fifo_button.set_active (this.settings.get_fifo ());
                    break;
                case "max-history-size":
                    this.max_history_size_button.set_value (this.settings.get_max_history_size ());
                    break;
                case "max-displayed-history-size":
                    this.max_displayed_history_size_button.set_value (this.settings.get_max_displayed_history_size ());
                    break;
                case "element-size":
                    this.element_size_button.set_value (this.settings.get_element_size ());
                    break;
                case "min-text-item-size":
                    this.min_text_item_size_button.set_value (this.settings.get_min_text_item_size ());
                    break;
                case "max-text-item-size":
                    this.max_text_item_size_button.set_value (this.settings.get_max_text_item_size ());
                    break;
                case "show-history":
                    this.show_history_entry.set_text (this.settings.get_show_history ());
                    break;
                case "paste-and-pop":
                    this.paste_and_pop_entry.set_text (this.settings.get_paste_and_pop ());
                    break;
                }
            });

            this.application = application;
            this.title = _("GPaste daemon settings");
            this.set_position (Gtk.WindowPosition.CENTER);
            this.resizable = false;
            this.fill ();
        }

        private Panel make_behaviour_panel () {
            var panel = new Panel ();

            this.track_changes_button = panel.add_boolean_setting (_("_Track clipboard changes"),
                                                                   this.settings.get_track_changes (),
                                                                   (value) => {
                                                                       this.settings.set_track_changes (value);
                                                                   });
            this.track_extension_state_button = panel.add_boolean_setting (_("Sync the daemon state with the _extension's one"),
                                                                           this.settings.get_track_extension_state (),
                                                                           (value) => {
                                                                               this.settings.set_track_extension_state (value);
                                                                           });
            panel.add_separator ();
            this.primary_to_history_button = panel.add_boolean_setting (_("_Primary selection affects history"),
                                                                        this.settings.get_primary_to_history (),
                                                                        (value) => {
                                                                            this.settings.set_primary_to_history (value);
                                                                        });
            this.synchronize_clipboards_button = panel.add_boolean_setting (_("_Synchronize clipboard with primary selection"),
                                                                            this.settings.get_synchronize_clipboards (),
                                                                            (value) => {
                                                                                this.settings.set_synchronize_clipboards (value);
                                                                            });
            panel.add_separator ();
            this.save_history_button = panel.add_boolean_setting (_("_Save history"),
                                                                  this.settings.get_save_history (),
                                                                  (value) => {
                                                                      this.settings.set_save_history (value);
                                                                  });
            this.trim_items_button = panel.add_boolean_setting (_("_Trim items"),
                                                                this.settings.get_trim_items (),
                                                                (value) => {
                                                                    this.settings.set_trim_items (value);
                                                                });
            this.fifo_button = panel.add_boolean_setting (_("_Copy to end of history"),
                                                          this.settings.get_fifo (),
                                                          (value) => {
                                                              this.settings.set_fifo (value);
                                                          });

            return panel;
        }

        private Panel make_history_settings_panel () {
            var panel = new Panel ();

            this.max_history_size_button = panel.add_range_setting (_("Max history size: "),
                                                                    (double)this.settings.get_max_history_size (),
                                                                    5, 255, 5,
                                                                    (value) => {
                                                                        this.settings.set_max_history_size ((uint)value);
                                                                    });
            this.max_displayed_history_size_button = panel.add_range_setting (_("Max displayed history size: "),
                                                                              (double)this.settings.get_max_displayed_history_size (),
                                                                              5, 255, 5,
                                                                              (value) => {
                                                                                  this.settings.set_max_displayed_history_size ((uint)value);
                                                                              });
            this.element_size_button = panel.add_range_setting (_("Max element size when displaying: "),
                                                                (double)this.settings.get_element_size (),
                                                                0, 255, 5,
                                                                (value) => {
                                                                    this.settings.set_element_size ((uint)value);
                                                                });
            this.min_text_item_size_button = panel.add_range_setting (_("Min text item length: "),
                                                                      (double)this.settings.get_min_text_item_size (),
                                                                      1, uint32.MAX, 1,
                                                                      (value) => {
                                                                          this.settings.set_min_text_item_size ((uint)value);
                                                                      });
            this.max_text_item_size_button = panel.add_range_setting (_("Max text item length: "),
                                                                      (double)this.settings.get_max_text_item_size (),
                                                                      1, uint32.MAX, 1,
                                                                      (value) => {
                                                                          this.settings.set_max_text_item_size ((uint)value);
                                                                      });

            return panel;
        }

        private Panel make_keybindings_panel () {
            var panel = new Panel ();

            /* translators: Keyboard shortcut to display the history */
            this.show_history_entry = panel.add_text_setting (_("Display the history: "),
                                                              this.settings.get_show_history (),
                                                              (value) => {
                                                                  this.settings.set_show_history (value);
                                                              });
            /* translators: Keyboard shortcut to paste and then delete the first item in history */
            this.paste_and_pop_entry = panel.add_text_setting (_("Paste and then delete the first item in history: "),
                                                               this.settings.get_paste_and_pop (),
                                                               (value) => {
                                                                   this.settings.set_paste_and_pop (value);
                                                               });

            return panel;
        }

        private void refill_histories () {
            this.targets.remove_all ();
            foreach (string label in History.list ()) {
                this.targets.append_text (label);
            }
            this.targets.active = 0;
        }

        private Panel make_histories_panel () {
            var panel = new Panel ();

            panel.add_text_confirm_setting (_("Backup history as: "),
                                            this.settings.get_history_name () + "_backup",
                                            (value) => { /* nothing to do there */ },
                                            "Backup",
                                            (value) => {
                                                try {
                                                    (this.application as Main).gpaste.backup_history (value);
                                                    this.refill_histories ();
                                                } catch (GLib.IOError e) {
                                                    /* TODO: error message */
                                                }
                                            });
            string[] actions = { "Switch to", "Delete" };
            this.targets = panel.add_multi_action_setting (actions, History.list (), "Go", (action, target) => {
                               try {
                                   switch (action) {
                                   case "Switch to":
                                       (this.application as Main).gpaste.switch_history (target);
                                       break;
                                   case "Delete":
                                       (this.application as Main).gpaste.delete_history (target);
                                       this.refill_histories ();
                                       break;
                                   }
                               } catch (GLib.IOError e) {
                                   /* TODO: error message */
                               }
                           });

            return panel;
        }

        private void fill () {
            var notebook = new Notebook ();

            notebook.add_panel (_("General behaviour"), this.make_behaviour_panel ());
            notebook.add_panel (_("History settings"), this.make_history_settings_panel ());
            notebook.add_panel (_("Keyboard shortcuts"), this.make_keybindings_panel ());
            notebook.add_panel (_("Histories"), this.make_histories_panel ());

            this.add (notebook);
        }
    }
}
