#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "plugins/audio/audiograph.h"
#include "plugins/audio/pluginnode.h"

namespace mixxx {
namespace plugins {

namespace {

// A trivial stereo gain node used to exercise the graph.
class TestGainNode final : public PluginNode {
  public:
    explicit TestGainNode(float gain)
            : m_gain(gain) {
    }
    QString name() const override {
        return QStringLiteral("test-gain");
    }
    uint32_t numInputs() const override {
        return 2;
    }
    uint32_t numOutputs() const override {
        return 2;
    }
    bool prepare(double, uint32_t) override {
        return true;
    }
    void process(const float* const* inputs,
            float* const* outputs,
            uint32_t frames,
            const mixxx_param_event*,
            uint32_t) override {
        for (uint32_t channel = 0; channel < 2; ++channel) {
            for (uint32_t i = 0; i < frames; ++i) {
                outputs[channel][i] = inputs[channel][i] * m_gain;
            }
        }
    }
    void release() override {
    }
    bool pushParam(const mixxx_param_event&) override {
        return true;
    }

  private:
    float m_gain;
};

mixxx_param_event makeFloatEvent(uint32_t id, float value) {
    mixxx_param_event event{};
    event.id = id;
    event.type = MIXXX_PARAM_FLOAT;
    event.value.f = value;
    event.sequence = 0;
    return event;
}

// --- An in-test native plugin vtable (no dlopen) ---------------------------

struct VtGainState {
    float gain = 1.0f;
};

void* vtCreateState() {
    return new VtGainState();
}
void vtDestroyState(void* state) {
    delete static_cast<VtGainState*>(state);
}
bool vtPrepare(double, uint32_t) {
    return true;
}
void vtRelease() {
}
uint32_t vtNumInputs() {
    return 2;
}
uint32_t vtNumOutputs() {
    return 2;
}
const char* vtName() {
    return "vt-gain";
}
const char* vtVersion() {
    return "1.0.0";
}
uint32_t vtNumParams() {
    return 1;
}
bool vtDescribeParam(uint32_t index, mixxx_param_info* out) {
    if (index != 0 || out == nullptr) {
        return false;
    }
    out->id = 0;
    out->type = MIXXX_PARAM_FLOAT;
    out->min_value = 0.0f;
    out->max_value = 2.0f;
    out->default_value = 1.0f;
    out->name = "gain";
    out->unit = "";
    return true;
}
void vtProcess(const mixxx_audio_buffers* buffers,
        const mixxx_param_event* events,
        uint32_t numEvents,
        void* userData) {
    auto* state = static_cast<VtGainState*>(userData);
    if (state == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < numEvents; ++i) {
        if (events[i].id == 0 && events[i].type == MIXXX_PARAM_FLOAT) {
            state->gain = events[i].value.f;
        }
    }
    for (uint32_t channel = 0; channel < buffers->num_outputs; ++channel) {
        for (uint32_t i = 0; i < buffers->frames; ++i) {
            buffers->outputs[channel][i] = buffers->inputs[channel][i] * state->gain;
        }
    }
}

const mixxx_plugin_v1 kTestVtable{
        MIXXX_PLUGIN_ABI_VERSION,
        vtName,
        vtVersion,
        vtNumInputs,
        vtNumOutputs,
        vtPrepare,
        vtProcess,
        vtRelease,
        vtNumParams,
        vtDescribeParam,
        vtCreateState,
        vtDestroyState};

} // namespace

TEST(PluginAudioGraphTest, EmptyGraphIsPassthrough) {
    AudioGraph graph;
    graph.updateRenderParameters(48000.0, 1024);
    std::vector<float> buffer(8, 0.5f);
    graph.processInPlace(buffer.data(), static_cast<int>(buffer.size()));
    for (float value : buffer) {
        EXPECT_FLOAT_EQ(value, 0.5f);
    }
}

TEST(PluginAudioGraphTest, AppliesConnectedGainNode) {
    AudioGraph graph;
    const QString nodeId = graph.addNode(std::make_shared<TestGainNode>(0.5f));
    ASSERT_FALSE(nodeId.isEmpty());
    ASSERT_TRUE(graph.connectExternalInput(0, nodeId, 0));
    ASSERT_TRUE(graph.connectExternalInput(1, nodeId, 1));
    ASSERT_TRUE(graph.connectToExternalOutput(nodeId, 0, 0));
    ASSERT_TRUE(graph.connectToExternalOutput(nodeId, 1, 1));
    graph.updateRenderParameters(48000.0, 1024);

    std::vector<float> buffer(8, 1.0f);
    graph.processInPlace(buffer.data(), static_cast<int>(buffer.size()));
    for (float value : buffer) {
        EXPECT_NEAR(value, 0.5f, 1e-6f);
    }
}

TEST(PluginAudioGraphTest, ChannelsAreIndependent) {
    AudioGraph graph;
    const QString nodeId = graph.addNode(std::make_shared<TestGainNode>(2.0f));
    ASSERT_FALSE(nodeId.isEmpty());
    ASSERT_TRUE(graph.connectExternalInput(0, nodeId, 0));
    ASSERT_TRUE(graph.connectExternalInput(1, nodeId, 1));
    ASSERT_TRUE(graph.connectToExternalOutput(nodeId, 0, 0));
    ASSERT_TRUE(graph.connectToExternalOutput(nodeId, 1, 1));
    graph.updateRenderParameters(48000.0, 1024);

    // Interleaved stereo: L=1.0, R=0.25
    std::vector<float> buffer{1.0f, 0.25f, 1.0f, 0.25f};
    graph.processInPlace(buffer.data(), static_cast<int>(buffer.size()));
    EXPECT_NEAR(buffer[0], 2.0f, 1e-6f);
    EXPECT_NEAR(buffer[1], 0.5f, 1e-6f);
    EXPECT_NEAR(buffer[2], 2.0f, 1e-6f);
    EXPECT_NEAR(buffer[3], 0.5f, 1e-6f);
}

TEST(PluginAudioGraphTest, RemovingNodeRestoresPassthrough) {
    AudioGraph graph;
    const QString nodeId = graph.addNode(std::make_shared<TestGainNode>(0.5f));
    ASSERT_TRUE(graph.connectExternalInput(0, nodeId, 0));
    ASSERT_TRUE(graph.connectToExternalOutput(nodeId, 0, 0));
    graph.updateRenderParameters(48000.0, 1024);
    ASSERT_TRUE(graph.removeNode(nodeId));

    std::vector<float> buffer(4, 1.0f);
    graph.processInPlace(buffer.data(), static_cast<int>(buffer.size()));
    for (float value : buffer) {
        EXPECT_FLOAT_EQ(value, 1.0f);
    }
}

TEST(PluginNativeNodeTest, DescribesAndAppliesParameters) {
    NativePluginNode node(&kTestVtable);
    ASSERT_EQ(node.numInputs(), 2u);
    ASSERT_EQ(node.numOutputs(), 2u);
    ASSERT_EQ(node.numParams(), 1u);

    mixxx_param_info info{};
    ASSERT_TRUE(node.describeParam(0, &info));
    EXPECT_EQ(info.type, static_cast<uint32_t>(MIXXX_PARAM_FLOAT));
    EXPECT_FLOAT_EQ(info.default_value, 1.0f);

    ASSERT_TRUE(node.prepare(48000.0, 1024));
    ASSERT_TRUE(node.pushParam(makeFloatEvent(0, 0.25f)));

    const float inL[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float inR[4] = {2.0f, 2.0f, 2.0f, 2.0f};
    const float* inputs[2] = {inL, inR};
    float outL[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float outR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float* outputs[2] = {outL, outR};

    node.process(inputs, outputs, 4, nullptr, 0);
    EXPECT_NEAR(outL[0], 0.25f, 1e-6f);
    EXPECT_NEAR(outR[0], 0.5f, 1e-6f);
    node.release();
}

TEST(PluginNativeNodeTest, RejectsUnknownParameter) {
    NativePluginNode node(&kTestVtable);
    mixxx_param_info info{};
    EXPECT_FALSE(node.describeParam(1, &info)); // the node exposes one parameter
    EXPECT_FALSE(node.describeParam(0, nullptr));
    EXPECT_FALSE(node.describeParam(1, nullptr));
}

TEST(PluginAudioGraphTest, ChainsNodesInOrder) {
    AudioGraph graph;
    const QString firstId = graph.addNode(std::make_shared<TestGainNode>(0.5f));
    const QString secondId = graph.addNode(std::make_shared<TestGainNode>(0.5f));
    ASSERT_FALSE(firstId.isEmpty());
    ASSERT_FALSE(secondId.isEmpty());
    ASSERT_TRUE(graph.connect(firstId, 0, secondId, 0));
    ASSERT_TRUE(graph.connect(firstId, 1, secondId, 1));
    ASSERT_TRUE(graph.connectExternalInput(0, firstId, 0));
    ASSERT_TRUE(graph.connectExternalInput(1, firstId, 1));
    ASSERT_TRUE(graph.connectToExternalOutput(secondId, 0, 0));
    ASSERT_TRUE(graph.connectToExternalOutput(secondId, 1, 1));
    graph.updateRenderParameters(48000.0, 1024);

    std::vector<float> buffer(4, 1.0f);
    graph.processInPlace(buffer.data(), static_cast<int>(buffer.size()));
    for (float value : buffer) {
        EXPECT_NEAR(value, 0.25f, 1e-6f);
    }
}

TEST(PluginAudioGraphTest, RejectsInvalidWiring) {
    AudioGraph graph;
    const QString nodeId = graph.addNode(std::make_shared<TestGainNode>(1.0f));
    ASSERT_FALSE(nodeId.isEmpty());

    EXPECT_FALSE(graph.connect(QStringLiteral("missing"), 0, nodeId, 0));
    EXPECT_FALSE(graph.connect(nodeId, 0, nodeId, 0)); // self-connection
    EXPECT_FALSE(graph.connectExternalInput(2, nodeId, 0)); // only two inputs
    EXPECT_FALSE(graph.connectExternalInput(0, nodeId, 2)); // node has two inputs
    EXPECT_FALSE(graph.connectToExternalOutput(nodeId, 0, 2)); // two outputs
    EXPECT_FALSE(graph.connectToExternalOutput(QStringLiteral("missing"), 0, 0));
    EXPECT_FALSE(graph.disconnect(nodeId, 5));
    EXPECT_FALSE(graph.removeNode(QStringLiteral("missing")));
}

TEST(PluginAudioGraphTest, ProcessInPlaceIgnoresEmptyAndOddBuffers) {
    AudioGraph graph;
    const QString nodeId = graph.addNode(std::make_shared<TestGainNode>(0.5f));
    ASSERT_FALSE(nodeId.isEmpty());
    ASSERT_TRUE(graph.connectExternalInput(0, nodeId, 0));
    ASSERT_TRUE(graph.connectToExternalOutput(nodeId, 0, 0));
    graph.updateRenderParameters(48000.0, 1024);

    std::vector<float> empty;
    graph.processInPlace(empty.data(), 0); // no crash, nothing to process

    std::vector<float> odd{0.7f};
    graph.processInPlace(odd.data(), 1); // less than one stereo frame: untouched
    EXPECT_FLOAT_EQ(odd[0], 0.7f);
}

TEST(PluginAudioGraphTest, ScopeRefCountsClients) {
    AudioGraph graph;
    EXPECT_FALSE(graph.audioScopeEnabled());

    graph.acquireAudioScope();
    graph.acquireAudioScope();
    EXPECT_TRUE(graph.audioScopeEnabled());

    graph.releaseAudioScope();
    EXPECT_TRUE(graph.audioScopeEnabled()); // one client remains
    graph.releaseAudioScope();
    EXPECT_FALSE(graph.audioScopeEnabled());

    graph.releaseAudioScope(); // over-release must not go negative
    EXPECT_FALSE(graph.audioScopeEnabled());
}

TEST(PluginAudioGraphTest, ScopeTapCapturesOutputWhileAcquired) {
    AudioGraph graph;
    graph.updateRenderParameters(48000.0, 1024);
    graph.acquireAudioScope();

    std::vector<float> buffer{1.0f, 0.5f, 1.0f, 0.5f};
    graph.processInPlace(buffer.data(), static_cast<int>(buffer.size()));

    float left[2];
    float right[2];
    float mono[2];
    ASSERT_EQ(graph.readAudioScope(2, left, right, mono), 2);
    EXPECT_FLOAT_EQ(left[0], 1.0f);
    EXPECT_FLOAT_EQ(right[0], 0.5f);
    EXPECT_FLOAT_EQ(left[1], 1.0f);
    EXPECT_FLOAT_EQ(right[1], 0.5f);
    graph.releaseAudioScope();
}

} // namespace plugins
} // namespace mixxx
