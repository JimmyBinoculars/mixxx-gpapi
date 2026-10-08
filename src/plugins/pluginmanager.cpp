#include "plugins/pluginmanager.h"

#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QJSEngine>
#include <QLabel>
#include <QLibrary>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QPoint>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QSize>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

#ifdef MIXXX_USE_QML
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#endif

#include "plugins/ui/pluginwebview.h"

#include "library/library.h"
#include "moc_pluginmanager.cpp"
#include "plugins/audio/audiograph.h"
#include "plugins/audio/pluginnode.h"
#include "plugins/library/pluginremotetrackmodel.h"
#include "plugins/pluginconsentdialog.h"
#include "plugins/pluginstore.h"
#include "plugins/script/pluginjsproxies.h"
#include "plugins/script/pluginscriptengine.h"
#include "plugins/ui/pluginmenuregistry.h"
#include "util/dnd.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginManager");

QString pluginDisplayName(const PluginInfo& info) {
    if (!info.manifest.name.isEmpty()) {
        return info.manifest.name;
    }
    return info.manifest.id;
}

/// A native plugin library that was loaded successfully, together with its
/// versioned vtable. The caller owns `pLibrary`.
struct NativeLibrary {
    QLibrary* pLibrary = nullptr;
    const mixxx_plugin_v1* pVtable = nullptr;
};

/// Loads `libraryPath` and resolves its plugin vtable. On failure returns
/// std::nullopt, having released the library, and stores a reason in `pError`.
std::optional<NativeLibrary> loadNativeLibrary(
        const QString& libraryPath, QString* pError) {
    const auto fail = [pError](const QString& reason) {
        if (pError != nullptr) {
            *pError = reason;
        }
        return std::nullopt;
    };

    auto* pLibrary = new QLibrary(libraryPath);
    pLibrary->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!pLibrary->load()) {
        const QString error = pLibrary->errorString();
        delete pLibrary;
        return fail(error);
    }
    auto entry = reinterpret_cast<mixxx_plugin_entry_fn>(
            pLibrary->resolve(MIXXX_PLUGIN_ENTRY_NAME));
    if (entry == nullptr) {
        pLibrary->unload();
        delete pLibrary;
        return fail(PluginManager::tr("Missing %1 symbol.").arg(MIXXX_PLUGIN_ENTRY_NAME));
    }
    const mixxx_plugin_v1* pVtable = entry(MIXXX_PLUGIN_ABI_VERSION);
    if (pVtable == nullptr || pVtable->abi_version > MIXXX_PLUGIN_ABI_VERSION) {
        pLibrary->unload();
        delete pLibrary;
        return fail(PluginManager::tr("Incompatible plugin ABI."));
    }
    return NativeLibrary{pLibrary, pVtable};
}

/// A modeless, standalone top-level window used for "dialog" panels.
///
/// It is intentionally created without a parent (and as a plain Qt::Window
/// rather than a transient dialog) so the window manager treats it as a normal
/// window: it appears in the taskbar and Alt+Tab switcher, can go behind other
/// windows, and can be moved between monitors.
///
/// When `aspectRatio` > 0 the window keeps that width/height ratio while it is
/// resized (suspended while fullscreen or maximized).
class PluginPanelWindow : public QWidget {
  public:
    explicit PluginPanelWindow(double aspectRatio)
            : QWidget(nullptr, Qt::Window),
              m_aspectRatio(aspectRatio) {
        setProperty("mixxxPanelWindow", true);
    }

  protected:
    void resizeEvent(QResizeEvent* pEvent) override {
        QWidget::resizeEvent(pEvent);
        if (m_aspectRatio <= 0.0 || isFullScreen() || isMaximized() ||
                property("mixxxEmulatedFullscreen").toBool() || m_adjusting) {
            return;
        }
        const QSize previous = pEvent->oldSize();
        QSize next = pEvent->size();
        if (next.width() <= 0 || next.height() <= 0) {
            return;
        }
        const bool widthChanged = previous.width() != next.width();
        const bool heightChanged = previous.height() != next.height();
        if (heightChanged && !widthChanged) {
            next.setWidth(static_cast<int>(std::lround(
                    next.height() * m_aspectRatio)));
        } else {
            next.setHeight(static_cast<int>(std::lround(
                    next.width() / m_aspectRatio)));
        }
        if (next == pEvent->size()) {
            return;
        }
        m_adjusting = true;
        resize(next);
        m_adjusting = false;
    }

  private:
    double m_aspectRatio;
    bool m_adjusting = false;
};

} // anonymous namespace

/// Concrete, fully defined plugin instance. Kept out of the header so that
/// PluginScriptEngine/QLibrary do not leak into every includer.
struct PluginInstance {
    PluginScriptEngine* pScriptEngine = nullptr;
    QLibrary* pLibrary = nullptr;
    std::shared_ptr<PluginNode> pNativeNode;
    QString graphNodeId;
};

