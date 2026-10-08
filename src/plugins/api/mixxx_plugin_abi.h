// SPDX-License-Identifier: 0BSD
//
// This file is part of the Mixxx plugin SDK and is licensed under the BSD Zero
// Clause License (0BSD). See docs/plugins/LICENSE for the full text.
//
// SCOPE — this permissive license applies ONLY to these three interface files:
//   * src/plugins/api/mixxx_plugin_abi.h  (this file)
//   * docs/plugins/mixxx.d.ts
//   * docs/plugins/plugin.schema.json
// No other file is covered. The rest of the Mixxx source tree, including every
// other file under src/plugins/, remains under the GNU General Public License
// v2.0 or later (GPL-2.0-or-later).
//
// Plugin exception: combining the Mixxx plugin API with a plugin, and
// distributing that plugin under any terms (including proprietary), is
// expressly permitted. See "Plugin exception" in docs/plugins/README.md.

#pragma once

// Mixxx general-purpose native plugin ABI.
//
// This header is intentionally plain C (C99) and must not include any Qt or
// C++ standard library headers. It is the *only* file a native plugin author
// needs to compile against, which keeps the plugin decoupled from the
// compiler/Qt version Mixxx was built with. The shared library is loaded with
// QLibrary and the exported `mixxx_plugin_entry` symbol is resolved at runtime.
//
// ABI stability rules:
//  * Never change the layout or the meaning of an existing member of a
//    versioned struct. Add a new struct (mixxx_plugin_v2) and a new entry
//    point instead.
//  * The host advertises the highest ABI it understands through the
//    `host_abi` argument of `mixxx_plugin_entry`. The plugin returns a pointer
//    to the highest struct it implements that is <= host_abi, or NULL if
//    there is no common version.
//  * All strings returned by the plugin must remain valid until `release()`.
//
// Real-time safety: `process()` is called from the engine callback thread. It
// must not allocate, lock, call into the operating system, or block for any
// reason. Parameter changes arrive as `mixxx_param_event` records that were
// pushed from the main thread, so `process()` never touches mutexes.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Highest plugin ABI revision implemented by this header.
#define MIXXX_PLUGIN_ABI_VERSION 1u

/// Visibility/export attribute for the entry point. Mixxx builds with
/// `-fvisibility=hidden`, so the entry point must be explicitly exported.
#if defined(_WIN32) || defined(__CYGWIN__)
#define MIXXX_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define MIXXX_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define MIXXX_PLUGIN_EXPORT
#endif

/// Opaque audio-buffer descriptor passed to `process()`.
typedef struct mixxx_audio_buffers {
    /// Array of `num_inputs` pointers, each pointing at `frames` contiguous
    /// samples (interleaved within one stream is not assumed: each pointer is
    /// one channel of one stream).
    const float* const* inputs;
    /// Array of `num_outputs` pointers, each pointing at `frames` contiguous
    /// writable samples. May alias `inputs` for in-place processing.
    float* const* outputs;
    uint32_t num_inputs;
    uint32_t num_outputs;
    uint32_t frames;
    /// Reserved for future use; must be ignored by plugins.
    uint32_t sample_rate_hz;
} mixxx_audio_buffers;

/// Parameter/event type tags for `mixxx_param_event`.
enum {
    MIXXX_PARAM_FLOAT = 0,
    MIXXX_PARAM_INT = 1,
    MIXXX_PARAM_BOOL = 2,
    /// A note-on style trigger; `id` selects the destination, `f` is the
    /// velocity in the range [0, 1].
    MIXXX_PARAM_TRIGGER = 3,
};

