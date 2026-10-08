#include "plugins/audio/audiograph.h"

#include <QTimer>

#include <algorithm>

#include "moc_audiograph.cpp"
#include "util/assert.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginAudioGraph");

/// Retired snapshots and deferred node releases are reclaimed on this interval
/// when the graph is otherwise idle.
constexpr int kSnapshotReclaimIntervalMs = 2000;

} // anonymous namespace

struct AudioGraphSnapshot {
    enum InputKind {
        InputSilence = 0,
        InputExternal = 1,
        InputNode = 2,
    };

    struct ResolvedInput {
        InputKind kind = InputSilence;
        int nodeIndex = -1;
        uint32_t channel = 0;
        uint32_t externalChannel = 0;
    };

    struct Node {
        std::shared_ptr<PluginNode> pNode;
        uint32_t numInputs = 0;
        uint32_t numOutputs = 0;
        QVector<ResolvedInput> inputs;
        std::vector<std::vector<float>> outputs;
    };

    struct OutBinding {
        int nodeIndex = -1;
        uint32_t channel = 0;
    };

    std::vector<Node> nodes;
    OutBinding outputs[2];
};

AudioGraph::AudioGraph(QObject* pParent)
        : QObject(pParent) {
    m_pReclaimTimer = new QTimer(this);
    m_pReclaimTimer->setInterval(kSnapshotReclaimIntervalMs);
    // AudioGraph defines its own `connect` method for graph edges, so
    // QObject::connect must be qualified here.
    QObject::connect(m_pReclaimTimer,
            &QTimer::timeout,
            this,
            &AudioGraph::reclaimRetiredSnapshots);
    m_pReclaimTimer->start();
}

AudioGraph::~AudioGraph() {
    // Stop processing before tearing nodes down.
    AudioGraphSnapshot* pSnapshot = m_pSnapshot.exchange(nullptr);
    delete pSnapshot;
    for (AudioGraphSnapshot* pRetired : std::as_const(m_retiredSnapshots)) {
        delete pRetired;
    }
    m_retiredSnapshots.clear();
    releaseAllNodes();
    releaseDeferredNodes();
}

void AudioGraph::releaseAllNodes() {
    for (const NodeRecord& record : std::as_const(m_nodes)) {
        record.node->release();
    }
    m_prepared = false;
}

void AudioGraph::releaseDeferredNodes() {
    for (const std::shared_ptr<PluginNode>& pNode : std::as_const(m_deferredRelease)) {
        if (pNode) {
            pNode->release();
        }
    }
    m_deferredRelease.clear();
}

void AudioGraph::cancelDeferredRelease(const std::shared_ptr<PluginNode>& pNode) {
    for (auto it = m_deferredRelease.begin(); it != m_deferredRelease.end();) {
        if (*it == pNode) {
            it = m_deferredRelease.erase(it);
        } else {
            ++it;
        }
    }
}

QString AudioGraph::addNode(std::shared_ptr<PluginNode> pNode) {
    VERIFY_OR_DEBUG_ASSERT(pNode) {
        return QString();
    }
    // A node that was removed and whose release was deferred is active again;
    // cancel the pending release so it is not torn down while in use.
    cancelDeferredRelease(pNode);
    static quint64 s_counter = 0;
    const QString id = QStringLiteral("node-%1").arg(++s_counter);
    m_nodes.insert(id, NodeRecord{id, pNode});
    m_nodeOrder.append(id);
    // Prepare only this node; the other nodes are already prepared.
    if (m_maxFrames > 0) {
        prepareNode(id, *pNode);
    }
    rebuild();
    return id;
}

