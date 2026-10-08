// Example native DSP plugin for the Mixxx general-purpose plugin API.
//
// Build with the accompanying CMakeLists.txt or directly:
//   cc -shared -fPIC -I<repo>/src/plugins/api gain_plugin.c -o libgain.so
//
// Copy the resulting library next to a plugin.json like the one in this
// directory, then drop the folder into ~/.mixxx/plugins/.

#include <stdlib.h>

#include "mixxx_plugin_abi.h"

typedef struct {
    float gain;
} gain_state;

static const char* gain_name(void) {
    return "Example Gain";
}

static const char* gain_version(void) {
    return "1.0.0";
}

static uint32_t gain_num_inputs(void) {
    return 2;
}

static uint32_t gain_num_outputs(void) {
    return 2;
}

static void* gain_create_state(void) {
    gain_state* pState = (gain_state*)malloc(sizeof(gain_state));
    if (pState != NULL) {
        pState->gain = 1.0f;
    }
    return pState;
}

static void gain_destroy_state(void* user_data) {
    free(user_data);
}

static bool gain_prepare(double sample_rate, uint32_t max_frames) {
    (void)sample_rate;
    (void)max_frames;
    return true;
}

static void gain_release(void) {
}

static void gain_process(const mixxx_audio_buffers* buffers,
        const mixxx_param_event* events,
        uint32_t num_events,
        void* user_data) {
    gain_state* pState = (gain_state*)user_data;
    if (pState == NULL) {
        return;
    }

    for (uint32_t i = 0; i < num_events; ++i) {
        if (events[i].id == 0 && events[i].type == MIXXX_PARAM_FLOAT) {
            pState->gain = events[i].value.f;
        }
    }

    for (uint32_t channel = 0; channel < buffers->num_outputs; ++channel) {
        float* output = buffers->outputs[channel];
        if (buffers->num_inputs == 0) {
            // Generator with no inputs: emit silence rather than dereferencing
            // a missing input channel.
            for (uint32_t frame = 0; frame < buffers->frames; ++frame) {
                output[frame] = 0.0f;
            }
            continue;
        }
        const float* input = channel < buffers->num_inputs
                ? buffers->inputs[channel]
                : buffers->inputs[0];
        for (uint32_t frame = 0; frame < buffers->frames; ++frame) {
            output[frame] = input[frame] * pState->gain;
        }
    }
}

static uint32_t gain_num_params(void) {
    return 1;
}

static bool gain_describe_param(uint32_t index, mixxx_param_info* out) {
    if (index != 0 || out == NULL) {
        return false;
    }
    out->id = 0;
    out->type = MIXXX_PARAM_FLOAT;
    out->min_value = 0.0f;
    out->max_value = 4.0f;
    out->default_value = 1.0f;
    out->name = "Gain";
    out->unit = "x";
    return true;
}

static const mixxx_plugin_v1 kGainPlugin = {
        MIXXX_PLUGIN_ABI_VERSION,
        gain_name,
        gain_version,
        gain_num_inputs,
        gain_num_outputs,
        gain_prepare,
        gain_process,
        gain_release,
        gain_num_params,
        gain_describe_param,
        gain_create_state,
        gain_destroy_state,
};

MIXXX_DECLARE_PLUGIN_ENTRY() {
    if (host_abi < MIXXX_PLUGIN_ABI_VERSION) {
        return NULL;
    }
    return &kGainPlugin;
}
