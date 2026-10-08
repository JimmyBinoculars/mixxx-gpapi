// SPDX-License-Identifier: 0BSD
//
// This file is part of the Mixxx plugin SDK and is licensed under the BSD Zero
// Clause License (0BSD). See docs/plugins/LICENSE for the full text.
//
// SCOPE — this permissive license applies ONLY to these three interface files:
//   * src/plugins/api/mixxx_plugin_abi.h
//   * docs/plugins/mixxx.d.ts          (this file)
//   * docs/plugins/plugin.schema.json
// No other file is covered. The rest of the Mixxx source tree, including every
// other file under src/plugins/, remains under the GNU General Public License
// v2.0 or later (GPL-2.0-or-later).
//
// Plugin exception: combining the Mixxx plugin API with a plugin, and
// distributing that plugin under any terms (including proprietary), is
// expressly permitted. See "Plugin exception" in docs/plugins/README.md.

// TypeScript declarations for the Mixxx script plugin API (plugin API v1).
//
// Reference this file in your plugin with a triple-slash directive:
//   /// <reference path="mixxx.d.ts" />
//
// The `mixxx` object is a global in every script plugin engine.

interface MixxxControls {
    /** Returns the current value of a control. Requires `controls.read`. */
    get(group: string, item: string): number;
    /** Returns true if the control exists. Requires `controls.read`. */
    exists(group: string, item: string): boolean;
    /** Sets a control value. Requires `controls.write`. */
    set(group: string, item: string, value: number): boolean;
    /** Sets a control to 1 (useful for push buttons). Requires `controls.write`. */
    trigger(group: string, item: string): boolean;
}

interface MixxxMenuItemOptions {
    /** Top-level menu: "File", "View", "Library", "Options", "Help" or "Plugins". */
    menu?: string;
    /** Optional submenu path, e.g. "Tools/Utilities". */
    submenu?: string;
    text: string;
    shortcut?: string;
    checkable?: boolean;
    checked?: boolean;
    enabled?: boolean;
    callback?: () => void;
    /** Only used for checkable items. */
    onToggle?: (checked: boolean) => void;
}

interface MixxxMenu {
    /** Returns an id that can be passed to removeItem(). Requires `ui.menu`. */
    addItem(options: MixxxMenuItemOptions): string;
    removeItem(itemId: string): void;
    /** Removes every item added by this plugin. */
    clear(): void;
}

interface MixxxPanelListItem {
    text: string;
    /** Reported to the callback instead of the text. */
    value?: number | string | boolean;
    /** Optional icon file path shown next to the text (local, not a URL). */
    icon?: string;
}

interface MixxxPanelControl {
    /** Unique id within the panel. */
    id?: string;
    /** "label", "separator", "button", "slider", "checkbox", "number", "text", "choice", "list" or "progress". */
    type: string;
    /** Label text (or the button caption). */
    text?: string;
    value?: number | string | boolean;
    min?: number;
    max?: number;
    step?: number;
    decimals?: number;
    checked?: boolean;
    /** For type "choice". */
    choices?: string[];
    /** Initial items for type "list". */
    items?: Array<string | MixxxPanelListItem>;
    /** For type "list": icon display width in pixels (paired with iconHeight). */
    iconWidth?: number;
    /** For type "list": icon display height in pixels (paired with iconWidth). */
    iconHeight?: number;
    /** Called when this control changes (buttons call it with no arguments). */
    callback?: (value?: number | string | boolean) => void;
}

interface MixxxPanelOptions {
    title?: string;
    /** "left", "right", "top" or "bottom". */
    area?: string;
    /** Create a floating dialog window instead of a dock. */
    dialog?: boolean;
    /** Native declarative controls, rendered without QML. */
    controls?: MixxxPanelControl[];
    /** Called for any control change: id plus the new value. */
    onChange?: (id: string, value: number | string | boolean) => void;
    /** Called when the user closes the dock panel. */
    onClose?: () => void;
    /** QML file relative to the plugin directory (requires a QML-enabled build). */
    qml?: string;
    /**
     * HTML file relative to the plugin directory, rendered in an embedded web
     * view. Requires `ui.web` and a build with Qt WebEngine.
     */
    web?: string;
    /**
     * When true (and `web` is set), stream the master output to the page so it
     * can drive a visualization. Requires `audio.scope`.
     */
    audio?: boolean;
    /**
     * Dialog panels only: when false the window is rendered without chrome
     * (no Close button) so content can fill it. Defaults to true.
     */
    chrome?: boolean;
    /**
     * Dialog panels only: keep this width/height ratio while the window is
     * resized (e.g. 16 / 9). Ignored while fullscreen or maximized.
     */
    aspectRatio?: number;
    /**
     * Called when the embedded web page posts a message to the host. Requires
     * `ui.web`.
     */
    onWebMessage?: (data: unknown) => void;
}

