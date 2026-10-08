# Mixxx General-Purpose Plugin API

This fork adds a general-purpose plugin system to Mixxx with two runtimes:

| Runtime | Language | Trust | Use for |
|---|---|---|---|
| `script` | JavaScript ES modules in an isolated `QJSEngine` | Capability-gated with a consent dialog | Menus, UI, automation, network, library access, the REPL |
| `native` | Shared library exposing a versioned C ABI | Trusted (compiled code) | Real-time audio DSP |

> **Security warning.** The JavaScript runtime is **not** a security sandbox.
> Capability gating protects against accidents and sloppy plugins, not a
> determined attacker. Only install plugins you trust.

## Where plugins live

Plugins are folders containing a `plugin.json` manifest:

- User plugins: `~/.mixxx/plugins/<folder>/plugin.json`
- Bundled examples: `<resource path>/plugins/<folder>/plugin.json`

The user folder takes precedence: a bundled plugin is ignored if a user plugin
with the same `id` exists. You can drop a plugin folder in by hand or install
one from the **Preferences → Plugins** page (from a `.zip` / `.tar.gz` archive
or a URL).

## The manifest

```jsonc
{
  "id": "com.example.gain-boost",   // required, [A-Za-z0-9._-]
  "name": "Gain Boost",             // required
  "description": "A tiny gain stage.",
  "author": "You",
  "version": "1.0.0",
  "apiVersion": 1,                  // required; bump only on breaking API changes
  "runtime": "script",              // "script" (default) or "native"
  "entry": {
    "script": "main.js",            // relative to the plugin folder
    "native": "libgainboost.so"
  },
  "capabilities": ["audio.graph", "controls.read", "ui.menu"],
  "minMixxxVersion": "2.5.6",
  "maxMixxxVersion": "3.0"
}
```

Unknown capability strings are reported in the preferences page but ignored.

## Versioning and compatibility

This fork tracks an upstream Mixxx release; on top of it, two independent
numbers control plugin compatibility:

| Number | Where | Meaning |
|---|---|---|
| Mixxx version | `VersionStore::version()` / `versionNumber()` | Upstream base, e.g. `2.5.6` |
| Fork revision | `VersionStore::forkVersion()` | This fork's release of that base, e.g. `gpapi.1` |
| Plugin API version | manifest `apiVersion`; `VersionStore::pluginApiVersion()` | Contract for script plugins (a breaking-change counter) |
| Native ABI version | `MIXXX_PLUGIN_ABI_VERSION` | Contract for native plugins (versioned C structs) |
| Host range | manifest `minMixxxVersion` / `maxMixxxVersion` | Which Mixxx releases a plugin supports |

A manifest declares the contract it was written against (`apiVersion`) and the
host range it supports (`minMixxxVersion`/`maxMixxxVersion`). A build loads a
plugin only when `MIXXX_PLUGIN_API_MIN_VERSION <= apiVersion <=
MIXXX_PLUGIN_API_VERSION` and the running Mixxx version is within the declared
range. The current values are shown in **Preferences → Plugins** and in
`mixxx.log`.

**Bump policy.** Increment `apiVersion` only when you *break* the script API
(rename, remove, or change the meaning of an existing call). Additive features
get a new **capability** and do *not* require an API bump. The native C ABI is
versioned separately by appending new structs (`mixxx_plugin_v2`, ...).

See [`COMPATIBILITY.md`](COMPATIBILITY.md) for the mapping between plugin API
versions and fork releases.

## Capabilities

| Capability | Namespace | Meaning |
|---|---|---|
| `controls.read` | `mixxx.controls` | Read `ControlObject` values |
| `controls.write` | `mixxx.controls` | Write `ControlObject` values |
| `ui.menu` | `mixxx.menu` | Add/remove main menu items |
| `ui.panel` | `mixxx.ui` | Dock panels and notifications |
| `network` | `mixxx.net` | HTTP requests and downloads |
| `files.read` | `mixxx.files` | Read files inside the plugin folder |
| `files.write` | `mixxx.files` | Write files inside the plugin folder |
| `library.read` | `mixxx.library` | Read track metadata |
| `library.write` | `mixxx.library` | Load tracks into decks |
| `audio.graph` | `mixxx.audio` | Insert nodes into the real-time audio graph |
| `download` | `mixxx.download` | Install other plugins from a URL |
| `process.exec` | `mixxx.process` | Run external programs |
| `files.paths` | `mixxx.files` | Read/write in folders the user approves |
| `ui.web` | `mixxx.ui` | Open embedded web windows (e.g. WebGL visualizations) |
| `audio.scope` | `mixxx.audio` | Read the master output audio for visualization |
| `library.remote` | `mixxx.remote` | Add an internet source to the library and resolve its tracks on demand |

