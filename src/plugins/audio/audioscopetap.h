#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace mixxx {
namespace plugins {

/// Lock-free tap of the master output for visualization plugins.
///
/// The real-time engine thread writes interleaved stereo frames into a fixed
/// ring buffer and publishes a monotonically increasing write index. The main
/// / GUI thread reads back the most recent window without locking. The window
/// may occasionally tear if the producer wraps around the region being read;
/// that is acceptable for visualization and keeps the audio thread free of
/// locks and allocations.
///
/// Only one producer (the engine callback) and one consumer (the GUI) are
/// supported, matching the SPSC discipline used elsewhere in the plugin audio
/// graph.
class AudioScopeTap {
  public:
    /// Number of frames handed to the visualizer per read.
    static constexpr int kWindowFrames = 1024;
    /// Ring capacity. Large enough that a 1024-frame window is never fully
    /// overwritten between reads at any realistic block rate.
    static constexpr int kRingFrames = 1 << 14;

    /// Prepares the ring for a sample rate. Called on the main thread before
    /// processing starts (or when the sample rate changes).
    void prepare(double sampleRate);

    /// Enables/disables capture. Must be called from the main thread.
    void setEnabled(bool enabled);
    bool isEnabled() const {
        return m_enabled.load(std::memory_order_relaxed);
    }

    /// Real-time entry point: copies an interleaved stereo buffer. Never
    /// blocks, allocates or touches the filesystem.
    void write(const float* interleaved, int numSamples);

    /// Reads the most recent `frames` frames (clamped to kWindowFrames) into
    /// the deinterleaved left/right buffers plus a mono mixdown. Missing
    /// history is reported as silence (zeroes). Returns the number of frames
    /// written to the outputs.
    int readLatest(int frames,
            float* outLeft,
            float* outRight,
            float* outMono) const;

    double sampleRate() const {
        return m_sampleRate.load(std::memory_order_relaxed);
    }

  private:
    std::vector<float> m_ringLeft;
    std::vector<float> m_ringRight;
    std::atomic<uint64_t> m_writeIndex{0};
    std::atomic<bool> m_enabled{false};
    std::atomic<double> m_sampleRate{0.0};
};

} // namespace plugins
} // namespace mixxx