interface MixxxUi {
    /** Returns a panel id, or "" on failure. Requires `ui.panel`. */
    addPanel(options: MixxxPanelOptions): string;
    /** As addPanel(), but always creates a floating dialog. Requires `ui.panel`. */
    addDialog(options: MixxxPanelOptions): string;
    removePanel(panelId: string): void;
    /** Updates a control without invoking its callback. Requires `ui.panel`. */
    setPanelValue(
        panelId: string,
        controlId: string,
        value: number | string | boolean,
    ): boolean;
    /** Replaces the items of a "list" control. Requires `ui.panel`. */
    setPanelItems(panelId: string, controlId: string, items: string[]): boolean;
    /** Replaces the items/values of a "list" control. Requires `ui.panel`. */
    setPanelList(
        panelId: string,
        controlId: string,
        items: Array<string | MixxxPanelListItem>,
    ): boolean;
    /**
     * Sets (or clears, with "") the icon of a single "list" row without
     * disturbing the rest of the list or its selection. `index` is 0-based.
     * Requires `ui.panel`.
     */
    setPanelListItemIcon(
        panelId: string,
        controlId: string,
        index: number,
        iconPath: string,
    ): boolean;
    /** Enables/disables a control. Requires `ui.panel`. */
    setPanelEnabled(panelId: string, controlId: string, enabled: boolean): boolean;
    /** Shows and raises a panel. Returns false while the panel is pending. */
    showPanel(panelId: string): boolean;
    hidePanel(panelId: string): boolean;
    panelVisible(panelId: string): boolean;
    /** True if the panel was created as a dialog window rather than a dock. */
    panelIsDialog(panelId: string): boolean;
    /**
     * Moves and/or resizes a panel window. A negative x/y keeps the current
     * position; a non-positive width/height keeps the current size.
     */
    setPanelGeometry(
        panelId: string,
        x: number,
        y: number,
        width: number,
        height: number,
    ): boolean;
    /** Current panel window geometry, or an empty object if unknown. */
    panelGeometry(panelId: string): { x: number; y: number; width: number; height: number };
    /** Shows the panel fullscreen (true) or restores it (false). */
    setPanelFullscreen(panelId: string, fullscreen: boolean): boolean;
    /** Toggles fullscreen; returns the resulting state. */
    togglePanelFullscreen(panelId: string): boolean;
    panelFullscreen(panelId: string): boolean;
    /** Keeps the panel above other windows. */
    setPanelAlwaysOnTop(panelId: string, onTop: boolean): boolean;
    /**
     * Evaluates JavaScript inside a web panel's page. Requires `ui.web`.
     * Returns false when the panel is not a web panel.
     */
    webEval(panelId: string, code: string): boolean;
    notify(title: string, message: string): void;
    /** Native folder chooser; approves the chosen folder. Requires `files.paths`. */
    pickFolder(title: string, startDir: string): string;
    /** Native file chooser; approves the file's folder. Requires `files.paths`. */
    pickFile(title: string, startDir: string, filter: string): string;
    /** Requires `ui.panel`. */
    openInFileManager(path: string): boolean;
    /** Requires `ui.panel`. */
    copyToClipboard(text: string): void;
    /** Requires `ui.panel`. */
    clipboardText(): string;
}

interface MixxxNetResult {
    ok: boolean;
    status?: number;
    error?: string;
    body?: string;
}

interface MixxxDownloadResult {
    ok: boolean;
    path?: string;
    error?: string;
}

interface MixxxNet {
    /** Requires `network`. */
    get(url: string, callback: (result: MixxxNetResult) => void): void;
    /** Requires `network`. */
    post(url: string, body: string, callback: (result: MixxxNetResult) => void): void;
    /** Downloads to a file inside the plugin directory. Requires `network`. */
    download(
        url: string,
        relativePath: string,
        callback: (result: MixxxDownloadResult) => void,
    ): void;
    /** As download(), with a progress callback. Requires `network`. */
    downloadWithProgress(
        url: string,
        relativePath: string,
        callback: (result: MixxxDownloadResult) => void,
        onProgress: (received: number, total: number) => void,
    ): void;
    /**
     * Downloads to an absolute file path that the user has approved (via
     * `mixxx.ui.pickFolder()` or `files.allowPath()`). Requires `network` and
     * `files.paths`.
     */
    downloadTo(
        url: string,
        absolutePath: string,
        callback: (result: MixxxDownloadResult) => void,
    ): void;
    /** As downloadTo(), with a progress callback. */
    downloadToWithProgress(
        url: string,
        absolutePath: string,
        callback: (result: MixxxDownloadResult) => void,
        onProgress: (received: number, total: number) => void,
    ): void;
    /**
     * When enabled, TLS certificate errors (for example self-signed
     * certificates) are ignored for subsequent requests. Requires `network`.
     * This weakens transport security; only enable it for servers you trust.
     */
    setIgnoreSslErrors(ignore: boolean): void;
}

