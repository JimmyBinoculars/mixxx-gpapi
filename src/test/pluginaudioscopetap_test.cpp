#include <gtest/gtest.h>

#include <vector>

#include "plugins/audio/audioscopetap.h"

namespace mixxx {
namespace plugins {

TEST(AudioScopeTapTest, DisabledTapReturnsSilence) {
    AudioScopeTap tap;
    tap.prepare(48000.0);

    std::vector<float> buffer(8, 0.5f);
    tap.write(buffer.data(), static_cast<int>(buffer.size()));

    float left[AudioScopeTap::kWindowFrames];
    float right[AudioScopeTap::kWindowFrames];
    float mono[AudioScopeTap::kWindowFrames];
    EXPECT_EQ(tap.readLatest(4, left, right, mono), 4);
    EXPECT_FLOAT_EQ(left[0], 0.0f);
    EXPECT_FLOAT_EQ(right[3], 0.0f);
    EXPECT_DOUBLE_EQ(tap.sampleRate(), 48000.0);
}

TEST(AudioScopeTapTest, ReadsMostRecentWindow) {
    AudioScopeTap tap;
    tap.prepare(44100.0);
    tap.setEnabled(true);

    constexpr int kFrames = 8;
    std::vector<float> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        buffer[2 * i] = static_cast<float>(i);
        buffer[2 * i + 1] = static_cast<float>(-i);
    }
    tap.write(buffer.data(), kFrames * 2);

    float left[AudioScopeTap::kWindowFrames];
    float right[AudioScopeTap::kWindowFrames];
    float mono[AudioScopeTap::kWindowFrames];
    const int got = tap.readLatest(kFrames, left, right, mono);
    EXPECT_EQ(got, kFrames);
    for (int i = 0; i < kFrames; ++i) {
        EXPECT_FLOAT_EQ(left[i], static_cast<float>(i));
        EXPECT_FLOAT_EQ(right[i], static_cast<float>(-i));
        EXPECT_FLOAT_EQ(mono[i], 0.0f);
    }
}

TEST(AudioScopeTapTest, WrapsAroundRingBuffer) {
    AudioScopeTap tap;
    tap.prepare(48000.0);
    tap.setEnabled(true);

    constexpr int kBlock = 1024;
    std::vector<float> buffer(kBlock * 2);
    for (int block = 0; block < 20; ++block) {
        for (int i = 0; i < kBlock; ++i) {
            const float value = static_cast<float>(block * kBlock + i);
            buffer[2 * i] = value;
            buffer[2 * i + 1] = value;
        }
        tap.write(buffer.data(), kBlock * 2);
    }

    float left[AudioScopeTap::kWindowFrames];
    float right[AudioScopeTap::kWindowFrames];
    float mono[AudioScopeTap::kWindowFrames];
    tap.readLatest(4, left, right, mono);
    // The final frame written has sample index 20 * 1024 - 1 == 20479.
    EXPECT_FLOAT_EQ(left[3], 20479.0f);
    EXPECT_FLOAT_EQ(right[3], 20479.0f);
}

TEST(AudioScopeTapTest, PadsInsufficientHistoryWithSilence) {
    AudioScopeTap tap;
    tap.prepare(48000.0);
    tap.setEnabled(true);

    std::vector<float> buffer(2, 1.0f); // a single frame
    tap.write(buffer.data(), 2);

    float left[AudioScopeTap::kWindowFrames];
    float right[AudioScopeTap::kWindowFrames];
    float mono[AudioScopeTap::kWindowFrames];
    tap.readLatest(4, left, right, mono);
    EXPECT_FLOAT_EQ(left[0], 0.0f);
    EXPECT_FLOAT_EQ(left[3], 1.0f);
}

// The mono output is the average of left and right. Use distinct values so a
// missing mixdown cannot hide behind symmetric input.
TEST(AudioScopeTapTest, MixesLeftAndRightIntoMono) {
    AudioScopeTap tap;
    tap.prepare(48000.0);
    tap.setEnabled(true);

    const float frame[2] = {2.0f, 4.0f};
    tap.write(frame, 2);

    float left[1];
    float right[1];
    float mono[1];
    ASSERT_EQ(tap.readLatest(1, left, right, mono), 1);
    EXPECT_FLOAT_EQ(left[0], 2.0f);
    EXPECT_FLOAT_EQ(right[0], 4.0f);
    EXPECT_FLOAT_EQ(mono[0], 3.0f);
}

TEST(AudioScopeTapTest, ReadLatestClampsToWindowFrames) {
    AudioScopeTap tap;
    tap.prepare(48000.0);
    tap.setEnabled(true);

    std::vector<float> buffer(static_cast<size_t>(AudioScopeTap::kWindowFrames) * 2, 1.0f);
    tap.write(buffer.data(), static_cast<int>(buffer.size()));

    std::vector<float> left(AudioScopeTap::kWindowFrames);
    std::vector<float> right(AudioScopeTap::kWindowFrames);
    std::vector<float> mono(AudioScopeTap::kWindowFrames);
    const int tooMany = AudioScopeTap::kWindowFrames * 4;
    EXPECT_EQ(tap.readLatest(tooMany, left.data(), right.data(), mono.data()),
            AudioScopeTap::kWindowFrames);
}

TEST(AudioScopeTapTest, ReadLatestRejectsInvalidArguments) {
    AudioScopeTap tap;
    tap.prepare(48000.0);
    tap.setEnabled(true);

    float left[4] = {};
    float right[4] = {};
    float mono[4] = {};
    EXPECT_EQ(tap.readLatest(0, left, right, mono), 0);
    EXPECT_EQ(tap.readLatest(-1, left, right, mono), 0);
    EXPECT_EQ(tap.readLatest(4, nullptr, right, mono), 0);
    EXPECT_EQ(tap.readLatest(4, left, nullptr, mono), 0);
    EXPECT_EQ(tap.readLatest(4, left, right, nullptr), 0);
}

TEST(AudioScopeTapTest, WriteIgnoresTooShortBuffer) {
    AudioScopeTap tap;
    tap.prepare(48000.0);
    tap.setEnabled(true);

    const float frame[2] = {7.0f, 7.0f};
    tap.write(frame, 2);
    const float incomplete[1] = {99.0f};
    tap.write(incomplete, 1); // fewer than two samples: dropped

    float left[1];
    float right[1];
    float mono[1];
    ASSERT_EQ(tap.readLatest(1, left, right, mono), 1);
    EXPECT_FLOAT_EQ(left[0], 7.0f);
    EXPECT_FLOAT_EQ(right[0], 7.0f);
}

} // namespace plugins
} // namespace mixxx