PluginManager::PluginManager(PluginHost host, QObject* pParent)
        : QObject(pParent),
          m_host(std::move(host)) {
    m_pPermissions = std::make_unique<PluginPermissionStore>(m_host.pConfig);
    m_pNetworkManager = new QNetworkAccessManager(this);
    m_pStore = new PluginStore(m_pNetworkManager, m_host.pConfig, this);
    m_pRemoteSources = new RemoteSourceManager(this);
    connect(m_pRemoteSources,
            &RemoteSourceManager::sourcesChanged,
            this,
            &PluginManager::pluginsChanged);
    if (m_host.pLibrary) {
        m_host.pLibrary->setPluginRemoteSourceManager(m_pRemoteSources);
    }
    // Allow remote tracks (which have no local file yet) to be dropped onto
    // decks; the actual download is triggered from Library::slotLoadLocationToPlayer.
    DragAndDropHelper::setRemoteLocationDetector(
            [](const QString& location) {
                return PluginRemoteTrackModel::isRemoteLocation(location);
            });
}

PluginManager::~PluginManager() {
    const QStringList ids = m_instances.keys();
    for (const QString& id : ids) {
        unloadPlugin(id);
    }

    // Nodes and panels created by the developer REPL are not owned by any
    // instance, so clean them up here (still while the audio graph is alive).
    const QStringList graphNodes = m_graphNodes.keys();
    for (const QString& nodeId : graphNodes) {
        removeGraphNode(nodeId);
    }
    const QStringList panelIds = m_panelPluginIds.keys();
    for (const QString& panelId : panelIds) {
        removePanel(panelId);
    }
    if (m_host.pMenuRegistry != nullptr) {
        m_host.pMenuRegistry->removePluginItems(QStringLiteral("repl"));
    }

    // Delete the REPL API before its engine and before the child objects
    // (notably m_pRemoteSources) that its destructor reaches through the
    // manager; QObject child teardown order would otherwise free them first.
    delete m_pReplApi;
    m_pReplApi = nullptr;
    if (m_pReplEngine != nullptr) {
        delete m_pReplEngine;
        m_pReplEngine = nullptr;
    }
}

QString PluginManager::userPluginPath() const {
    return QDir(m_host.pConfig->getSettingsPath()).filePath(QStringLiteral("plugins"));
}

QString PluginManager::bundledPluginPath() const {
    return QDir(m_host.pConfig->getResourcePath()).filePath(QStringLiteral("plugins"));
}

void PluginManager::scanDirectory(const QString& root, QList<PluginInfo>* pOut) const {
    QDir rootDir(root);
    if (!rootDir.exists()) {
        return;
    }
    const QStringList entries = rootDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& entry : entries) {
        const QString directory = rootDir.absoluteFilePath(entry);
        const QString manifestPath = QDir(directory).filePath(QStringLiteral("plugin.json"));
        if (!QFileInfo::exists(manifestPath)) {
            continue;
        }

        PluginInfo info;
        info.directory = directory;

        QString parseError;
        const auto manifest = PluginManifestReader::read(manifestPath, &parseError);
        if (!manifest) {
            info.valid = false;
            info.error = parseError;
            // Keep a minimal manifest so the UI can show the directory.
            info.manifest.name = entry;
            pOut->append(info);
            continue;
        }

        info.manifest = *manifest;
        info.valid = true;

        QString supportError;
        info.supported = PluginManifestReader::isSupportedByThisBuild(
                *manifest, &supportError);
        if (!info.supported) {
            info.error = supportError;
        }
        if (!manifest->unknownCapabilities.isEmpty()) {
            const QString unknownError =
                    tr("Unknown capabilities: %1").arg(manifest->unknownCapabilities.join(", "));
            info.error = info.error.isEmpty() ? unknownError : info.error + "\n" + unknownError;
        }
        pOut->append(info);
    }
}

void PluginManager::discover() {
    QList<PluginInfo> discovered;
    scanDirectory(userPluginPath(), &discovered);

    QSet<QString> seenIds;
    for (const PluginInfo& info : std::as_const(discovered)) {
        if (info.valid) {
            seenIds.insert(info.manifest.id);
        }
    }
    // Bundled plugins are only added if the user has not installed a plugin
    // with the same id.
    QList<PluginInfo> bundled;
    scanDirectory(bundledPluginPath(), &bundled);
    for (const PluginInfo& info : std::as_const(bundled)) {
        if (info.valid && seenIds.contains(info.manifest.id)) {
            continue;
        }
        discovered.append(info);
    }

    for (PluginInfo& info : discovered) {
        if (info.valid) {
            info.enabled = m_pPermissions->isEnabled(info.manifest.id);
            info.loaded = m_instances.contains(info.manifest.id);
        }
    }

    std::sort(discovered.begin(), discovered.end(),
            [](const PluginInfo& a, const PluginInfo& b) {
                return pluginDisplayName(a).compare(
                               pluginDisplayName(b), Qt::CaseInsensitive) < 0;
            });

    m_plugins = discovered;
    emit pluginsChanged();
}

void PluginManager::loadAll() {
    discover();
    const QList<PluginInfo> infos = m_plugins;
    for (const PluginInfo& info : infos) {
        if (!info.valid) {
            kLogger.debug() << "Skipping invalid plugin" << info.directory
                            << info.error;
            continue;
        }
        if (!info.supported) {
            kLogger.debug() << "Skipping unsupported plugin" << info.manifest.id
                            << info.error;
            continue;
        }
        if (!info.enabled) {
            kLogger.debug() << "Skipping disabled plugin" << info.manifest.id;
            continue;
        }
        loadPlugin(info.manifest.id);
    }
}