bool AudioGraph::removeNode(const QString& nodeId) {
    if (!m_nodes.contains(nodeId)) {
        return false;
    }
    // Do not release() yet: a snapshot the real-time thread is still draining
    // may reference this node. Keep it alive and defer the release until every
    // such snapshot has been reclaimed (see reclaimRetiredSnapshots()).
    m_deferredRelease.append(m_nodes.value(nodeId).node);
    m_nodes.remove(nodeId);
    m_nodeOrder.removeAll(nodeId);
    m_inputBindings.remove(nodeId);

    // Remove connections referencing the removed node.
    for (auto it = m_inputBindings.begin(); it != m_inputBindings.end(); ++it) {
        for (InputBinding& binding : it.value()) {
            if (binding.kind == InputBinding::Kind::Node &&
                    binding.sourceNodeId == nodeId) {
                binding = InputBinding();
            }
        }
    }
    for (OutputBinding& output : m_outputs) {
        if (output.sourceNodeId == nodeId) {
            output = OutputBinding();
        }
    }
    m_prepared = false;
    rebuild();
    return true;
}

bool AudioGraph::hasNode(const QString& nodeId) const {
    return m_nodes.contains(nodeId);
}

std::shared_ptr<PluginNode> AudioGraph::node(const QString& nodeId) const {
    const auto it = m_nodes.constFind(nodeId);
    if (it == m_nodes.constEnd()) {
        return nullptr;
    }
    return it->node;
}

QStringList AudioGraph::nodeIds() const {
    QStringList ids;
    for (const QString& id : m_nodeOrder) {
        ids.append(id);
    }
    return ids;
}

bool AudioGraph::connect(const QString& sourceNodeId,
        uint32_t sourceChannel,
        const QString& destNodeId,
        uint32_t destChannel) {
    if (!m_nodes.contains(sourceNodeId) || !m_nodes.contains(destNodeId)) {
        return false;
    }
    if (sourceNodeId == destNodeId) {
        // Self-connections would introduce a trivial cycle.
        return false;
    }
    const uint32_t sourceChannels = m_nodes.value(sourceNodeId).node->numOutputs();
    const uint32_t destChannels = m_nodes.value(destNodeId).node->numInputs();
    if (sourceChannel >= sourceChannels || destChannel >= destChannels) {
        return false;
    }

    InputBinding binding;
    binding.kind = InputBinding::Kind::Node;
    binding.sourceNodeId = sourceNodeId;
    binding.sourceChannel = sourceChannel;
    inputBindingSlot(destNodeId, destChannel) = binding;
    rebuild();
    return true;
}

bool AudioGraph::connectExternalInput(
        uint32_t inputChannel, const QString& destNodeId, uint32_t destChannel) {
    if (!m_nodes.contains(destNodeId) || inputChannel > 1) {
        return false;
    }
    if (destChannel >= m_nodes.value(destNodeId).node->numInputs()) {
        return false;
    }
    InputBinding binding;
    binding.kind = InputBinding::Kind::External;
    binding.externalChannel = inputChannel;
    inputBindingSlot(destNodeId, destChannel) = binding;
    rebuild();
    return true;
}

AudioGraph::InputBinding& AudioGraph::inputBindingSlot(
        const QString& destNodeId, uint32_t destChannel) {
    QVector<InputBinding>& bindings = m_inputBindings[destNodeId];
    if (bindings.size() <= static_cast<int>(destChannel)) {
        bindings.resize(static_cast<int>(destChannel) + 1);
    }
    return bindings[static_cast<int>(destChannel)];
}

bool AudioGraph::connectToExternalOutput(
        const QString& sourceNodeId, uint32_t sourceChannel, uint32_t outputChannel) {
    if (!m_nodes.contains(sourceNodeId) || outputChannel > 1) {
        return false;
    }
    if (sourceChannel >= m_nodes.value(sourceNodeId).node->numOutputs()) {
        return false;
    }
    m_outputs[outputChannel].sourceNodeId = sourceNodeId;
    m_outputs[outputChannel].sourceChannel = sourceChannel;
    rebuild();
    return true;
}

bool AudioGraph::disconnect(const QString& destNodeId, uint32_t destChannel) {
    auto it = m_inputBindings.find(destNodeId);
    if (it == m_inputBindings.end() ||
            static_cast<int>(destChannel) >= it->size()) {
        return false;
    }
    (*it)[static_cast<int>(destChannel)] = InputBinding();
    rebuild();
    return true;
}

void AudioGraph::clearExternalOutputs() {
    m_outputs[0] = OutputBinding();
    m_outputs[1] = OutputBinding();
    rebuild();
}

