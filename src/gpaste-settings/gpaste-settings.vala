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

public class Window : Gtk.Window {
    private Gtk.CheckButton track_changes_button;
    private Gtk.CheckButton track_extension_state_button;
    private Gtk.CheckButton primary_to_history_button;
    private Gtk.CheckButton synchronize_clipboards_button;
    private Gtk.CheckButton save_history_button;
    private Gtk.CheckButton trim_items_button;
    private Gtk.SpinButton max_history_size_button;
    private Gtk.SpinButton max_displayed_history_size_button;
    private Gtk.SpinButton element_size_button;
    private Gtk.SpinButton min_text_item_size_button;
    private Gtk.SpinButton max_text_item_size_button;
    private Gtk.Entry show_history_entry;
    private Gtk.Entry paste_and_pop_entry;

    public bool track_changes {
        get {
            return this.track_changes_button.get_active ();
        }
        set {
            this.track_changes_button.set_active (value);
        }
    }

    public bool track_extension_state {
        get {
            return this.track_extension_state_button.get_active ();
        }
        set {
            this.track_extension_state_button.set_active (value);
        }
    }

    public bool primary_to_history {
        get {
            return this.primary_to_history_button.get_active ();
        }
        set {
            this.primary_to_history_button.set_active (value);
        }
    }

    public bool synchronize_clipboards {
        get {
            return this.synchronize_clipboards_button.get_active ();
        }
        set {
            this.synchronize_clipboards_button.set_active (value);
        }
    }

    public bool save_history {
        get {
            return this.save_history_button.get_active ();
        }
        set {
            this.save_history_button.set_active (value);
        }
    }

    public bool trim_items {
        get {
            return this.trim_items_button.get_active ();
        }
        set {
            this.trim_items_button.set_active (value);
        }
    }

    public uint32 max_history_size {
        get {
            return (uint32)this.max_history_size_button.get_value_as_int ();
        }
        set {
            this.max_history_size_button.get_adjustment ().value = value;
        }
    }

    public uint32 max_displayed_history_size {
        get {
            return (uint32)this.max_displayed_history_size_button.get_value_as_int ();
        }
        set {
            this.max_displayed_history_size_button.get_adjustment ().value = value;
        }
    }

    public uint32 element_size {
        get {
            return (uint32)this.element_size_button.get_value_as_int ();
        }
        set {
            this.element_size_button.get_adjustment ().value = value;
        }
    }

    public uint32 min_text_item_size {
        get {
            return (uint32)this.min_text_item_size_button.get_value_as_int ();
        }
        set {
            this.min_text_item_size_button.get_adjustment ().value = value;
        }
    }

    public uint32 max_text_item_size {
        get {
            return (uint32)this.max_text_item_size_button.get_value_as_int ();
        }
        set {
            this.max_text_item_size_button.get_adjustment ().value = value;
        }
    }

    public string show_history {
        get {
            return this.show_history_entry.get_text ();
        }
        set {
            this.show_history_entry.set_text (value);
        }
    }

    public string paste_and_pop {
        get {
            return this.paste_and_pop_entry.get_text ();
        }
        set {
            this.paste_and_pop_entry.set_text (value);
        }
    }

    public Window (Gtk.Application app) {
        GLib.Object (type: Gtk.WindowType.TOPLEVEL);
        this.title = _("GPaste daemon settings");
        this.application = app;
        this.set_position (Gtk.WindowPosition.CENTER);
        this.resizable = false;
        this.fill ();
    }