PluginInfo* PluginManager::findPlugin(const QString& pluginId) {
    for (PluginInfo& info : m_plugins) {
        if (info.manifest.id == pluginId) {
            return &info;
        }
    }
    return nullptr;
}

const PluginInfo* PluginManager::findPlugin(const QString& pluginId) const {
    for (const PluginInfo& info : m_plugins) {
        if (info.manifest.id == pluginId) {
            return &info;
        }
    }
    return nullptr;
}

PluginInfo PluginManager::pluginInfo(const QString& pluginId) const {
    const PluginInfo* pInfo = findPlugin(pluginId);
    return pInfo != nullptr ? *pInfo : PluginInfo();
}

bool PluginManager::loadScriptPlugin(PluginInstance& instance, const PluginInfo& info) {
    // Script plugins are capability-gated. Consent is requested on the first
    // load and whenever the manifest requests a capability the user has never
    // approved. Reducing permissions, or the manifest dropping a capability,
    // never re-prompts (and never disables the plugin).
    const bool consentShownBefore = m_pPermissions->isConsentShown(info.manifest.id);
    const PluginCapabilitySet consented =
            m_pPermissions->consentedCapabilities(info.manifest.id);
    const bool newCapabilitiesRequested =
            (info.manifest.requestedCapabilities & ~consented) != PluginCapabilitySet();
    if (!consentShownBefore || newCapabilitiesRequested) {
        PluginCapabilitySet granted = m_pPermissions->grantedCapabilities(info.manifest.id);
        if (!PluginConsentDialog::askConsent(
                    info.manifest, &granted, m_pMainWindow)) {
            m_pPermissions->setConsentShown(info.manifest.id, true);
            // Only the very first declined consent disables the plugin. A
            // declined *capability change* must not silently disable a plugin
            // the user already enabled; it just does not load this time.
            if (!consentShownBefore) {
                m_pPermissions->setEnabled(info.manifest.id, false);
            }
            kLogger.info() << "User declined permissions for plugin"
                            << info.manifest.id;
            return false;
        }
        m_pPermissions->setGrantedCapabilities(info.manifest.id, granted);
        m_pPermissions->setConsentShown(info.manifest.id, true);
    }

    auto* pEngine = new PluginScriptEngine(info.manifest, info.directory, this, this);
    if (!pEngine->initialize()) {
        delete pEngine;
        return false;
    }
    instance.pScriptEngine = pEngine;
    return true;
}

bool PluginManager::loadNativePlugin(PluginInstance& instance, const PluginInfo& info) {
    const QString libraryPath = QDir(info.directory).filePath(info.manifest.nativeEntry);
    QString loadError;
    const auto library = loadNativeLibrary(libraryPath, &loadError);
    if (!library) {
        kLogger.warning() << "Failed to load native plugin" << libraryPath
                           << loadError;
        return false;
    }

    instance.pLibrary = library->pLibrary;
    instance.pNativeNode = std::make_shared<NativePluginNode>(library->pVtable);
    if (m_host.pAudioGraph != nullptr) {
        instance.graphNodeId = m_host.pAudioGraph->addNode(instance.pNativeNode);
        if (!instance.graphNodeId.isEmpty()) {
            m_nativeChain.append(instance.graphNodeId);
            rebuildNativeChain();
        }
    }
    // Native plugins that describe their parameters get a generated control
    // panel (no QML required).
    addNativeParamPanel(info.manifest.id, instance.pNativeNode);
    return true;
}

bool PluginManager::loadPlugin(const QString& pluginId) {
    PluginInfo* pInfo = findPlugin(pluginId);
    if (pInfo == nullptr) {
        return false;
    }
    if (m_instances.contains(pluginId)) {
        return true;
    }
    if (!pInfo->valid) {
        emit pluginLoadFailed(pluginId, pInfo->error);
        return false;
    }
    if (!pInfo->supported) {
        emit pluginLoadFailed(pluginId, pInfo->error);
        return false;
    }

    auto* instance = new PluginInstance();

    bool ok = false;
    if (pInfo->manifest.runtime == PluginManifest::Runtime::Native) {
        ok = loadNativePlugin(*instance, *pInfo);
    } else {
        ok = loadScriptPlugin(*instance, *pInfo);
    }

    if (!ok) {
        delete instance;
        emit pluginLoadFailed(pluginId, tr("Failed to load plugin."));
        return false;
    }

    m_instances.insert(pluginId, instance);
    pInfo->loaded = true;
    kLogger.info() << "Loaded plugin" << pluginId;
    emit pluginLoaded(pluginId);
    emit pluginsChanged();
    return true;
}

