const GPaste = imports.gi.GPaste;
const Gtk = imports.gi.Gtk;

const Gettext = imports.gettext;
const ExtensionUtils = imports.misc.extensionUtils;

function init() {
    let metadata = ExtensionUtils.getCurrentExtension().metadata;
    Gettext.bindtextdomain(metadata.gettext-package, metadata.localedir);
}

function buildPrefsWidget() {
    let widget = new GPaste.SettingsUiWidget({ orientation: Gtk.Orientation.VERTICAL, margin: 12 });
    widget.show_all();
    return widget;
}
