// Bundled example script plugin: Butterchurn (Milkdrop) WebGL visualizer.
//
// Opens a modeless, chromeless window that renders a Butterchurn visualizer
// driven by Mixxx's master output. The bundled web page (web/visualizer.html)
// runs Butterchurn with a null AudioContext and is fed time-domain samples by
// the host (see the `audio` panel option, which requires `audio.scope`).
//
// A small dock panel offers preset navigation and fullscreen toggling.

let visualPanelId = null;
let controlPanelId = null;
let menuItemId = null;
let fullscreenItemId = null;
let controlsItemId = null;
let currentPreset = "";
let latestPresetNames = [];
let lastAudioActive = null;
let autoEnabled = false;
let autoIntervalSeconds = 30;

const kLastPresetKey = "lastPreset";
const kAutoEnabledKey = "autoSwitch";
const kAutoIntervalKey = "autoInterval";

function applyAutoSettings() {
    webEval(
        "MixxxVisualizer.setAutoSwitch(" +
            (autoEnabled ? "true" : "false") +
            "," +
            autoIntervalSeconds +
            ")"
    );
}

function webEval(code) {
    if (visualPanelId) {
        mixxx.ui.webEval(visualPanelId, code);
    }
}

// The web window is created lazily so no WebEngine view is instantiated until
// the user actually opens the visualizer.
function ensureVisualPanel() {
    if (visualPanelId) {
        return visualPanelId;
    }
    visualPanelId = mixxx.ui.addPanel({
        title: "Butterchurn Visualizer",
        dialog: true,
        chrome: false,
        aspectRatio: 16 / 9,
        web: "web/visualizer.html",
        audio: true,
        onWebMessage: onWebMessage,
    });
    if (!visualPanelId) {
        mixxx.log(
            "Butterchurn: web panels are unavailable. This build needs Qt WebEngine and the ui.web capability."
        );
    }
    return visualPanelId;
}

function showVisualizer() {
    if (!ensureVisualPanel()) {
        return;
    }
    mixxx.ui.showPanel(visualPanelId);
}

function toggleVisualizer() {
    if (visualPanelId && mixxx.ui.panelVisible(visualPanelId)) {
        mixxx.ui.hidePanel(visualPanelId);
    } else {
        showVisualizer();
    }
}

function toggleFullscreen() {
    // Open the window first if it is not up yet, then toggle fullscreen.
    if (!ensureVisualPanel()) {
        return;
    }
    const fullscreen = mixxx.ui.togglePanelFullscreen(visualPanelId);
    if (controlPanelId) {
        mixxx.ui.setPanelValue(controlPanelId, "state",
            fullscreen ? "Fullscreen" : "Windowed");
    }
    mixxx.log("Butterchurn: toggle fullscreen -> " + fullscreen + " (panel " + visualPanelId + ")");
}

function nextPreset() {
    webEval("MixxxVisualizer.nextPreset()");
}

function prevPreset() {
    webEval("MixxxVisualizer.prevPreset()");
}

function randomPreset() {
    webEval("MixxxVisualizer.randomPreset()");
}

function choosePreset(name) {
    webEval("MixxxVisualizer.setPreset(" + JSON.stringify(String(name)) + ")");
}

function audioStatusText() {
    if (!mixxx.hasCapability("audio.scope")) {
        return "Audio: permission not granted (enable audio.scope in Preferences → Plugins)";
    }
    if (lastAudioActive === null) {
        return "Audio: waiting…";
    }
    return lastAudioActive ? "Audio: live" : "Audio: no signal";
}

