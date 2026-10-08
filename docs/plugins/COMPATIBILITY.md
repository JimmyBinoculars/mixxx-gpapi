# Plugin API compatibility

This fork tracks an upstream Mixxx release and layers a plugin system on top.
The upstream (Mixxx) version and the plugin API version advance independently, so
the table below is the authoritative mapping between them. Keep it in sync when
you cut a fork release.

| Plugin API | Native ABI | Mixxx base | Fork revision | Notes |
|---|---|---|---|---|
| 1 | `mixxx_plugin_v1` | 2.5.6 | `gpapi.1` | Initial plugin system. |

## Where the numbers live

- **CMake** (`CMakeLists.txt`):
  - `MIXXX_FORK_REVISION` — bump for every published fork build; reset to 1 when
    the Mixxx base changes.
  - `MIXXX_PLUGIN_API_VERSION` — the script/JS plugin contract. Bump **only** on
    a breaking change.
  - `MIXXX_PLUGIN_API_MIN_VERSION` — the oldest plugin API this build still
    loads; raise it when you drop support for an old API.
- **Generated header** `src/version.h` (from `src/version.h.in`) exposes
  `MIXXX_FORK_VERSION`, `MIXXX_PLUGIN_API_VERSION`,
  `MIXXX_PLUGIN_API_MIN_VERSION`.
- **Runtime** (`util/versionstore.h`): `VersionStore::forkVersion()`,
  `VersionStore::pluginApiVersion()`, `VersionStore::pluginApiMinVersion()`.
  These are shown in **Preferences → Plugins**, the About dialog, and
  `mixxx.log`.
- **Native ABI**: `MIXXX_PLUGIN_ABI_VERSION` in
  `src/plugins/api/mixxx_plugin_abi.h`, negotiated via the `host_abi` argument
  of `mixxx_plugin_entry`.

## Compatibility rule

A build loads a plugin when all of the following hold:

```
MIXXX_PLUGIN_API_MIN_VERSION <= manifest.apiVersion <= MIXXX_PLUGIN_API_VERSION
manifest.minMixxxVersion     <= running Mixxx version <= manifest.maxMixxxVersion
```

(enforced by `PluginManifestReader::isSupportedByThisBuild`). Native plugins are
additionally gated by the ABI handshake: the plugin returns the highest
`mixxx_plugin_vN` it implements that is `<= host_abi`.

## Cutting a new fork release

1. Update to the new upstream Mixxx version and set `project(mixxx VERSION …)`
   accordingly.
2. If the script API broke, raise `MIXXX_PLUGIN_API_VERSION`. If you dropped
   support for an older API, raise `MIXXX_PLUGIN_API_MIN_VERSION`.
3. Bump `MIXXX_FORK_REVISION` (reset it to 1 when the Mixxx base changes).
4. Add a row to the table above.

## Shipping an additive API change

Add a **capability** string (plus its namespace/method) and update the
Capabilities table in `README.md`. Do **not** bump `apiVersion`; existing
plugins keep working and new ones opt in via the capability.