void PluginManager::unloadPlugin(const QString& pluginId) {
    const auto it = m_instances.find(pluginId);
    if (it == m_instances.end()) {
        return;
    }
    PluginInstance* instance = it.value();
    m_instances.erase(it);

    if (m_host.pMenuRegistry != nullptr) {
        m_host.pMenuRegistry->removePluginItems(pluginId);
    }
    removePluginPanels(pluginId);
    if (m_pRemoteSources != nullptr) {
        m_pRemoteSources->removePluginSources(pluginId);
    }

    if (instance->pScriptEngine != nullptr) {
        instance->pScriptEngine->shutdown();
        delete instance->pScriptEngine;
        instance->pScriptEngine = nullptr;
    }

    if (m_host.pAudioGraph != nullptr && !instance->graphNodeId.isEmpty()) {
        m_host.pAudioGraph->removeNode(instance->graphNodeId);
        m_nativeChain.removeAll(instance->graphNodeId);
        rebuildNativeChain();
    }
    instance->pNativeNode.reset();

    // Remove any nodes the plugin added itself through mixxx.audio; otherwise
    // they would keep processing audio after the plugin is gone.
    QStringList ownedNodes;
    for (auto nodeIt = m_graphNodes.constBegin(); nodeIt != m_graphNodes.constEnd(); ++nodeIt) {
        if (nodeIt.value().pluginId == pluginId) {
            ownedNodes.append(nodeIt.key());
        }
    }
    for (const QString& nodeId : std::as_const(ownedNodes)) {
        removeGraphNode(nodeId);
    }

    if (instance->pLibrary != nullptr) {
        // Do NOT call unload(): the audio graph may still hold the node (and
        // therefore its vtable) in a retired snapshot that the real-time thread
        // has not finished with. QLibrary keeps libraries mapped until process
        // exit unless unload() is called, so dropping the wrapper here keeps
        // the plugin code alive for as long as any snapshot can reach it.
        delete instance->pLibrary;
        instance->pLibrary = nullptr;
    }

    delete instance;

    PluginInfo* pInfo = findPlugin(pluginId);
    if (pInfo != nullptr) {
        pInfo->loaded = false;
    }
    kLogger.info() << "Unloaded plugin" << pluginId;
    emit pluginUnloaded(pluginId);
    emit pluginsChanged();
}

void PluginManager::setPluginEnabled(const QString& pluginId, bool enabled) {
    m_pPermissions->setEnabled(pluginId, enabled);
    PluginInfo* pInfo = findPlugin(pluginId);
    if (pInfo != nullptr) {
        pInfo->enabled = enabled;
    }
    if (enabled) {
        if (pInfo != nullptr && pInfo->valid && pInfo->supported) {
            loadPlugin(pluginId);
        }
    } else {
        unloadPlugin(pluginId);
    }
    emit pluginsChanged();
}

void PluginManager::reloadPlugin(const QString& pluginId) {
    if (m_instances.contains(pluginId)) {
        unloadPlugin(pluginId);
        loadPlugin(pluginId);
    }
}

bool PluginManager::uninstallPlugin(const QString& pluginId) {
    const PluginInfo* pInfo = findPlugin(pluginId);
    if (pInfo == nullptr) {
        return false;
    }
    const QString directory = pInfo->directory;
    unloadPlugin(pluginId);
    m_pPermissions->removePlugin(pluginId);

    QDir dir(directory);
    if (dir.exists() && !dir.removeRecursively()) {
        kLogger.warning() << "Failed to remove plugin directory" << directory;
        return false;
    }
    discover();
    return true;
}

void PluginManager::rebuildNativeChain() {
    if (m_host.pAudioGraph == nullptr) {
        return;
    }
    AudioGraph* pGraph = m_host.pAudioGraph;
    pGraph->clearExternalOutputs();

    QString previousNodeId;
    for (const QString& nodeId : std::as_const(m_nativeChain)) {
        const std::shared_ptr<PluginNode> pNode = pGraph->node(nodeId);
        if (!pNode) {
            continue;
        }
        const uint32_t inputs = pNode->numInputs();
        if (previousNodeId.isEmpty()) {
            for (uint32_t channel = 0; channel < inputs && channel < 2; ++channel) {
                pGraph->connectExternalInput(channel, nodeId, channel);
            }
        } else {
            const std::shared_ptr<PluginNode> pPrevious = pGraph->node(previousNodeId);
            const uint32_t previousOutputs = pPrevious ? pPrevious->numOutputs() : 0;
            const uint32_t channels = std::min(inputs, previousOutputs);
            for (uint32_t channel = 0; channel < channels; ++channel) {
                pGraph->connect(previousNodeId, channel, nodeId, channel);
            }
        }
        previousNodeId = nodeId;
    }

    if (!previousNodeId.isEmpty()) {
        const std::shared_ptr<PluginNode> pLast = pGraph->node(previousNodeId);
        const uint32_t outputs = pLast ? pLast->numOutputs() : 0;
        for (uint32_t channel = 0; channel < outputs && channel < 2; ++channel) {
            pGraph->connectToExternalOutput(previousNodeId, channel, channel);
        }
    }
}

void PluginManager::setMainWindow(QMainWindow* pMainWindow) {
    m_pMainWindow = pMainWindow;
    if (m_pMainWindow == nullptr) {
        return;
    }
    const QList<PendingPanel> pending = m_pendingPanels;
    m_pendingPanels.clear();
    for (const PendingPanel& panel : pending) {
        materializePanel(panel.spec, panel.panelId);
    }
}