// The controls are a standalone dialog window, created on demand (so nothing
// pops up until the user asks for it).
function ensureControlPanel() {
    if (controlPanelId) {
        return controlPanelId;
    }
    controlPanelId = mixxx.ui.addPanel({
        title: "Butterchurn Controls",
        dialog: true,
        controls: [
            { type: "label", id: "current", text: currentPreset || "(no preset)" },
            { type: "label", id: "state", text: "Windowed" },
            { type: "label", id: "audio", text: audioStatusText() },
            { type: "button", id: "open", text: "Open Visualizer", callback: showVisualizer },
            { type: "button", id: "next", text: "Next Preset", callback: nextPreset },
            { type: "button", id: "prev", text: "Previous Preset", callback: prevPreset },
            { type: "button", id: "random", text: "Random Preset", callback: randomPreset },
            { type: "button", id: "full", text: "Fullscreen", callback: toggleFullscreen },
            { type: "list", id: "presets", callback: choosePreset },
            { type: "separator" },
            {
                type: "checkbox",
                id: "auto",
                text: "Auto-switch presets",
                checked: autoEnabled,
                callback(value) {
                    autoEnabled = value;
                    mixxx.settings.set(kAutoEnabledKey, value);
                    applyAutoSettings();
                },
            },
            {
                type: "number",
                id: "interval",
                text: "Switch every (seconds)",
                min: 5,
                max: 3600,
                step: 5,
                decimals: 0,
                value: autoIntervalSeconds,
                callback(value) {
                    autoIntervalSeconds = value;
                    mixxx.settings.set(kAutoIntervalKey, value);
                    applyAutoSettings();
                },
            },
        ],
    });
    if (controlPanelId && latestPresetNames.length > 0) {
        mixxx.ui.setPanelList(controlPanelId, "presets", latestPresetNames);
    }
    return controlPanelId;
}

function toggleControls() {
    if (controlPanelId && mixxx.ui.panelVisible(controlPanelId)) {
        mixxx.ui.hidePanel(controlPanelId);
    } else {
        ensureControlPanel();
        mixxx.ui.showPanel(controlPanelId);
    }
}

function onWebMessage(data) {
    if (!data || !data.type) {
        return;
    }
    if (data.type === "ready") {
        // The page is up: ask for the preset list and restore the last preset.
        webEval("MixxxVisualizer.announce()");
        const last = mixxx.settings.get(kLastPresetKey, "");
        if (last) {
            webEval("MixxxVisualizer.setPreset(" + JSON.stringify(String(last)) + ")");
        }
        applyAutoSettings();
    } else if (data.type === "presets") {
        latestPresetNames = data.names || [];
        if (controlPanelId) {
            mixxx.ui.setPanelList(controlPanelId, "presets", latestPresetNames);
        }
    } else if (data.type === "presetChanged") {
        currentPreset = data.name || "";
        mixxx.settings.set(kLastPresetKey, currentPreset);
        if (controlPanelId) {
            mixxx.ui.setPanelValue(controlPanelId, "current", currentPreset);
        }
    } else if (data.type === "toggleFullscreen") {
        toggleFullscreen();
    } else if (data.type === "audio") {
        lastAudioActive = !!data.active;
        if (controlPanelId) {
            mixxx.ui.setPanelValue(controlPanelId, "audio", audioStatusText());
        }
    }
}

export function init(api) {
    mixxx.log("Butterchurn Visualizer is initializing");

    autoEnabled = mixxx.settings.get(kAutoEnabledKey, false) === true;
    autoIntervalSeconds = Number(mixxx.settings.get(kAutoIntervalKey, 30));
    if (!isFinite(autoIntervalSeconds) || autoIntervalSeconds <= 0) {
        autoIntervalSeconds = 30;
    }

    menuItemId = mixxx.menu.addItem({
        menu: "Plugins",
        text: "Butterchurn Visualizer",
        callback: toggleVisualizer,
    });

    fullscreenItemId = mixxx.menu.addItem({
        menu: "Plugins",
        submenu: "Butterchurn",
        text: "Toggle Fullscreen",
        shortcut: "Ctrl+Shift+F",
        callback: toggleFullscreen,
    });

    controlsItemId = mixxx.menu.addItem({
        menu: "Plugins",
        submenu: "Butterchurn",
        text: "Controls…",
        shortcut: "Ctrl+Shift+B",
        callback: toggleControls,
    });
}

export function shutdown() {
    if (menuItemId) {
        mixxx.menu.removeItem(menuItemId);
        menuItemId = null;
    }
    if (fullscreenItemId) {
        mixxx.menu.removeItem(fullscreenItemId);
        fullscreenItemId = null;
    }
    if (controlsItemId) {
        mixxx.menu.removeItem(controlsItemId);
        controlsItemId = null;
    }
    if (controlPanelId) {
        mixxx.ui.removePanel(controlPanelId);
        controlPanelId = null;
    }
    if (visualPanelId) {
        mixxx.ui.removePanel(visualPanelId);
        visualPanelId = null;
    }
    mixxx.log("Butterchurn Visualizer has shut down");
}