Script plugins are shown a consent dialog the first time they load, and again
whenever an update requests new capabilities. Grants are stored in `mixxx.cfg`
and can be changed per plugin in **Preferences → Plugins**.

## Script plugin contract

`main.js` is an ES module exporting `init` and `shutdown`:

```js
export function init(api) {
    mixxx.log("hello");
    const id = mixxx.menu.addItem({
        menu: "Plugins",
        text: "Do the thing",
        callback() {
            mixxx.controls.set("[Channel1]", "play", 1);
        },
    });
}

export function shutdown() {
    // release resources, undo registrations
}
```

The module is hot-reloaded whenever `main.js` changes on disk.

### The `mixxx` API

See [`mixxx.d.ts`](mixxx.d.ts) for the complete typed surface. Highlights:

```js
// Controls
mixxx.controls.get("[Channel1]", "play");
mixxx.controls.set("[Channel1]", "play", 1);
mixxx.controls.trigger("[Channel1]", "play");

// Menu + UI
mixxx.menu.addItem({ menu: "Plugins", text: "Hi", callback() {} });
mixxx.ui.notify("Title", "Message");

// Panels use declarative controls and need no QML:
const panel = mixxx.ui.addPanel({
    title: "My Panel",
    area: "right",
    controls: [
        { type: "button", id: "run", text: "Run", callback() {} },
        { type: "slider", id: "gain", text: "Gain", min: 0, max: 1, value: 0.5,
          callback(value) { mixxx.log("gain " + value); } },
        { type: "checkbox", id: "on", text: "Enabled", checked: true,
          callback(enabled) {} },
    ],
});
mixxx.ui.setPanelValue(panel, "gain", 0.75); // update without firing callback

// Network
mixxx.net.get("https://example.org/data.json", (result) => {
    if (result.ok) console.log(result.body);
});

// Files (sandboxed to the plugin folder)
const text = mixxx.files.readText("state.json");
mixxx.files.writeText("state.json", JSON.stringify({ hello: 1 }));

// Library
const track = mixxx.library.getTrack("/music/track.mp3");
mixxx.library.loadTrack("/music/track.mp3", 1, true); // deck 1, play

// Per-plugin settings
mixxx.settings.set("volume", 0.8);
const volume = mixxx.settings.get("volume", 1.0);

// Audio graph
const node = mixxx.audio.addNativeNode("libmydsp.so");
mixxx.audio.connectInput(0, node, 0);
mixxx.audio.connectInput(1, node, 1);
mixxx.audio.connectOutput(node, 0, 0);
mixxx.audio.connectOutput(node, 1, 1);
mixxx.audio.setParam(node, 0, 0.5);
```

## Running external programs

Requires the `process.exec` capability. `run()` reports once when the program
exits:

```js
mixxx.process.run("ffmpeg", ["-version"], (result) => {
    if (result.ok) mixxx.log(result.stdout);
});

// Streaming handle with options (working dir, env, PATH prefix):
const handle = mixxx.process.spawnWithOptions("yt-dlp", [url], {
    cwd: destination,
    pathPrefix: "/usr/bin",       // so yt-dlp finds ffmpeg
    env: { HOME: destination },   // cookies/cache location
    timeoutMs: 600000,
});
handle.onStdout((chunk) => mixxx.log(chunk));
handle.onStderr((chunk) => mixxx.log(chunk));
handle.onExit((result) => {
    mixxx.library.addFolder(destination); // index what we downloaded
    mixxx.library.rescan();
});
```

## Outside the plugin folder

`mixxx.files` is confined to the plugin folder unless the plugin also requests
`files.paths`. Ask the user for a destination with `mixxx.ui.pickFolder()` — the
chosen folder is approved automatically. `mixxx.files.allowPath()`,
`allowedPaths()` and `revokePath()` manage approvals explicitly:

```js
const dest = mixxx.ui.pickFolder("Choose download folder", "~/Music");
if (dest) {
    const dirs = mixxx.files.list(dest);          // works outside the plugin dir
    mixxx.files.writeText(dest + "/.keep", "");
    mixxx.ui.openInFileManager(dest);
}
```

## Panels

`mixxx.ui.addPanel()` creates a dock widget on the main window. Panels are
rendered **natively** (no QML needed) from a declarative `controls` array:

| `type` | Behaviour | Value passed to `callback` |
|---|---|---|
| `label` | Static text | — |
| `separator` | Horizontal line | — |
| `button` | Push button | none |
| `slider` | Horizontal slider (`min`, `max`, `value`) | number |
| `number` | Spin box (`min`, `max`, `step`, `decimals`) | number |
| `checkbox` | Check box (`checked`) | boolean |
| `text` | Text field (`value`) | string |
| `choice` | Combo box (`choices`, `value`) | string |
| `list` | List box (`items`: strings or `{text, value, icon}`) | selected value (or text) |
| `progress` | Read-only progress bar (0–100, set via `setPanelValue`) | — |