QString PluginManager::addPanel(const PluginPanelSpec& spec) {
    static quint64 s_counter = 0;
    const QString panelId = QStringLiteral("plugin-panel-%1").arg(++s_counter);

    if (m_pMainWindow == nullptr) {
        m_pendingPanels.append(PendingPanel{spec, panelId});
        return panelId;
    }

    materializePanel(spec, panelId);
    return panelId;
}

QWidget* PluginManager::buildPanelContent(
        const PluginPanelSpec& spec, QWidget* pParent, const QString& panelId) {
    QWidget* pContent = nullptr;
    if (!spec.webFile.isEmpty()) {
        QString resolved = spec.webFile;
        const PluginInfo* pInfo = findPlugin(spec.pluginId);
        if (pInfo != nullptr && !QDir::isAbsolutePath(spec.webFile)) {
            resolved = QDir(pInfo->directory).filePath(spec.webFile);
        }
        if (!QFileInfo::exists(resolved)) {
            auto* pLabel = new QLabel(
                    tr("Missing web file: %1").arg(spec.webFile), pParent);
            pLabel->setAlignment(Qt::AlignCenter);
            pLabel->setWordWrap(true);
            pContent = pLabel;
        } else {
            const bool audioScope = spec.audioScope &&
                    m_pPermissions->isGranted(
                            spec.pluginId, PluginCapability::AudioScope);
            QWidget* pView = createWebPanel(m_host.pAudioGraph,
                    resolved,
                    audioScope,
                    [spec](const QVariant& data) {
                        if (spec.onWebMessage) {
                            spec.onWebMessage(data);
                        }
                    },
                    pParent);
            if (pView != nullptr) {
                m_webViews.insert(panelId, pView);
                pContent = pView;
            } else {
                // This build has no Qt WebEngine support.
                auto* pLabel = new QLabel(tr("This plugin needs a build with Qt "
                                             "WebEngine to display '%1'.")
                                                  .arg(spec.webFile),
                        pParent);
                pLabel->setAlignment(Qt::AlignCenter);
                pLabel->setWordWrap(true);
                pContent = pLabel;
            }
        }
    }
#ifdef MIXXX_USE_QML
    if (pContent == nullptr && !spec.qmlFile.isEmpty()) {
        QString resolved = spec.qmlFile;
        const PluginInfo* pInfo = findPlugin(spec.pluginId);
        if (pInfo != nullptr && !QDir::isAbsolutePath(spec.qmlFile)) {
            resolved = QDir(pInfo->directory).filePath(spec.qmlFile);
        }
        if (QFileInfo::exists(resolved)) {
            auto* pQuickWidget = new QQuickWidget(pParent);
            pQuickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
            pQuickWidget->setSource(QUrl::fromLocalFile(resolved));
            pContent = pQuickWidget;
        }
    }
#endif
    if (pContent == nullptr && !spec.controls.isEmpty()) {
        auto* pPanelWidget = new PluginPanelWidget(spec, pParent);
        m_panelWidgets.insert(panelId, pPanelWidget);
        pContent = pPanelWidget;
    }
    if (pContent == nullptr) {
        auto* pLabel = new QLabel(tr("This plugin has no panel content."), pParent);
        pLabel->setAlignment(Qt::AlignCenter);
        pLabel->setWordWrap(true);
        pContent = pLabel;
    }
    return pContent;
}

void PluginManager::materializePanel(
        const PluginPanelSpec& spec, const QString& panelId) {
    if (spec.dialog) {
        // Standalone top-level window: never docks, and (unlike a transient
        // dialog parented to the main window) it appears in the taskbar and the
        // Alt+Tab switcher, can be moved between monitors and can go behind
        // other windows.
        auto* pWindow = new PluginPanelWindow(spec.aspectRatio);
        pWindow->setObjectName(panelId);
        pWindow->setWindowTitle(spec.title.isEmpty() ? spec.pluginId : spec.title);
        pWindow->setAttribute(Qt::WA_DeleteOnClose, false);
        pWindow->setMinimumSize(spec.chrome ? QSize(360, 280) : QSize(160, 120));

        auto* pLayout = new QVBoxLayout(pWindow);
        pLayout->setContentsMargins(spec.chrome ? 8 : 0, spec.chrome ? 8 : 0,
                spec.chrome ? 8 : 0, spec.chrome ? 8 : 0);
        pLayout->addWidget(buildPanelContent(spec, pWindow, panelId), 1);

        if (spec.chrome) {
            auto* pButtons = new QDialogButtonBox(QDialogButtonBox::Close, pWindow);
            connect(pButtons, &QDialogButtonBox::rejected, pWindow, &QWidget::close);
            pLayout->addWidget(pButtons);
        }

        if (spec.aspectRatio > 0.0) {
            const int width = 1280;
            pWindow->resize(width,
                    static_cast<int>(std::lround(width / spec.aspectRatio)));
        } else {
            pWindow->resize(460, 700);
        }
        if (m_pMainWindow != nullptr) {
            const QPoint center = m_pMainWindow->geometry().center();
            pWindow->move(center.x() - pWindow->width() / 2,
                    center.y() - pWindow->height() / 2);
        }

        if (spec.onClose) {
            m_panelCloseCallbacks.insert(pWindow, spec.onClose);
            pWindow->installEventFilter(this);
        }

        m_panels.insert(panelId, pWindow);
        m_panelPluginIds.insert(panelId, spec.pluginId);
        pWindow->show();
        pWindow->raise();
        return;
    }

    auto* pDock = new QDockWidget(
            spec.title.isEmpty() ? spec.pluginId : spec.title, m_pMainWindow);
    pDock->setObjectName(panelId);
    pDock->setWidget(buildPanelContent(spec, pDock, panelId));

    Qt::DockWidgetArea dockArea = Qt::RightDockWidgetArea;
    const QString normalizedArea = spec.area.trimmed().toLower();
    if (normalizedArea == QLatin1String("left")) {
        dockArea = Qt::LeftDockWidgetArea;
    } else if (normalizedArea == QLatin1String("top")) {
        dockArea = Qt::TopDockWidgetArea;
    } else if (normalizedArea == QLatin1String("bottom")) {
        dockArea = Qt::BottomDockWidgetArea;
    }
    m_pMainWindow->addDockWidget(dockArea, pDock);
    pDock->show();

    if (spec.onClose) {
        m_panelCloseCallbacks.insert(pDock, spec.onClose);
        pDock->installEventFilter(this);
    }

    m_panels.insert(panelId, pDock);
    m_panelPluginIds.insert(panelId, spec.pluginId);
}

