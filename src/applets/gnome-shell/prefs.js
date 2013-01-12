const GPaste = imports.gi.GPaste;

function init() {
}

function buildPrefsWidget() {
    let notebook = new GPaste.SettingsUiNotebook();
    notebook.fill();
    notebook.show_all();
    return notebook;
}
