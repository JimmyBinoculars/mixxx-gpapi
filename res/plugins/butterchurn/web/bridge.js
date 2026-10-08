// Host <-> page bridge.
//
// Audio arrives from the host as base64-encoded time-domain samples via
// `window.MixxxBridge.pushAudio(...)`, invoked by the embedding C++ code. The
// page posts events back to the host through the QWebChannel bridge.
(function () {
    "use strict";

    const FRAMES = 1024;

    function zeros() {
        const a = new Uint8Array(FRAMES);
        a.fill(128); // 128 == silence for unsigned time-domain data
        return a;
    }

    const audioLevels = {
        timeByteArray: zeros(),
        timeByteArrayL: zeros(),
        timeByteArrayR: zeros(),
    };

    function decodeBase64(b64) {
        const binary = atob(b64 || "");
        const out = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) {
            out[i] = binary.charCodeAt(i);
        }
        return out;
    }

    let host = null;

    window.MixxxBridge = {
        ready: false,

        pushAudio(b64Left, b64Right, b64Mono) {
            audioLevels.timeByteArrayL = decodeBase64(b64Left);
            audioLevels.timeByteArrayR = decodeBase64(b64Right);
            audioLevels.timeByteArray = decodeBase64(b64Mono);
        },

        audioLevels() {
            return audioLevels;
        },

        postToHost(message) {
            if (!host) {
                return;
            }
            try {
                host.postMessage(JSON.stringify(message));
            } catch (error) {
                /* ignore */
            }
        },
    };

    function notifyReady() {
        window.MixxxBridge.ready = true;
        if (window.MixxxVisualizer && window.MixxxVisualizer.onHostReady) {
            window.MixxxVisualizer.onHostReady();
        }
    }

    window.addEventListener("load", function () {
        if (typeof QWebChannel === "undefined" || !window.qt ||
                !window.qt.webChannelTransport) {
            // No WebChannel: the visualizer still renders, but the host cannot
            // be notified of preset changes.
            notifyReady();
            return;
        }
        new QWebChannel(window.qt.webChannelTransport, function (channel) {
            host = channel.objects.mixxxBridge || null;
            if (host && host.log) {
                host.log("Butterchurn page bridge ready");
            }
            notifyReady();
        });
    });
})();
