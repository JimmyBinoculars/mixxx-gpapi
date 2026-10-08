#include "plugins/audio/audioscopetap.h"

#include <algorithm>

namespace mixxx {
namespace plugins {

void AudioScopeTap::prepare(double sampleRate) {
    m_ringLeft.assign(kRingFrames, 0.0f);
    m_ringRight.assign(kRingFrames, 0.0f);
    m_writeIndex.store(0, std::memory_order_relaxed);
    m_sampleRate.store(sampleRate, std::memory_order_relaxed);
}

void AudioScopeTap::setEnabled(bool enabled) {
    m_enabled.store(enabled, std::memory_order_relaxed);
}

void AudioScopeTap::write(const float* interleaved, int numSamples) {
    if (!m_enabled.load(std::memory_order_relaxed) || interleaved == nullptr ||
            numSamples < 2) {
        return;
    }
    if (m_ringLeft.empty()) {
        // prepare() has not run yet; drop silently rather than allocate on the
        // real-time thread.
        return;
    }

    const int frames = numSamples / 2;
    uint64_t index = m_writeIndex.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        const int slot = static_cast<int>(index % kRingFrames);
        m_ringLeft[slot] = interleaved[2 * i];
        m_ringRight[slot] = interleaved[2 * i + 1];
        ++index;
    }
    m_writeIndex.store(index, std::memory_order_release);
}

int AudioScopeTap::readLatest(int frames,
        float* outLeft,
        float* outRight,
        float* outMono) const {
    if (frames <= 0 || outLeft == nullptr || outRight == nullptr ||
            outMono == nullptr) {
        return 0;
    }
    frames = std::min(frames, kWindowFrames);

    const uint64_t end = m_writeIndex.load(std::memory_order_acquire);
    const int available = static_cast<int>(
            std::min<uint64_t>(end, static_cast<uint64_t>(kRingFrames)));
    const int count = std::min(frames, available);
    const int padding = frames - count;

    for (int i = 0; i < padding; ++i) {
        outLeft[i] = 0.0f;
        outRight[i] = 0.0f;
    }
    for (int i = 0; i < count; ++i) {
        const uint64_t index = end - static_cast<uint64_t>(count) +
                static_cast<uint64_t>(i);
        const int slot = static_cast<int>(index % kRingFrames);
        outLeft[padding + i] = m_ringLeft[slot];
        outRight[padding + i] = m_ringRight[slot];
    }
    for (int i = 0; i < frames; ++i) {
        outMono[i] = 0.5f * (outLeft[i] + outRight[i]);
    }
    return frames;
}

} // namespace plugins
} // namespace mixxx
