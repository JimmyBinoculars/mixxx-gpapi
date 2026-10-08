#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>
#include <vector>

#include "plugins/audio/audioscopetap.h"
#include "plugins/audio/pluginnode.h"
#include "util/types.h"

class QTimer;

namespace mixxx {
namespace plugins {

struct AudioGraphSnapshot;

/// Immutable audio processing graph with lock-free snapshot swapping.
///
/// The graph is edited on the main thread. Each edit builds a brand new
/// `AudioGraphSnapshot` (nodes, resolved input bindings, per-node output
/// buffers and a topologically sorted processing order) and atomically
/// publishes it. The real-time callback only ever reads the current snapshot,
/// so it never allocates, locks or blocks.
///
/// The graph owns two external stereo ports:
///  * input 0/1 is sourced from the caller-provided buffer,
///  * output 0/1 is fed back to the caller-provided buffer, pass-through by
///    default.
///
/// Nodes are connected either from the external input (`connectExternalInput`),
/// from another node's output (`connect`) or left unconnected (silence).
class AudioGraph : public QObject {
    Q_OBJECT
  public:
    explicit AudioGraph(QObject* pParent = nullptr);
    ~AudioGraph() override;

    // --- Main-thread graph editing ---

    /// Adds a node and returns its unique id.
    QString addNode(std::shared_ptr<PluginNode> pNode);
    bool removeNode(const QString& nodeId);
    bool hasNode(const QString& nodeId) const;
    std::shared_ptr<PluginNode> node(const QString& nodeId) const;
    QStringList nodeIds() const;
    int nodeCount() const {
        return m_nodeOrder.size();
    }

    /// Connects `sourceNodeId`:`sourceChannel` to `destNodeId`:`destChannel`.
    bool connect(const QString& sourceNodeId,
            uint32_t sourceChannel,
            const QString& destNodeId,
            uint32_t destChannel);

    /// Connects the external stereo input channel to a node input.
    bool connectExternalInput(
            uint32_t inputChannel, const QString& destNodeId, uint32_t destChannel);

    /// Routes a node output to an external stereo output channel (0 or 1).
    bool connectToExternalOutput(
            const QString& sourceNodeId, uint32_t sourceChannel, uint32_t outputChannel);

    /// Removes any binding for the given node input (reverts to silence).
    bool disconnect(const QString& destNodeId, uint32_t destChannel);

    /// Clears all external output bindings (pass-through).
    void clearExternalOutputs();

    /// Removes all nodes and connections.
    void clear();

    /// Called from the main thread when engine parameters change or after
    /// graph edits. Prepares nodes if needed and publishes a new snapshot.
    void updateRenderParameters(double sampleRate, uint32_t maxFrames);

    /// Real-time entry point. `pBuffer` is a stereo interleaved buffer of
    /// `numSamples` (i.e. `numSamples / 2` frames).
    void processInPlace(CSAMPLE* pBuffer, int numSamples);

    // --- Master output tap (for visualization plugins) ---

    /// Registers a consumer of the master output scope. The tap is enabled as
    /// long as at least one client holds it. Call from the main thread.
    void acquireAudioScope();
    /// Releases a previously acquired audio scope client.
    void releaseAudioScope();
    bool audioScopeEnabled() const {
        return m_scopeClients.load() > 0;
    }

    /// Reads the most recent `frames` frames of the master output. See
    /// AudioScopeTap::readLatest(). Call from the main/GUI thread.
    int readAudioScope(int frames,
            float* outLeft,
            float* outRight,
            float* outMono) const {
        return m_scopeTap.readLatest(frames, outLeft, outRight, outMono);
    }

    bool isEmpty() const {
        return m_nodes.isEmpty();
    }

    double sampleRate() const {
        return m_sampleRate;
    }

  signals:
    void graphChanged();

  private:
    void ensurePrepared();
    void rebuild();
    void reclaimRetiredSnapshots();
    void releaseAllNodes();
    void releaseDeferredNodes();
    void cancelDeferredRelease(const std::shared_ptr<PluginNode>& pNode);
    void prepareNode(const QString& nodeId, PluginNode& node);

    struct NodeRecord {
        QString id;
        std::shared_ptr<PluginNode> node;
    };

    struct InputBinding {
        enum class Kind {
            Silence,
            External,
            Node,
        };
        Kind kind = Kind::Silence;
        QString sourceNodeId;
        uint32_t sourceChannel = 0;
        uint32_t externalChannel = 0;
    };

    InputBinding& inputBindingSlot(const QString& destNodeId, uint32_t destChannel);

    struct OutputBinding {
        QString sourceNodeId;
        uint32_t sourceChannel = 0;
    };

    QHash<QString, NodeRecord> m_nodes;
    QVector<QString> m_nodeOrder;
    /// destNodeId -> per-input binding
    QHash<QString, QVector<InputBinding>> m_inputBindings;
    OutputBinding m_outputs[2];

    double m_sampleRate = 0.0;
    uint32_t m_maxFrames = 0;
    bool m_prepared = false;

    AudioScopeTap m_scopeTap;
    std::atomic<int> m_scopeClients{0};

    std::atomic<AudioGraphSnapshot*> m_pSnapshot{nullptr};
    std::atomic<AudioGraphSnapshot*> m_pLastUsed{nullptr};
    QVector<AudioGraphSnapshot*> m_retiredSnapshots;
    /// Nodes removed from the graph whose `release()` is deferred until every
    /// snapshot that could still reference them has been reclaimed. Keeping the
    /// shared_ptr alive prevents the node from being destroyed (and thus
    /// released) while the real-time thread may still be inside `process()`.
    QVector<std::shared_ptr<PluginNode>> m_deferredRelease;
    QTimer* m_pReclaimTimer;

    /// Deinterleaved scratch buffers, allocated on the main thread.
    std::vector<float> m_scratchInput[2];
    std::vector<float> m_silence;
};

} // namespace plugins
} // namespace mixxx