bool PluginManager::setPanelValue(const QString& panelId,
        const QString& controlId,
        const QVariant& value) {
    PluginPanelWidget* pWidget = m_panelWidgets.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    pWidget->setValue(controlId, value);
    return true;
}

bool PluginManager::setPanelItems(const QString& panelId,
        const QString& controlId,
        const QStringList& items) {
    PluginPanelWidget* pWidget = m_panelWidgets.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    pWidget->setItems(controlId, items);
    return true;
}

bool PluginManager::setPanelListItems(const QString& panelId,
        const QString& controlId,
        const QStringList& texts,
        const QVariantList& values,
        const QStringList& icons) {
    PluginPanelWidget* pWidget = m_panelWidgets.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    pWidget->setListItems(controlId, texts, values, icons);
    return true;
}

bool PluginManager::setPanelListItemIcon(const QString& panelId,
        const QString& controlId,
        int index,
        const QString& iconPath) {
    PluginPanelWidget* pWidget = m_panelWidgets.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    pWidget->setListItemIcon(controlId, index, iconPath);
    return true;
}

bool PluginManager::setPanelEnabled(const QString& panelId,
        const QString& controlId,
        bool enabled) {
    PluginPanelWidget* pWidget = m_panelWidgets.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    pWidget->setControlEnabled(controlId, enabled);
    return true;
}

bool PluginManager::setPanelVisible(const QString& panelId, bool visible) {
    QWidget* pWidget = m_panels.value(panelId).data();
    if (pWidget == nullptr) {
        // Pending panels cannot be shown; the script can fall back to addPanel.
        return false;
    }
    if (visible) {
        pWidget->show();
        pWidget->raise();
        pWidget->activateWindow();
    } else {
        pWidget->hide();
    }
    return true;
}

bool PluginManager::isPanelVisible(const QString& panelId) const {
    QWidget* pWidget = m_panels.value(panelId).data();
    return pWidget != nullptr && pWidget->isVisible();
}

bool PluginManager::isPanelDialog(const QString& panelId) const {
    QWidget* pWidget = m_panels.value(panelId).data();
    return pWidget != nullptr && pWidget->property("mixxxPanelWindow").toBool();
}

bool PluginManager::setPanelGeometry(const QString& panelId,
        int x,
        int y,
        int width,
        int height) {
    QWidget* pWidget = m_panels.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    if (width > 0 && height > 0) {
        pWidget->resize(width, height);
    }
    if (x >= 0 && y >= 0) {
        pWidget->move(x, y);
    }
    return true;
}

QVariantMap PluginManager::panelGeometry(const QString& panelId) const {
    QVariantMap result;
    QWidget* pWidget = m_panels.value(panelId).data();
    if (pWidget == nullptr) {
        return result;
    }
    result.insert(QStringLiteral("x"), pWidget->x());
    result.insert(QStringLiteral("y"), pWidget->y());
    result.insert(QStringLiteral("width"), pWidget->width());
    result.insert(QStringLiteral("height"), pWidget->height());
    return result;
}

