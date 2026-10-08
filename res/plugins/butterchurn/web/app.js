// Butterchurn visualizer driven by time-domain audio pushed from the host.
//
// Butterchurn is created with a null AudioContext (headless mode) and fed one
// frame of samples per animation frame via
// `visualizer.render({ elapsedTime, audioLevels })`.
(function () {
    "use strict";

    const canvas = document.getElementById("canvas");
    const errorBox = document.getElementById("error");

    let visualizer = null;
    let presets = {};
    let presetNames = [];
    let index = 0;
    let blendSeconds = 2.7;
    let lastFrameTime = performance.now();
    let lastAudioReport = 0;
    let readyAnnounced = false;
    let autoSwitchEnabled = false;
    let autoSwitchIntervalMs = 30000;
    let lastAutoSwitch = performance.now();

    function fail(message) {
        errorBox.style.display = "block";
        errorBox.textContent = message;
    }

    // The Butterchurn bundles are UMD builds whose browser global is a module
    // namespace object; the usable class may live under `.default`. Resolve
    // whichever shape is present.
    function resolveModule(mod, member) {
        if (!mod) {
            return null;
        }
        if (typeof mod[member] === "function") {
            return mod;
        }
        if (mod.default && typeof mod.default[member] === "function") {
            return mod.default;
        }
        return null;
    }

    function gatherPresets() {
        const packs = [
            window.butterchurnPresets,
            window.butterchurnPresetsExtra,
            window.butterchurnPresetsExtra2,
            window.butterchurnPresetsMD1,
        ];
        const merged = {};
        for (const pack of packs) {
            const resolved = resolveModule(pack, "getPresets");
            if (!resolved) {
                continue;
            }
            const entries = resolved.getPresets();
            for (const name of Object.keys(entries)) {
                merged[name] = entries[name];
            }
        }
        return merged;
    }

    function resize() {
        // The host constrains the window to 16:9, so the canvas simply fills it.
        const dpr = window.devicePixelRatio || 1;
        const width = Math.max(1, window.innerWidth);
        const height = Math.max(1, window.innerHeight);
        canvas.width = Math.round(width * dpr);
        canvas.height = Math.round(height * dpr);
        canvas.style.width = width + "px";
        canvas.style.height = height + "px";
        if (visualizer) {
            visualizer.setRendererSize(canvas.width, canvas.height, {
                pixelRatio: 1,
                textureRatio: 1,
            });
        }
    }

    function announcePreset() {
        if (!visualizer || presetNames.length === 0) {
            return;
        }
        window.MixxxBridge.postToHost({
            type: "presetChanged",
            name: presetNames[index],
            index: index,
            total: presetNames.length,
        });
    }

    function loadIndex(newIndex) {
        if (!visualizer || presetNames.length === 0) {
            return;
        }
        index = ((newIndex % presetNames.length) + presetNames.length) %
                presetNames.length;
        const name = presetNames[index];
        visualizer.loadPreset(presets[name], blendSeconds);
        announcePreset();
    }

    function pickRandomIndex() {
        if (presetNames.length <= 1) {
            return index;
        }
        let next = index;
        while (next === index) {
            next = Math.floor(Math.random() * presetNames.length);
        }
        return next;
    }

    // Reports whether the incoming audio frames are silent (all bytes == 128),
    // so the host can show an "Audio: live / no signal" indicator.
    function isSilent(levels) {
        const data = levels.timeByteArray;
        for (let i = 0; i < data.length; i += 4) {
            if (data[i] !== 128) {
                return false;
            }
        }
        return true;
    }

    function render() {
        window.requestAnimationFrame(render);
        if (!visualizer) {
            return;
        }
        const now = performance.now();
        const elapsedTime = Math.min(1, (now - lastFrameTime) / 1000);
        lastFrameTime = now;
        const levels = window.MixxxBridge
                ? window.MixxxBridge.audioLevels()
                : undefined;
        if (levels && now - lastAudioReport > 500) {
            lastAudioReport = now;
            window.MixxxBridge.postToHost({
                type: "audio",
                active: !isSilent(levels),
            });
        }
        if (autoSwitchEnabled && now - lastAutoSwitch >= autoSwitchIntervalMs) {
            lastAutoSwitch = now;
            loadIndex(pickRandomIndex());
        }
        try {
            visualizer.render({ elapsedTime: elapsedTime, audioLevels: levels });
        } catch (error) {
            fail("Render error: " + error);
        }
    }

    function boot() {
        const butterchurn = resolveModule(window.butterchurn, "createVisualizer");
        if (!butterchurn) {
            fail("Butterchurn failed to load. Globals present: butterchurn=" +
                    (typeof window.butterchurn) + ", butterchurnPresets=" +
                    (typeof window.butterchurnPresets));
            return;
        }
        presets = gatherPresets();
        presetNames = Object.keys(presets).sort();
        if (presetNames.length === 0) {
            fail("No Butterchurn presets were found.");
            return;
        }

        resize();
        visualizer = butterchurn.createVisualizer(null, canvas, {
            width: canvas.width,
            height: canvas.height,
            pixelRatio: 1,
            textureRatio: 1,
            meshWidth: 64,
            meshHeight: 48,
        });

        const extraImages = resolveModule(window.butterchurnExtraImages, "getImages");
        if (typeof visualizer.loadExtraImages === "function" && extraImages) {
            try {
                visualizer.loadExtraImages(extraImages.getImages());
            } catch (error) {
                /* extra textures are optional */
            }
        }

        index = 0;
        loadIndex(0);
        window.requestAnimationFrame(render);

        window.MixxxBridge.postToHost({
            type: "presets",
            names: presetNames,
        });

        if (window.MixxxBridge.ready) {
            window.MixxxVisualizer.onHostReady();
        }
    }

    window.MixxxVisualizer = {
        nextPreset() {
            loadIndex(index + 1);
        },
        prevPreset() {
            loadIndex(index - 1);
        },
        randomPreset() {
            loadIndex(pickRandomIndex());
        },
        setPreset(name) {
            const found = presetNames.indexOf(name);
            if (found >= 0) {
                loadIndex(found);
            }
        },
        setBlend(seconds) {
            blendSeconds = Number(seconds) || 0;
        },
        // Enable/disable automatic switching to a random preset every
        // `seconds` seconds. The timer runs in this render loop.
        setAutoSwitch(enabled, seconds) {
            autoSwitchEnabled = !!enabled;
            const parsed = Number(seconds);
            if (isFinite(parsed) && parsed > 0) {
                autoSwitchIntervalMs = parsed * 1000;
            }
            lastAutoSwitch = performance.now();
        },
        announce() {
            window.MixxxBridge.postToHost({
                type: "presets",
                names: presetNames,
            });
            announcePreset();
        },
        onHostReady() {
            if (readyAnnounced) {
                return;
            }
            readyAnnounced = true;
            window.MixxxBridge.postToHost({ type: "ready" });
        },
    };

    window.addEventListener("resize", resize);
    window.addEventListener("load", boot);

    // Double-click the visual to toggle the host window's fullscreen state.
    canvas.addEventListener("dblclick", function () {
        window.MixxxBridge.postToHost({ type: "toggleFullscreen" });
    });
})();