Each control may define its own `callback`, and/or the panel may define a global
`onChange(id, value)`. `mixxx.ui.setPanelValue(panelId, controlId, value)`
updates **any** control (including labels and progress bars) without invoking
its callback.

```js
const panel = mixxx.ui.addPanel({
    title: "Downloader",
    onClose() { /* the user closed the dock */ },
    controls: [
        { type: "text", id: "url", text: "URL" },
        { type: "button", id: "go", text: "Download", callback: start },
        { type: "progress", id: "progress" },
        { type: "label", id: "status", text: "Idle" },
        { type: "list", id: "results", callback: (id) => play(id) },
    ],
});
mixxx.ui.setPanelValue(panel, "status", "Downloading…");
mixxx.ui.setPanelValue(panel, "progress", 42);
mixxx.ui.setPanelEnabled(panel, "go", false);          // prevent double submit
mixxx.ui.setPanelList(panel, "results", [
    "Plain string row",
    { text: "Title · 3:45", value: "dQw4w9WgXcQ" },    // value reported
    { text: "With art", value: "abc123", icon: "/path/thumb.jpg" },
]);
mixxx.ui.showPanel(panel);        // reveal without recreating (keeps state)
mixxx.ui.hidePanel(panel);
```

List items may carry an optional `icon` (a local file path), and the list
control may set `iconWidth`/`iconHeight` for the thumbnail size. Use
`mixxx.ui.setPanelListItemIcon(panel, "results", row, path)` to fill in a row's
icon later (e.g. after an async download) without rebuilding the list or losing
the selection. Icons are loaded from disk, not from URLs.

If the build has QML support, a panel may instead (or additionally) set `qml` to
a file relative to the plugin directory.

### Web panels

On builds with **Qt WebEngine** (configure with `-DWEBENGINE=ON`, the default
when the component is installed), a panel may set `web` to an HTML file relative
to the plugin directory. The page is rendered in an embedded view and can be
made a modeless, chromeless, fullscreen-capable window:

```js
const panel = mixxx.ui.addPanel({
    title: "My Visualizer",
    dialog: true,
    chrome: false,          // no Close button; content fills the window
    web: "web/index.html",
    audio: true,            // stream the master output (requires audio.scope)
    onWebMessage(data) {    // messages posted by the page
        mixxx.log("page says " + JSON.stringify(data));
    },
});
mixxx.ui.togglePanelFullscreen(panel);
mixxx.ui.webEval(panel, "myPageFunction()");
```

Web pages talk back to the host through the Qt WebChannel object
`mixxxBridge`:

```js
new QWebChannel(qt.webChannelTransport, (channel) => {
    channel.objects.mixxxBridge.postMessage(JSON.stringify({ type: "ready" }));
});
```

When `audio: true`, the host pushes 1024 time-domain samples (as base64
`Uint8Array`s) about 60 times per second by calling
`window.MixxxBridge.pushAudio(left, right, mono)` in the page. This matches
Butterchurn's headless `render({ audioLevels })` mode, so a visualizer needs no
Web Audio graph of its own. Non-web scripts can read the same window through
`mixxx.audio.readScope()` (requires `audio.scope`).

The bundled **Butterchurn Visualizer** example (`res/plugins/butterchurn`)
demonstrates all of this: it bundles Butterchurn plus the full preset packs and
opens a separate, movable, fullscreen-capable window driven by the master mix.

## Remote library sources

A script plugin can add an internet music source to the library with the
`library.remote` capability. The source appears as a child of the **Online**
sidebar node; its tracks are shown in the normal track table and the global
search box queries them. Tracks are **not** stored in the local collection:
loading one into a deck first calls the plugin's `resolve()` to download (or
locate a cached copy of) the file.

```js
const sourceId = mixxx.remote.addSource({
    name: "Navidrome",
    // Populate the table. An empty query means "browse all".
    search(query, offset, limit, cb) {
        cb({ ok: true, tracks: [
            { id: "123", title: "Track", artist: "Artist", album: "Album",
              duration: 214, bitrate: 320 },
        ], total: 1 });
    },
    // Called when the user loads a track into a deck. Return a local path.
    resolve(trackId, cb) {
        cb({ ok: true, path: "/home/me/.mixxx/plugins/.../cache/" + trackId });
    },
    // Optional, enables the "Download for offline" context-menu action.
    download(trackId, cb) { /* download without loading */ },
    isDownloaded(trackId, cb) { cb({ downloaded: false }); },
    removeDownload(trackId, cb) { cb({ ok: true }); },
});
```