    private void fill () {
        var grid = new Gtk.Grid ();
        var app = this.application as Main;
        int current_line = 0;

        grid.margin = 12;
        grid.set_column_spacing (10);
        grid.set_row_spacing (10);

        this.track_changes_button = new Gtk.CheckButton.with_mnemonic (_("_Track clipboard changes"));
        this.track_changes = app.settings.get_track_changes ();
        this.track_changes_button.toggled.connect (() => {
            app.settings.set_track_changes (this.track_changes);
        });
        grid.attach (track_changes_button, 0, current_line++, 2, 1);

        this.track_extension_state_button = new Gtk.CheckButton.with_mnemonic (_("Sync the daemon state with the _extension's one"));
        this.track_extension_state = app.settings.get_track_extension_state ();
        this.track_extension_state_button.toggled.connect (() => {
            app.settings.set_track_extension_state (this.track_extension_state);
        });
        grid.attach (track_extension_state_button, 0, current_line++, 2, 1);

        grid.attach (new Gtk.Separator (Gtk.Orientation.HORIZONTAL), 0, current_line++, 2, 1);

        this.primary_to_history_button = new Gtk.CheckButton.with_mnemonic (_("_Primary selection affects history"));
        this.primary_to_history = app.settings.get_primary_to_history ();
        this.primary_to_history_button.toggled.connect (() => {
            app.settings.set_primary_to_history (this.primary_to_history);
        });
        grid.attach (primary_to_history_button, 0, current_line++, 2, 1);

        this.synchronize_clipboards_button = new Gtk.CheckButton.with_mnemonic (_("_Synchronize clipboard with primary selection"));
        this.synchronize_clipboards = app.settings.get_synchronize_clipboards ();
        this.synchronize_clipboards_button.toggled.connect (() => {
            app.settings.set_synchronize_clipboards (this.synchronize_clipboards);
        });
        grid.attach (synchronize_clipboards_button, 0, current_line++, 2, 1);

        grid.attach (new Gtk.Separator (Gtk.Orientation.HORIZONTAL), 0, current_line++, 2, 1);

        this.save_history_button = new Gtk.CheckButton.with_mnemonic (_("_Save history"));
        this.save_history = app.settings.get_save_history ();
        this.save_history_button.toggled.connect (() => {
            app.settings.set_save_history (this.save_history);
        });
        grid.attach (save_history_button, 0, current_line++, 2, 1);

        this.trim_items_button = new Gtk.CheckButton.with_mnemonic (_("_Trim items"));
        this.trim_items = app.settings.get_trim_items ();
        this.trim_items_button.toggled.connect (() => {
            app.settings.set_trim_items (this.trim_items);
        });
        grid.attach (trim_items_button, 0, current_line++, 2, 1);

        grid.attach (new Gtk.Separator (Gtk.Orientation.HORIZONTAL), 0, current_line++, 2, 1);

        var max_history_size_label = new Gtk.Label (_("Max history size: "));
        max_history_size_label.xalign = 0;
        grid.attach (max_history_size_label, 0, current_line++, 1, 1);

        this.max_history_size_button = new Gtk.SpinButton.with_range (5, 255, 5);
        this.max_history_size = app.settings.get_max_history_size ();
        this.max_history_size_button.get_adjustment ().value_changed.connect (() => {
            app.settings.set_max_history_size (this.max_history_size);
        });
        grid.attach_next_to (this.max_history_size_button, max_history_size_label, Gtk.PositionType.RIGHT, 1, 1);

        var max_displayed_history_size_label = new Gtk.Label (_("Max displayed history size: "));
        max_displayed_history_size_label.xalign = 0;
        grid.attach (max_displayed_history_size_label, 0, current_line++, 1, 1);

        this.max_displayed_history_size_button = new Gtk.SpinButton.with_range(5, 255, 5);
        this.max_displayed_history_size = app.settings.get_max_displayed_history_size ();
        this.max_displayed_history_size_button.get_adjustment ().value_changed.connect(() => {
            app.settings.set_max_displayed_history_size (this.max_displayed_history_size);
        });
        grid.attach_next_to (this.max_displayed_history_size_button, max_displayed_history_size_label, Gtk.PositionType.RIGHT, 1, 1);

        var element_size_label = new Gtk.Label (_("Max element size when displaying: "));
        element_size_label.xalign = 0;
        grid.attach (element_size_label, 0, current_line++, 1, 1);

        this.element_size_button = new Gtk.SpinButton.with_range (0, 255, 5);
        this.element_size = app.settings.get_element_size ();
        this.element_size_button.get_adjustment ().value_changed.connect (() => {
            app.settings.set_element_size (this.element_size);
        });
        grid.attach_next_to (this.element_size_button, element_size_label, Gtk.PositionType.RIGHT, 1, 1);

        var min_text_item_size_label = new Gtk.Label (_("Min text item length: "));
        min_text_item_size_label.xalign = 0;
        grid.attach (min_text_item_size_label, 0, current_line++, 1, 1);

        this.min_text_item_size_button = new Gtk.SpinButton.with_range (1, uint32.MAX, 1);
        this.min_text_item_size = app.settings.get_min_text_item_size ();
        this.min_text_item_size_button.get_adjustment ().value_changed.connect (() => {
            app.settings.set_min_text_item_size (this.min_text_item_size);
        });
        grid.attach_next_to (this.min_text_item_size_button, min_text_item_size_label, Gtk.PositionType.RIGHT, 1, 1);

        var max_text_item_size_label = new Gtk.Label (_("Max text item length: "));
        max_text_item_size_label.xalign = 0;
        grid.attach (max_text_item_size_label, 0, current_line++, 1, 1);

        this.max_text_item_size_button = new Gtk.SpinButton.with_range (1, uint32.MAX, 1);
        this.max_text_item_size = app.settings.get_max_text_item_size ();
        this.max_text_item_size_button.get_adjustment ().value_changed.connect (() => {
            app.settings.set_max_text_item_size (this.max_text_item_size);
        });
        grid.attach_next_to (this.max_text_item_size_button, max_text_item_size_label, Gtk.PositionType.RIGHT, 1, 1);

        var show_history_label = new Gtk.Label (_("Keyboard shortcut to display the history: "));
        show_history_label.xalign = 0;
        grid.attach (show_history_label, 0, current_line++, 1, 1);

        this.show_history_entry = new Gtk.Entry ();
        this.show_history = app.settings.get_show_history ();
        this.show_history_entry.changed.connect (() => {
            app.settings.set_show_history (this.show_history);
        });
        grid.attach_next_to (this.show_history_entry, show_history_label, Gtk.PositionType.RIGHT, 1, 1);

        var paste_and_pop_label = new Gtk.Label (_("Keyboard shortcut to paste and then delete the first item in history: "));
        paste_and_pop_label.xalign = 0;
        grid.attach (paste_and_pop_label, 0, current_line++, 1, 1);

        this.paste_and_pop_entry = new Gtk.Entry ();
        this.paste_and_pop = app.settings.get_paste_and_pop ();
        this.paste_and_pop_entry.changed.connect (() => {
            app.settings.set_paste_and_pop (this.paste_and_pop);
        });
        grid.attach_next_to (this.paste_and_pop_entry, paste_and_pop_label, Gtk.PositionType.RIGHT, 1, 1);

        this.add (grid);
    }
}

