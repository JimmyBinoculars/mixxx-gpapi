#include "plugins/audio/pluginnode.h"

#include <algorithm>

#include "util/assert.h"

namespace mixxx {
namespace plugins {

NativePluginNode::NativePluginNode(const mixxx_plugin_v1* pVtable)
        : m_pVtable(pVtable) {
    DEBUG_ASSERT(m_pVtable);
}

NativePluginNode::~NativePluginNode() {
    release();
}

QString NativePluginNode::name() const {
    if (m_pVtable->name != nullptr) {
        return QString::fromUtf8(m_pVtable->name());
    }
    return QStringLiteral("native plugin");
}

uint32_t NativePluginNode::numInputs() const {
    if (m_pVtable->num_inputs != nullptr) {
        // The graph hands `process()` fixed-size channel pointer arrays, so a
        // node must never report more channels than the host can carry.
        return std::min<uint32_t>(m_pVtable->num_inputs(), kMaxNodeChannels);
    }
    return 2;
}

uint32_t NativePluginNode::numOutputs() const {
    if (m_pVtable->num_outputs != nullptr) {
        return std::min<uint32_t>(m_pVtable->num_outputs(), kMaxNodeChannels);
    }
    return 2;
}

bool NativePluginNode::prepare(double sampleRate, uint32_t maxFrames) {
    release();
    if (m_pVtable->create_state != nullptr) {
        m_pUserData = m_pVtable->create_state();
    }
    if (m_pVtable->prepare != nullptr) {
        if (!m_pVtable->prepare(sampleRate, maxFrames)) {
            // The plugin refused to prepare. Tear down the state created above
            // so it is not leaked: release() cannot do this because
            // m_prepared is still false and release() returns early.
            if (m_pUserData != nullptr && m_pVtable->destroy_state != nullptr) {
                m_pVtable->destroy_state(m_pUserData);
            }
            m_pUserData = nullptr;
            return false;
        }
    }
    m_prepared.store(true, std::memory_order_release);
    return true;
}

void NativePluginNode::process(const float* const* inputs,
        float* const* outputs,
        uint32_t frames,
        const mixxx_param_event* events,
        uint32_t eventCount) {
    Q_UNUSED(events);
    Q_UNUSED(eventCount);
    if (!m_prepared.load(std::memory_order_acquire) ||
            m_pVtable->process == nullptr) {
        return;
    }

    // Drain pending parameter changes without allocating.
    uint32_t drained = 0;
    while (drained < kMaxEventsPerBlock) {
        const mixxx_param_event* pFront = m_paramQueue.front();
        if (pFront == nullptr) {
            break;
        }
        m_drainBuffer[drained++] = *pFront;
        m_paramQueue.pop();
    }

    mixxx_audio_buffers buffers;
    buffers.inputs = inputs;
    buffers.outputs = outputs;
    buffers.num_inputs = numInputs();
    buffers.num_outputs = numOutputs();
    buffers.frames = frames;
    buffers.sample_rate_hz = 0;

    m_pVtable->process(&buffers, m_drainBuffer, drained, m_pUserData);
}

void NativePluginNode::release() {
    if (!m_prepared.exchange(false)) {
        return;
    }
    if (m_pVtable->release != nullptr) {
        m_pVtable->release();
    }
    if (m_pUserData != nullptr && m_pVtable->destroy_state != nullptr) {
        m_pVtable->destroy_state(m_pUserData);
    }
    m_pUserData = nullptr;
}

bool NativePluginNode::pushParam(const mixxx_param_event& event) {
    return m_paramQueue.try_push(event);
}

uint32_t NativePluginNode::numParams() const {
    if (m_pVtable->num_params != nullptr) {
        return m_pVtable->num_params();
    }
    return 0;
}

bool NativePluginNode::describeParam(uint32_t index, mixxx_param_info* out) const {
    if (m_pVtable->describe_param == nullptr || out == nullptr) {
        return false;
    }
    return m_pVtable->describe_param(index, out);
}

} // namespace plugins
} // namespace mixxx
