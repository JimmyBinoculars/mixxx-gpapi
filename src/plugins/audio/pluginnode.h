#pragma once

#include <QString>
#include <atomic>
#include <memory>
#include <vector>

#include <rigtorp/SPSCQueue.h>

#include "plugins/api/mixxx_plugin_abi.h"
#include "util/types.h"

namespace mixxx {
namespace plugins {

/// Maximum number of input/output channels a single graph node may expose.
/// The real-time processing loop uses fixed-size arrays of this size, so every
/// node is clamped to it before being handed to `process()`.
constexpr uint32_t kMaxNodeChannels = 8;

/// Host-side representation of a node in the plugin audio graph.
///
/// `process()` is called on the real-time engine thread and must be
/// allocation-, lock- and syscall-free. Parameter changes are delivered out of
/// band through the lock-free queue implemented by the concrete node.
class PluginNode {
  public:
    virtual ~PluginNode() = default;

    /// Stable identifier used by the graph API (not necessarily unique).
    virtual QString name() const = 0;

    virtual uint32_t numInputs() const = 0;
    virtual uint32_t numOutputs() const = 0;

    /// Called on the main thread before processing starts (and whenever the
    /// sample rate changes). May allocate.
    virtual bool prepare(double sampleRate, uint32_t maxFrames) = 0;

    /// Real-time callback.
    virtual void process(const float* const* inputs,
            float* const* outputs,
            uint32_t frames,
            const mixxx_param_event* events,
            uint32_t eventCount) = 0;

    /// Called on the main thread after processing stops. May deallocate.
    virtual void release() = 0;

    /// Pushes a parameter change from the main thread. Lock-free and
    /// non-blocking; returns false if the queue is full.
    virtual bool pushParam(const mixxx_param_event& event) = 0;

    /// Optional: number of parameters exposed to the host. Defaults to 0, i.e.
    /// nodes that do not describe parameters get no generated control panel.
    virtual uint32_t numParams() const {
        return 0;
    }

    /// Optional: fills `out` for parameter `index`. Returns false if unknown.
    virtual bool describeParam(uint32_t index, mixxx_param_info* out) const {
        (void)index;
        (void)out;
        return false;
    }
};

/// Wraps a value of `mixxx_plugin_v1` loaded from a native shared library.
class NativePluginNode final : public PluginNode {
  public:
    explicit NativePluginNode(const mixxx_plugin_v1* pVtable);
    ~NativePluginNode() override;

    QString name() const override;
    uint32_t numInputs() const override;
    uint32_t numOutputs() const override;
    bool prepare(double sampleRate, uint32_t maxFrames) override;
    void process(const float* const* inputs,
            float* const* outputs,
            uint32_t frames,
            const mixxx_param_event* events,
            uint32_t eventCount) override;
    void release() override;
    bool pushParam(const mixxx_param_event& event) override;

    uint32_t numParams() const override;
    bool describeParam(uint32_t index, mixxx_param_info* out) const override;

  private:
    static constexpr size_t kMaxQueuedEvents = 256;
    static constexpr uint32_t kMaxEventsPerBlock = 64;

    const mixxx_plugin_v1* const m_pVtable;
    void* m_pUserData = nullptr;
    std::atomic<bool> m_prepared{false};
    rigtorp::SPSCQueue<mixxx_param_event> m_paramQueue{kMaxQueuedEvents};
    mixxx_param_event m_drainBuffer[kMaxEventsPerBlock];
};

} // namespace plugins
} // namespace mixxx