interface MixxxFiles {
    /** Requires `files.read`. Relative paths resolve inside the plugin folder. */
    readText(relativePath: string): string;
    /** Requires `files.write`. */
    writeText(relativePath: string, contents: string): boolean;
    /** Deletes a file inside the plugin folder or an approved path. Requires `files.write`. */
    remove(path: string): boolean;
    /** Requires `files.read`. */
    exists(relativePath: string): boolean;
    /** Requires `files.read`. */
    list(relativeDir: string): string[];
    absolutePath(relativePath: string): string;
    /** Approves a path (and its subtree) outside the plugin folder. Requires `files.paths`. */
    allowPath(path: string): boolean;
    /** Requires `files.paths`. */
    allowedPaths(): string[];
    /** Requires `files.paths`. */
    revokePath(path: string): boolean;
}

interface MixxxTrack {
    location: string;
    title: string;
    artist: string;
    album: string;
    albumArtist: string;
    genre: string;
    comment: string;
    year: string;
    trackNumber: string;
    duration: number;
    bitrate: number;
    sampleRate: number;
}

interface MixxxLibrary {
    /** Requires `library.read`. */
    trackCount(): number;
    /** Returns an empty object when the track is not in the collection. Requires `library.read`. */
    getTrack(location: string): MixxxTrack | Record<string, never>;
    /** Loads a track into a deck (1-based). Requires `library.write`. */
    loadTrack(location: string, deck: number, play: boolean): boolean;
    /** Switches the library view to the given search. Requires `library.read`. */
    search(query: string): void;
    /** Requires `library.read`. */
    refresh(): void;
    /** Scans the library folders and indexes new/changed files. Requires `library.read`. */
    rescan(): void;
    /** Adds a folder to the library and triggers a scan. Requires `library.write`. */
    addFolder(path: string): boolean;
    /** The directories the library currently watches. Requires `library.read`. */
    folders(): string[];
}

interface MixxxProcessResult {
    ok: boolean;
    exitCode: number;
    stdout: string;
    stderr: string;
    error?: string;
}

interface MixxxProcessOptions {
    /** Working directory. */
    cwd?: string;
    /** Environment variables to set/override. */
    env?: Record<string, string>;
    /** Prepended to PATH (e.g. so a tool finds ffmpeg). */
    pathPrefix?: string;
    /** Kills the process after this many milliseconds. */
    timeoutMs?: number;
}

interface MixxxProcessHandle {
    readonly program: string;
    readonly running: boolean;
    write(data: string): void;
    closeStdin(): void;
    kill(): void;
    onStdout(callback: (chunk: string) => void): void;
    onStderr(callback: (chunk: string) => void): void;
    onExit(callback: (result: { ok: boolean; exitCode: number; error?: string }) => void): void;
}

interface MixxxProcess {
    /** Runs a program and reports once on exit. Requires `process.exec`. */
    run(program: string, args: string[], callback: (result: MixxxProcessResult) => void): void;
    /** As run(), with options. Requires `process.exec`. */
    runWithOptions(
        program: string,
        args: string[],
        options: MixxxProcessOptions,
        callback: (result: MixxxProcessResult) => void,
    ): void;
    /** Starts a program and streams its output. Requires `process.exec`. */
    spawn(program: string, args: string[]): MixxxProcessHandle | null;
    /** As spawn(), with options. Requires `process.exec`. */
    spawnWithOptions(
        program: string,
        args: string[],
        options: MixxxProcessOptions,
    ): MixxxProcessHandle | null;
    /** Blocking convenience wrapper; pass `{}` for options. Requires `process.exec`. */
    runSync(
        program: string,
        args: string[],
        options: MixxxProcessOptions,
    ): MixxxProcessResult;
}

interface MixxxSettings {
    get(key: string, defaultValue?: unknown): unknown;
    set(key: string, value: unknown): void;
    remove(key: string): void;
}

interface MixxxAudioNode {
    id: string;
    name: string;
    inputs: number;
    outputs: number;
}

interface MixxxAudioScope {
    /** Sample rate of the master output, in Hz. */
    sampleRate: number;
    /** Number of frames in each array (1024). */
    frames: number;
    /** Left-channel time-domain samples in [-1, 1]. */
    left: number[];
    /** Right-channel time-domain samples in [-1, 1]. */
    right: number[];
    /** Mono mixdown time-domain samples in [-1, 1]. */
    mono: number[];
}