bool PluginManager::setPanelFullscreen(const QString& panelId, bool fullscreen) {
    QWidget* pWidget = m_panels.value(panelId).data();
    if (pWidget == nullptr) {
        kLogger.debug() << "setPanelFullscreen: unknown panel" << panelId;
        return false;
    }
    // A panel that is not visible cannot be made fullscreen meaningfully; show
    // it first (it may have been closed by the user).
    if (!pWidget->isVisible()) {
        pWidget->show();
    }

    // QWidget::showFullScreen() is not honored by every window manager (it sets
    // Qt's window state but can leave the geometry untouched, e.g. for transient
    // dialogs). Emulate fullscreen explicitly: remember the geometry, drop the
    // frame and cover the screen.
    if (fullscreen) {
        if (!m_panelNormalGeometry.contains(panelId)) {
            m_panelNormalGeometry.insert(panelId, pWidget->geometry());
        }
        pWidget->setProperty("mixxxEmulatedFullscreen", true);
        pWidget->setWindowFlag(Qt::FramelessWindowHint, true);
        pWidget->show();
        QScreen* pScreen = pWidget->screen();
        if (pScreen != nullptr) {
            pWidget->setGeometry(pScreen->geometry());
        }
    } else if (m_panelNormalGeometry.contains(panelId)) {
        const QRect normal = m_panelNormalGeometry.take(panelId);
        pWidget->setProperty("mixxxEmulatedFullscreen", false);
        pWidget->setWindowFlag(Qt::FramelessWindowHint, false);
        pWidget->show();
        if (normal.isValid()) {
            pWidget->setGeometry(normal);
        }
    }

    pWidget->raise();
    pWidget->activateWindow();
    kLogger.debug() << "setPanelFullscreen" << panelId << fullscreen
                    << "geometry:" << pWidget->geometry();
    return true;
}

bool PluginManager::isPanelFullscreen(const QString& panelId) const {
    // Reflect our own emulated state, since QWidget::isFullScreen() is not
    // reliable when the window manager ignores native fullscreen requests.
    return m_panelNormalGeometry.contains(panelId);
}

bool PluginManager::togglePanelFullscreen(const QString& panelId) {
    const bool next = !isPanelFullscreen(panelId);
    setPanelFullscreen(panelId, next);
    return next;
}

bool PluginManager::setPanelAlwaysOnTop(const QString& panelId, bool onTop) {
    QWidget* pWidget = m_panels.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    Qt::WindowFlags flags = pWidget->windowFlags();
    if (onTop) {
        flags |= Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }
    pWidget->setWindowFlags(flags);
    pWidget->show();
    return true;
}

bool PluginManager::evaluatePanelWeb(const QString& panelId, const QString& code) {
    QWidget* pWidget = m_webViews.value(panelId).data();
    if (pWidget == nullptr) {
        return false;
    }
    return evaluateWebPanel(pWidget, code);
}

bool PluginManager::eventFilter(QObject* pObject, QEvent* pEvent) {
    if (pEvent->type() == QEvent::Close) {
        const std::function<void()> onClose = m_panelCloseCallbacks.value(pObject);
        if (onClose) {
            onClose();
        }
    }
    return QObject::eventFilter(pObject, pEvent);
}

void PluginManager::removePanel(const QString& panelId) {
    for (int i = 0; i < m_pendingPanels.size(); ++i) {
        if (m_pendingPanels.at(i).panelId == panelId) {
            m_pendingPanels.removeAt(i);
            return;
        }
    }
    QWidget* pWidget = m_panels.take(panelId).data();
    m_panelWidgets.remove(panelId);
    m_webViews.remove(panelId);
    m_panelNormalGeometry.remove(panelId);
    m_panelPluginIds.remove(panelId);
    m_panelCloseCallbacks.remove(pWidget);
    if (pWidget != nullptr) {
        if (auto* pDock = qobject_cast<QDockWidget*>(pWidget)) {
            if (m_pMainWindow != nullptr) {
                m_pMainWindow->removeDockWidget(pDock);
            }
        }
        // deleteLater() is never processed once the event loop has stopped
        // (e.g. during ~PluginManager), so destroy synchronously then; a
        // parentless dialog window would otherwise leak.
        if (QThread::currentThread()->loopLevel() > 0) {
            pWidget->deleteLater();
        } else {
            delete pWidget;
        }
    }
}

void PluginManager::removePluginPanels(const QString& pluginId) {
    QStringList panelIds;
    for (auto it = m_panelPluginIds.constBegin(); it != m_panelPluginIds.constEnd(); ++it) {
        if (it.value() == pluginId) {
            panelIds.append(it.key());
        }
    }
    for (const QString& panelId : std::as_const(panelIds)) {
        removePanel(panelId);
    }
    m_paramPanels.remove(pluginId);
    for (int i = m_pendingPanels.size() - 1; i >= 0; --i) {
        if (m_pendingPanels.at(i).spec.pluginId == pluginId) {
            m_pendingPanels.removeAt(i);
        }
    }
}

QString PluginManager::addNativeGraphNode(const QString& pluginId,
        const QString& libraryPath, QString* pError) {
    if (m_host.pAudioGraph == nullptr) {
        if (pError != nullptr) {
            *pError = tr("The audio graph is unavailable.");
        }
        return QString();
    }
    const auto library = loadNativeLibrary(libraryPath, pError);
    if (!library) {
        return QString();
    }
    QLibrary* pLibrary = library->pLibrary;

    auto pNode = std::make_shared<NativePluginNode>(library->pVtable);
    const QString nodeId = m_host.pAudioGraph->addNode(pNode);
    if (nodeId.isEmpty()) {
        // The node never entered the graph, so no snapshot can reach the
        // vtable; it is safe to release the node and unload the library.
        pNode.reset();
        pLibrary->unload();
        delete pLibrary;
        if (pError != nullptr) {
            *pError = tr("Failed to add node to the audio graph.");
        }
        return QString();
    }
    // The node keeps a raw pointer into this library, and audio graph
    // snapshots can outlive removeGraphNode(), so the library must never be
    // unloaded; QLibrary keeps it mapped after the wrapper is destroyed.
    // Parent the wrapper to us so it is released on teardown.
    pLibrary->setParent(this);
    m_graphNodes.insert(nodeId, NativeGraphNode{pluginId, libraryPath});
    addNativeParamPanel(pluginId, pNode);
    return nodeId;
}