void AudioGraph::clear() {
    // As in removeNode(), defer release() until no snapshot can reference the
    // nodes any more.
    for (const NodeRecord& record : std::as_const(m_nodes)) {
        m_deferredRelease.append(record.node);
    }
    m_nodes.clear();
    m_nodeOrder.clear();
    m_inputBindings.clear();
    m_outputs[0] = OutputBinding();
    m_outputs[1] = OutputBinding();
    m_prepared = false;
    rebuild();
}

void AudioGraph::updateRenderParameters(double sampleRate, uint32_t maxFrames) {
    const bool parametersChanged =
            sampleRate != m_sampleRate || maxFrames != m_maxFrames;
    m_sampleRate = sampleRate;
    m_maxFrames = maxFrames;
    if (parametersChanged) {
        m_prepared = false;
        m_scopeTap.prepare(sampleRate);
    }
    ensurePrepared();
    rebuild();
}

void AudioGraph::acquireAudioScope() {
    if (m_scopeClients.fetch_add(1) == 0) {
        m_scopeTap.setEnabled(true);
    }
}

void AudioGraph::releaseAudioScope() {
    if (m_scopeClients.fetch_sub(1) <= 1) {
        m_scopeClients.store(0);
        m_scopeTap.setEnabled(false);
    }
}

void AudioGraph::ensurePrepared() {
    if (m_prepared || m_maxFrames == 0) {
        return;
    }

    // Allocate the deinterleave/interleave scratch buffers on the main thread.
    const size_t frames = m_maxFrames;
    for (int channel = 0; channel < 2; ++channel) {
        m_scratchInput[channel].assign(frames, 0.0f);
    }
    m_silence.assign(frames, 0.0f);

    for (const NodeRecord& record : std::as_const(m_nodes)) {
        prepareNode(record.id, *record.node);
    }
    m_prepared = true;
}

void AudioGraph::prepareNode(const QString& nodeId, PluginNode& node) {
    if (!node.prepare(m_sampleRate, m_maxFrames)) {
        kLogger.warning() << "Failed to prepare node" << nodeId;
    }
}