interface MixxxAudio {
    /** Loads a native plugin library and adds it as a node. Requires `audio.graph`. */
    addNativeNode(libraryPath: string): string;
    removeNode(nodeId: string): boolean;
    nodes(): MixxxAudioNode[];
    connect(sourceNodeId: string, sourceChannel: number, destNodeId: string, destChannel: number): boolean;
    connectInput(inputChannel: number, destNodeId: string, destChannel: number): boolean;
    connectOutput(sourceNodeId: string, sourceChannel: number, outputChannel: number): boolean;
    disconnect(destNodeId: string, destChannel: number): boolean;
    setParam(nodeId: string, paramId: number, value: number): boolean;
    clear(): void;
    /**
     * Reads the most recent window of the master output for visualization.
     * Requires `audio.scope`. Returns an empty object when unavailable.
     */
    readScope(): MixxxAudioScope | Record<string, never>;
}

interface MixxxDownload {
    installFromUrl(
        url: string,
        sha256: string,
        callback: (result: MixxxDownloadResult & { pluginId?: string }) => void,
    ): void;
}

interface MixxxCrypto {
    /** Lowercase hex MD5 of the UTF-8 encoded text. */
    md5(text: string): string;
    /** A random lowercase hex string with 2 * byteCount characters. */
    randomHex(byteCount: number): string;
}

interface MixxxRemoteTrack {
    id: string;
    title: string;
    artist: string;
    album: string;
    albumArtist?: string;
    genre?: string;
    year?: string;
    trackNumber?: string;
    duration?: number;
    bitrate?: number;
    sampleRate?: number;
    coverArtUri?: string;
}

interface MixxxRemoteSearchResult {
    ok: boolean;
    tracks?: MixxxRemoteTrack[];
    total?: number;
    error?: string;
}

interface MixxxRemotePathResult {
    ok: boolean;
    path?: string;
    error?: string;
}

interface MixxxRemoteSource {
    /** Sidebar label, e.g. "Navidrome". Ignored when `id` is empty. */
    name: string;
    /** Optional stable id. Assigned from the name when omitted. */
    id?: string;
    /** Called to populate the source's table. Empty query means "browse all". */
    search(
        query: string,
        offset: number,
        limit: number,
        callback: (result: MixxxRemoteSearchResult) => void,
    ): void;
    /**
     * Called when a track is loaded into a deck. Download (or locate a cached
     * copy of) the track and return the local file path to load.
     */
    resolve(
        trackId: string,
        callback: (result: MixxxRemotePathResult) => void,
    ): void;
    /** Optional: download a track without loading it (offline predownload). */
    download?(
        trackId: string,
        callback: (result: MixxxRemotePathResult) => void,
    ): void;
    /** Optional: report whether a track is already downloaded. */
    isDownloaded?(
        trackId: string,
        callback: (result: { downloaded: boolean }) => void,
    ): void;
    /** Optional: delete a downloaded track. */
    removeDownload?(
        trackId: string,
        callback: (result: { ok: boolean }) => void,
    ): void;
    /** Optional: local cover-art path for a track. */
    coverArt?(
        coverArtUri: string,
        callback: (result: MixxxRemotePathResult) => void,
    ): void;
    /** Optional: total number of tracks. */
    count?(callback: (total: number) => void): void;
}

interface MixxxRemote {
    /** Registers a remote source. Requires `library.remote`. Returns its id. */
    addSource(source: MixxxRemoteSource): string;
    removeSource(sourceId: string): void;
    /** Re-runs the active query of a source. */
    refresh(sourceId: string): void;
    /** Reports download progress for a track's "Offline" column. */
    reportProgress(
        sourceId: string,
        trackId: string,
        received: number,
        total: number,
    ): void;
    /** Clears the progress reported for a track. */
    removeProgress(sourceId: string, trackId: string): void;
    /**
     * Adds an action to the track context menu for a source's tracks. The
     * callback receives the selected track ids. Returns the action id.
     */
    addTrackAction(options: {
        sourceId: string;
        text: string;
        callback: (trackIds: string[]) => void;
    }): string;
    removeTrackAction(actionId: string): void;
}

interface MixxxApi {
    readonly pluginId: string;
    readonly pluginDir: string;
    readonly mixxxVersion: string;

    readonly controls: MixxxControls;
    readonly menu: MixxxMenu;
    readonly ui: MixxxUi;
    readonly net: MixxxNet;
    readonly files: MixxxFiles;
    readonly library: MixxxLibrary;
    readonly settings: MixxxSettings;
    readonly audio: MixxxAudio;
    readonly download: MixxxDownload;
    readonly process: MixxxProcess;
    readonly crypto: MixxxCrypto;
    readonly remote: MixxxRemote;

    log(message: string): void;
    hasCapability(capability: string): boolean;
}

declare const mixxx: MixxxApi;

declare function init(api: MixxxApi): void;
declare function shutdown(): void;
