const GPaste = imports.gi.GPaste;
const Gtk = imports.gi.Gtk;

function init() {
}

function buildPrefsWidget() {
    let widget = new GPaste.SettingsUiWidget({ orientation: Gtk.Orientation.VERTICAL, margin: 12 });
    widget.show_all();
    return widget;
}