void AudioGraph::rebuild() {
    auto* pSnapshot = new AudioGraphSnapshot();

    // Map node id -> index in insertion order.
    QHash<QString, int> indexById;
    for (int i = 0; i < m_nodeOrder.size(); ++i) {
        indexById.insert(m_nodeOrder.at(i), i);
    }

    // Build adjacency for a topological sort.
    const int nodeCount = m_nodeOrder.size();
    QVector<QVector<int>> adjacency(nodeCount);
    QVector<int> inDegree(nodeCount, 0);
    for (auto it = m_inputBindings.constBegin(); it != m_inputBindings.constEnd(); ++it) {
        const int destIndex = indexById.value(it.key(), -1);
        if (destIndex < 0) {
            continue;
        }
        for (const InputBinding& binding : it.value()) {
            if (binding.kind != InputBinding::Kind::Node) {
                continue;
            }
            const int sourceIndex = indexById.value(binding.sourceNodeId, -1);
            if (sourceIndex < 0) {
                continue;
            }
            adjacency[sourceIndex].append(destIndex);
            ++inDegree[destIndex];
        }
    }

    QVector<int> order;
    order.reserve(nodeCount);
    QVector<int> ready;
    for (int i = 0; i < nodeCount; ++i) {
        if (inDegree[i] == 0) {
            ready.append(i);
        }
    }
    while (!ready.isEmpty()) {
        const int index = ready.takeFirst();
        order.append(index);
        for (int next : std::as_const(adjacency[index])) {
            if (--inDegree[next] == 0) {
                ready.append(next);
            }
        }
    }
    if (order.size() < nodeCount) {
        kLogger.warning() << "Audio graph contains a cycle; keeping insertion order.";
        for (int i = 0; i < nodeCount; ++i) {
            if (!order.contains(i)) {
                order.append(i);
            }
        }
    }

    const uint32_t bufferFrames = std::max<uint32_t>(m_maxFrames, 1);
    pSnapshot->nodes.resize(nodeCount);
    QHash<int, int> snapshotIndexByOriginalIndex;
    for (int position = 0; position < order.size(); ++position) {
        const int originalIndex = order.at(position);
        const QString nodeId = m_nodeOrder.at(originalIndex);
        const NodeRecord& record = m_nodes.value(nodeId);

        AudioGraphSnapshot::Node& node = pSnapshot->nodes[position];
        node.pNode = record.node;
        node.numInputs = std::min<uint32_t>(record.node->numInputs(), kMaxNodeChannels);
        node.numOutputs = std::min<uint32_t>(record.node->numOutputs(), kMaxNodeChannels);
        snapshotIndexByOriginalIndex.insert(originalIndex, position);

        const QVector<InputBinding> bindings = m_inputBindings.value(nodeId);
        node.inputs.resize(static_cast<int>(node.numInputs));
        for (uint32_t channel = 0; channel < node.numInputs; ++channel) {
            AudioGraphSnapshot::ResolvedInput resolved;
            if (static_cast<int>(channel) < bindings.size()) {
                const InputBinding& binding = bindings.at(static_cast<int>(channel));
                switch (binding.kind) {
                case InputBinding::Kind::External:
                    resolved.kind = AudioGraphSnapshot::InputExternal;
                    resolved.externalChannel = binding.externalChannel;
                    break;
                case InputBinding::Kind::Node: {
                    resolved.kind = AudioGraphSnapshot::InputNode;
                    const int originalSourceIndex =
                            indexById.value(binding.sourceNodeId, -1);
                    // Topological order guarantees the source has already been
                    // placed in the snapshot.
                    resolved.nodeIndex = snapshotIndexByOriginalIndex.value(
                            originalSourceIndex, -1);
                    resolved.channel = binding.sourceChannel;
                    break;
                }
                case InputBinding::Kind::Silence:
                default:
                    resolved.kind = AudioGraphSnapshot::InputSilence;
                    break;
                }
            }
            node.inputs[static_cast<int>(channel)] = resolved;
        }

        node.outputs.assign(node.numOutputs, std::vector<float>(bufferFrames, 0.0f));
    }

    for (int channel = 0; channel < 2; ++channel) {
        const OutputBinding& output = m_outputs[channel];
        if (!output.sourceNodeId.isEmpty()) {
            const int originalIndex = indexById.value(output.sourceNodeId, -1);
            const int snapshotIndex = snapshotIndexByOriginalIndex.value(originalIndex, -1);
            if (snapshotIndex >= 0) {
                pSnapshot->outputs[channel].nodeIndex = snapshotIndex;
                pSnapshot->outputs[channel].channel = output.sourceChannel;
                continue;
            }
        }
        pSnapshot->outputs[channel].nodeIndex = -1;
    }

    AudioGraphSnapshot* pOld = m_pSnapshot.exchange(pSnapshot, std::memory_order_seq_cst);
    if (pOld != nullptr) {
        // The reader publishes m_pLastUsed *before* it dereferences a snapshot
        // and re-validates m_pSnapshot afterwards. Pairing the exchange with a
        // seq_cst load of m_pLastUsed here makes the decision to free pOld
        // safe against that protocol.
        if (m_pLastUsed.load(std::memory_order_seq_cst) != pOld) {
            delete pOld;
        } else {
            m_retiredSnapshots.append(pOld);
        }
    }
    reclaimRetiredSnapshots();
    emit graphChanged();
}

void AudioGraph::reclaimRetiredSnapshots() {
    AudioGraphSnapshot* pLastUsed = m_pLastUsed.load(std::memory_order_acquire);
    for (int i = m_retiredSnapshots.size() - 1; i >= 0; --i) {
        AudioGraphSnapshot* pRetired = m_retiredSnapshots.at(i);
        if (pRetired != pLastUsed) {
            delete pRetired;
            m_retiredSnapshots.removeAt(i);
        }
    }
    // Once no retired snapshot remains, the only snapshot left is the current
    // one (m_pSnapshot), which no longer references any deferred node, so the
    // audio thread cannot be processing one.
    if (m_retiredSnapshots.isEmpty()) {
        releaseDeferredNodes();
    }
}