/// A single lock-free parameter change pushed by the host to the plugin.
/// The struct is deliberately trivially copyable and fixed-size.
typedef struct mixxx_param_event {
    /// Plugin-defined parameter id. Plugins advertise their parameter list
    /// through `mixxx_plugin_v1::describe_param` at `prepare()` time.
    uint32_t id;
    /// One of the MIXXX_PARAM_* tags.
    uint32_t type;
    union {
        float f;
        int32_t i;
        uint32_t b;
    } value;
    /// Monotonic sequence number, used to detect dropped events. May be 0 if
    /// the host does not track sequence numbers.
    uint32_t sequence;
} mixxx_param_event;

/// Description of a single parameter, returned by the optional
/// `describe_param` callback so host UIs (and script bindings) can build
/// generic controls.
typedef struct mixxx_param_info {
    uint32_t id;
    /// MIXXX_PARAM_* tag.
    uint32_t type;
    float min_value;
    float max_value;
    float default_value;
    const char* name;
    const char* unit;
} mixxx_param_info;

/// Version 1 of the native DSP plugin vtable.
///
/// The struct is extended by future ABI versions only by appending fields at
/// the end. Plugins that do not need a feature return a minimal trailing
/// struct; hosts must check `abi_version` and only call callbacks that are
/// present for that version.
typedef struct mixxx_plugin_v1 {
    /// Must equal MIXXX_PLUGIN_ABI_VERSION (or lower for compatibility shims).
    uint32_t abi_version;
    /// Human-readable name. Never NULL.
    const char* (*name)(void);
    /// Human-readable version string. May return NULL or "".
    const char* (*version)(void);
    /// Number of input streams (channels). 0 for generators.
    uint32_t (*num_inputs)(void);
    /// Number of output streams (channels). 0 for effects that are pure sinks.
    uint32_t (*num_outputs)(void);
    /// Called once before processing starts (and again after a sample-rate
    /// change). Must not block. `max_frames` is the largest block that will be
    /// handed to `process()`.
    bool (*prepare)(double sample_rate, uint32_t max_frames);
    /// Real-time audio callback. `buffers` is valid only for the duration of
    /// the call.
    void (*process)(const mixxx_audio_buffers* buffers,
            const mixxx_param_event* events,
            uint32_t num_events,
            void* user_data);
    /// Called when processing stops / before unload. Must not block.
    void (*release)(void);
    /// Optional; may be NULL. Number of parameters advertised.
    uint32_t (*num_params)(void);
    /// Optional; may be NULL. Fills `out` for parameter index `i`.
    bool (*describe_param)(uint32_t index, mixxx_param_info* out);
    /// Optional; may be NULL. Opaque per-instance state handed back to
    /// `process()` as `user_data`. Returning NULL is allowed.
    void* (*create_state)(void);
    /// Optional; may be NULL. Called to destroy the state returned by
    /// `create_state()`.
    void (*destroy_state)(void* user_data);
} mixxx_plugin_v1;

/// Signature of the single exported entry point.
///
/// A plugin implements:
///
///     const mixxx_plugin_v1* mixxx_plugin_entry(uint32_t host_abi);
///
/// and returns a pointer to its immutable vtable. The returned pointer must
/// stay valid until the library is unloaded.
typedef const mixxx_plugin_v1* (*mixxx_plugin_entry_fn)(uint32_t host_abi);

/// Convenience declaration macro. Plugin sources do not need to include any
/// other header to export a conforming entry point.
#define MIXXX_PLUGIN_ENTRY_NAME "mixxx_plugin_entry"

#ifdef __cplusplus
} // extern "C"

/// Declares/defines the entry point with the expected name and export
/// visibility. Works in both C and C++ (C++ gets `extern "C"` linkage).
#define MIXXX_DECLARE_PLUGIN_ENTRY()                                          \
    extern "C" MIXXX_PLUGIN_EXPORT const mixxx_plugin_v1* mixxx_plugin_entry( \
            uint32_t host_abi)
#else
#define MIXXX_DECLARE_PLUGIN_ENTRY()                                          \
    MIXXX_PLUGIN_EXPORT const mixxx_plugin_v1* mixxx_plugin_entry(uint32_t host_abi)
#endif