public class Main : Gtk.Application {
    private Window window;
    public GPaste.Settings settings {
        get; private set;
    }

    public Main () {
        GLib.Object (application_id: "org.gnome.GPaste.Settings");
        this.activate.connect (this.init);
        this.settings = new GPaste.Settings ();
        this.settings.changed.connect ((key) => {
            switch(key) {
            case "track-changes":
                this.window.track_changes = this.settings.get_track_changes ();
                break;
            case "track-extension-state":
                this.window.track_extension_state = this.settings.get_track_extension_state ();
                break;
            case "primary-to-history":
                this.window.primary_to_history = this.settings.get_primary_to_history ();
                break;
            case "synchronize_clipboards":
                this.window.synchronize_clipboards = this.settings.get_synchronize_clipboards ();
                break;
            case "save-history":
                this.window.save_history = this.settings.get_save_history ();
                break;
            case "trim-items":
                this.window.trim_items = this.settings.get_trim_items ();
                break;
            case "max-history-size":
                this.window.max_history_size = this.settings.get_max_history_size ();
                break;
            case "max-displayed-history-size":
                this.window.max_displayed_history_size = this.settings.get_max_displayed_history_size ();
                break;
            case "element-size":
                this.window.element_size = this.settings.get_element_size ();
                break;
            case "min-text-item-size":
                this.window.min_text_item_size = this.settings.get_min_text_item_size ();
                break;
            case "max-text-item-size":
                this.window.max_text_item_size = this.settings.get_max_text_item_size ();
                break;
            case "show-history":
                this.window.show_history = this.settings.get_show_history ();
                break;
            case "paste-and-pop":
                this.window.paste_and_pop = this.settings.get_paste_and_pop ();
                break;
            }
        });
    }

    private void init() {
        this.window = new Window (this);
        this.window.show_all ();
    }

    public static int main (string[] args) {
        const string gettext_package = Config.GETTEXT_PACKAGE;
        GLib.Intl.bindtextdomain (gettext_package, Config.LOCALEDIR);
        GLib.Intl.bind_textdomain_codeset (gettext_package, "UTF-8");
        GLib.Intl.textdomain (gettext_package);
        Gtk.init (ref args);
        Gtk.Settings.get_default ().gtk_application_prefer_dark_theme = true;

        var app = new Main ();
        try {
            app.register ();
        } catch (Error e) {
            stderr.printf(_("Fail to register the gtk application.\n"));
            return 1;
        }
        return app.run ();
    }
}