The right-click menu on a remote track always offers **Download for offline**
(when `download()` is implemented) and the tracks' `Offline` column reflects
`mixxx.remote.reportProgress()` / `removeProgress()` calls. Custom actions can
be added with `mixxx.remote.addTrackAction({ sourceId, text, callback })`.
Remote tracks can also be dragged onto a deck; the download is triggered on
drop. While a remote track is being resolved/downloaded for a deck, the deck
immediately shows the track's known tags (title/artist) and the waveform
overview displays a "Downloading..." hint (with a percentage when the source
reports byte progress via `mixxx.remote.reportProgress()`); both hand off to the
normal load/analysis flow once the local file is ready. If the source implements
`coverArt(coverArtUri, cb)`, selecting a remote track also shows its artwork in
the library cover panel without downloading the audio. Requires `library.remote`
(and usually `network`, `files.*`).

## Native plugins (C ABI)

Native plugins export a single symbol:

```c
const mixxx_plugin_v1* mixxx_plugin_entry(uint32_t host_abi);
```

The complete header is [`src/plugins/api/mixxx_plugin_abi.h`](../../src/plugins/api/mixxx_plugin_abi.h).
A minimal example lives in [`examples/plugins/gain`](../../examples/plugins/gain).

If a plugin implements the optional `num_params` / `describe_param` callbacks,
Mixxx generates a control panel for it automatically and wires the widgets to the
plugin's lock-free parameter queue (`pushParam`), so it needs no UI code of its own.

Rules:

- No Qt or C++ types cross the boundary. The ABI is plain C99.
- `process()` runs on the **engine callback thread**: no allocation, no locks,
  no syscalls, no blocking. Parameter changes are delivered as lock-free
  `mixxx_param_event` records pushed from the main thread.
- Bump the ABI by adding a new versioned struct, never by changing an existing
  one. A plugin returns the highest version it implements that is `<= host_abi`.

Native plugins that declare `audio.graph` are inserted, in load order, as a
chain on the master bus: `master → node1 → node2 → … → master`. Scripts can
reconfigure the graph arbitrarily through `mixxx.audio`.

## Under the hood

- `PluginManager` discovers plugins, owns their lifecycle and hot reload, and
  is exposed through `CoreServices`.
- Script plugins run in their own `QJSEngine` with a global `mixxx` object made
  of capability-checking QObject proxies (`src/plugins/script/pluginjsproxies.*`).
- The audio graph (`src/plugins/audio/audiograph.*`) is immutable: edits build
  a new snapshot on the main thread and atomically swap it; the callback only
  reads the current snapshot. Retired snapshots are reclaimed once the callback
  has moved on.
- The Developer Tools dialog has a **Console** tab wired to a REPL engine with
  the full API bound.

## Verifying downloads

The store verifies an optional SHA-256. Cryptographic **signature** verification
is not implemented in this fork (no key infrastructure is shipped), so treat
remote installs as you would any downloaded executable.

## License

This fork is a derivative of Mixxx and, like Mixxx, is distributed under the
**GNU General Public License v2.0 or later** (see the top-level `LICENSE`).
Every file, including this documentation and all other files under
`src/plugins/`, is GPL-2.0-or-later **except** the three plugin SDK interface
files below, which are licensed under the permissive **0BSD** license so that
plugin authors are not required to use the GPL:

- [`src/plugins/api/mixxx_plugin_abi.h`](../../src/plugins/api/mixxx_plugin_abi.h)
- [`docs/plugins/mixxx.d.ts`](mixxx.d.ts)
- [`docs/plugins/plugin.schema.json`](plugin.schema.json)

That 0BSD grant applies to **only those three files** and nothing else. The
full text and scope statement are in [`LICENSE`](LICENSE). This does not change
the license of Mixxx itself or of any other file in the tree.

### Plugin exception

As a special exception, the copyright holders of this fork grant you permission
to link, load, or otherwise combine the Mixxx plugin API — this fork's plugin
host, the native C ABI in `src/plugins/api/mixxx_plugin_abi.h`, and the script
API — with plugins of your own, and to copy, distribute and convey those
plugins under **whatever terms you choose**, including proprietary,
closed-source, or otherwise GPL-incompatible terms. This is true regardless of
whether the plugin runs in a separate process or is loaded into Mixxx's address
space.

This exception is an **additional permission**. It does not relicense any file,
and it does not alter the GPL-2.0-or-later terms that apply to the rest of the
Mixxx source tree; it only permits the combination of that tree with a
separately-licensed plugin.

> **Scope of this grant.** These copyright holders can grant this exception
> only for the code they own — this fork's plugin subsystem and the SDK
> interface files. Portions of the tree that originate from upstream Mixxx
> remain GPL-2.0-or-later and are owned by their respective authors. If a plugin
> were ever held to form a *single work* with those upstream portions, this
> exception cannot waive rights held by people other than the fork authors. In
> that scenario, an arm's-length boundary (e.g. running native DSP in a separate
> process) is the definitive solution. This note is informational, not legal
> advice.