QString PluginManager::addNativeParamPanel(
        const QString& pluginId, const std::shared_ptr<PluginNode>& pNode) {
    if (!pNode || pNode->numParams() == 0) {
        return QString();
    }

    PluginPanelSpec spec;
    spec.pluginId = pluginId;
    spec.area = QStringLiteral("right");
    const QString nodeName = pNode->name();
    spec.title = tr("%1 Controls").arg(
            nodeName.isEmpty() ? pluginId : nodeName);

    QHash<quint32, uint32_t> paramTypes;
    const uint32_t count = pNode->numParams();
    for (uint32_t i = 0; i < count; ++i) {
        mixxx_param_info info{};
        if (!pNode->describeParam(i, &info)) {
            continue;
        }
        PluginPanelControl control;
        control.id = QString::number(info.id);
        control.text = QString::fromUtf8(info.name != nullptr ? info.name : "");
        control.min = info.min_value;
        control.max = info.max_value;
        control.value = static_cast<double>(info.default_value);
        switch (info.type) {
        case MIXXX_PARAM_BOOL:
            control.type = QStringLiteral("checkbox");
            control.checked = info.default_value != 0.0f;
            break;
        case MIXXX_PARAM_INT:
            control.type = QStringLiteral("number");
            control.step = 1.0;
            control.decimals = 0;
            break;
        case MIXXX_PARAM_TRIGGER:
            control.type = QStringLiteral("button");
            break;
        case MIXXX_PARAM_FLOAT:
        default:
            control.type = QStringLiteral("slider");
            control.step = (info.max_value - info.min_value) / 100.0f;
            control.decimals = 2;
            break;
        }
        spec.controls.append(control);
        paramTypes.insert(info.id, info.type);
    }

    if (spec.controls.isEmpty()) {
        return QString();
    }

    spec.onChange = [pNode, paramTypes](const QString& id, const QVariant& value) {
        const uint32_t paramId = id.toUInt();
        const uint32_t type = paramTypes.value(paramId, MIXXX_PARAM_FLOAT);
        mixxx_param_event event;
        event.id = paramId;
        event.type = type;
        event.sequence = 0;
        switch (type) {
        case MIXXX_PARAM_BOOL:
            event.value.b = value.toBool() ? 1u : 0u;
            break;
        case MIXXX_PARAM_INT:
            event.value.i = value.toInt();
            break;
        case MIXXX_PARAM_TRIGGER:
            event.value.f = 1.0f;
            break;
        case MIXXX_PARAM_FLOAT:
        default:
            event.value.f = static_cast<float>(value.toDouble());
            break;
        }
        pNode->pushParam(event);
    };

    const QString panelId = addPanel(spec);
    m_paramPanels.insert(pluginId, panelId);
    return panelId;
}

bool PluginManager::removeGraphNode(const QString& nodeId) {
    if (m_host.pAudioGraph == nullptr) {
        return false;
    }
    if (!m_host.pAudioGraph->removeNode(nodeId)) {
        return false;
    }
    const auto it = m_graphNodes.find(nodeId);
    if (it == m_graphNodes.end()) {
        return true;
    }
    const QString libraryPath = it->libraryPath;
    m_graphNodes.erase(it);
    if (libraryPath.isEmpty()) {
        return true;
    }
    const QList<QLibrary*> libraries = findChildren<QLibrary*>();
    for (QLibrary* pLibrary : libraries) {
        if (QFileInfo(pLibrary->fileName()).absoluteFilePath() ==
                QFileInfo(libraryPath).absoluteFilePath()) {
            // Never unload(): retired audio graph snapshots may still call
            // into the library's vtable. Only the wrapper is released.
            delete pLibrary;
            break;
        }
    }
    return true;
}

void PluginManager::ensureReplEngine() {
    if (m_pReplEngine != nullptr) {
        return;
    }
    m_pReplEngine = new QJSEngine(this);
    m_pReplEngine->installExtensions(QJSEngine::ConsoleExtension);
    m_pReplApi = new PluginApi(QStringLiteral("repl"),
            userPluginPath(),
            allCapabilities(),
            this,
            this);
    m_pReplApi->setEngine(m_pReplEngine);
    m_pReplApi->setBypassPermissionChecks(true);
    m_pReplEngine->globalObject().setProperty(QStringLiteral("mixxx"),
            m_pReplEngine->newQObject(m_pReplApi));
}

QJSEngine* PluginManager::replEngine() {
    ensureReplEngine();
    return m_pReplEngine;
}

bool PluginManager::evaluateRepl(const QString& code, QJSValue* pResult) {
    ensureReplEngine();
    const QJSValue result = m_pReplEngine->evaluate(code, QStringLiteral("<repl>"));
    if (pResult != nullptr) {
        *pResult = result;
    }
    return !result.isError();
}

} // namespace plugins
} // namespace mixxx
