// Bundled example script plugin.
//
// The global `mixxx` object exposes the capability-gated API. This plugin
// requests `ui.menu` (to add a menu item), `controls.read` (to read a deck
// control) and `ui.panel` (to show a notification and a dock panel).

let menuItemId = null;
let panelId = null;

function deck1Playing() {
    return mixxx.controls.get("[Channel1]", "play") > 0;
}

function deck1Bpm() {
    return mixxx.controls.get("[Channel1]", "bpm");
}

function sayHello() {
    mixxx.ui.notify(
        "Hello Plugin",
        "Deck 1 playing: " + deck1Playing() +
            "\nDeck 1 BPM: " + deck1Bpm().toFixed(2)
    );
}

export function init(api) {
    mixxx.log("Hello Plugin is initializing");

    menuItemId = mixxx.menu.addItem({
        menu: "Plugins",
        text: "Say Hello",
        shortcut: "Ctrl+Shift+H",
        callback: sayHello,
    });

    // A native (non-QML) dock panel with declarative controls.
    panelId = mixxx.ui.addPanel({
        title: "Hello",
        area: "right",
        controls: [
            { type: "label", text: "Hello Plugin" },
            { type: "button", id: "say", text: "Say Hello", callback: sayHello },
            {
                type: "slider",
                id: "preview",
                text: "Preview",
                min: 0,
                max: 1,
                value: 0.5,
                callback(value) {
                    mixxx.log("panel preview = " + value.toFixed(2));
                },
            },
            {
                type: "checkbox",
                id: "verbose",
                text: "Verbose logging",
                checked: false,
                callback(enabled) {
                    mixxx.settings.set("verbose", enabled);
                    mixxx.log("verbose = " + enabled);
                },
            },
        ],
    });
}

export function shutdown() {
    if (menuItemId) {
        mixxx.menu.removeItem(menuItemId);
        menuItemId = null;
    }
    if (panelId) {
        mixxx.ui.removePanel(panelId);
        panelId = null;
    }
    mixxx.log("Hello Plugin has shut down");
}