void AudioGraph::processInPlace(CSAMPLE* pBuffer, int numSamples) {
    if (numSamples <= 0) {
        return;
    }

    // Tap the master output before any plugin processing. This is the audio the
    // audience hears (post main effects) and is what visualizers should show.
    if (m_scopeTap.isEnabled()) {
        m_scopeTap.write(pBuffer, numSamples);
    }

    if (m_maxFrames == 0) {
        return;
    }

    // Publish the snapshot we are about to use as a hazard pointer *before*
    // dereferencing it, then re-validate that the main thread did not swap in a
    // new snapshot meanwhile. rebuild() frees the old snapshot as soon as it
    // observes m_pLastUsed != old, so without this re-validation the audio
    // thread could keep processing a snapshot that was just deleted. The retry
    // loop is lock-, wait- and allocation-free.
    AudioGraphSnapshot* pSnapshot = nullptr;
    do {
        pSnapshot = m_pSnapshot.load(std::memory_order_acquire);
        m_pLastUsed.store(pSnapshot, std::memory_order_seq_cst);
    } while (m_pSnapshot.load(std::memory_order_seq_cst) != pSnapshot);

    if (pSnapshot == nullptr || pSnapshot->nodes.empty()) {
        return;
    }

    const uint32_t frames = static_cast<uint32_t>(numSamples / 2);
    if (frames > m_maxFrames) {
        return;
    }
    const float* const pExternalInput[2] = {
            m_scratchInput[0].data(),
            m_scratchInput[1].data()};

    for (uint32_t frame = 0; frame < frames; ++frame) {
        m_scratchInput[0][frame] = pBuffer[2 * frame];
        m_scratchInput[1][frame] = pBuffer[2 * frame + 1];
    }

    for (AudioGraphSnapshot::Node& node : pSnapshot->nodes) {
        const float* pInputs[kMaxNodeChannels] = {nullptr};
        float* pOutputs[kMaxNodeChannels] = {nullptr};

        for (uint32_t channel = 0; channel < node.numInputs; ++channel) {
            const AudioGraphSnapshot::ResolvedInput& resolved =
                    node.inputs.at(static_cast<int>(channel));
            switch (resolved.kind) {
            case AudioGraphSnapshot::InputExternal:
                pInputs[channel] = resolved.externalChannel < 2
                        ? pExternalInput[resolved.externalChannel]
                        : m_silence.data();
                break;
            case AudioGraphSnapshot::InputNode:
                if (resolved.nodeIndex >= 0 &&
                        resolved.nodeIndex < static_cast<int>(pSnapshot->nodes.size()) &&
                        resolved.channel <
                                pSnapshot->nodes[resolved.nodeIndex].outputs.size()) {
                    pInputs[channel] =
                            pSnapshot->nodes[resolved.nodeIndex]
                                    .outputs[resolved.channel]
                                    .data();
                } else {
                    pInputs[channel] = m_silence.data();
                }
                break;
            case AudioGraphSnapshot::InputSilence:
            default:
                pInputs[channel] = m_silence.data();
                break;
            }
        }
        for (uint32_t channel = 0; channel < node.numOutputs; ++channel) {
            pOutputs[channel] = node.outputs[channel].data();
        }
        node.pNode->process(pInputs, pOutputs, frames, nullptr, 0);
    }

    const float* pExternalOutput[2];
    for (int channel = 0; channel < 2; ++channel) {
        const AudioGraphSnapshot::OutBinding& output = pSnapshot->outputs[channel];
        if (output.nodeIndex >= 0 &&
                output.nodeIndex < static_cast<int>(pSnapshot->nodes.size()) &&
                output.channel <
                        pSnapshot->nodes[output.nodeIndex].outputs.size()) {
            pExternalOutput[channel] =
                    pSnapshot->nodes[output.nodeIndex].outputs[output.channel].data();
        } else {
            pExternalOutput[channel] = pExternalInput[channel];
        }
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        pBuffer[2 * frame] = pExternalOutput[0][frame];
        pBuffer[2 * frame + 1] = pExternalOutput[1][frame];
    }
}

} // namespace plugins
} // namespace mixxx
