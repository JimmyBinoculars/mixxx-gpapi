#include "plugins/script/pluginjsproxies.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QSslError>
#include <QUrl>

#include "control/controlobject.h"
#include "library/library.h"
#include "library/librarytablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playermanager.h"
#include "moc_pluginjsproxies.cpp"
#include "plugins/audio/audiograph.h"
#include "plugins/audio/pluginnode.h"
#include "plugins/library/remotesourcemanager.h"
#include "plugins/pluginmanager.h"
#include "plugins/pluginstore.h"
#include "plugins/script/processproxy.h"
#include "plugins/ui/pluginmenuregistry.h"
#include "track/track.h"
#include "track/trackref.h"
#include "util/desktophelper.h"
#include "util/fileinfo.h"
#include "util/logger.h"
#include "util/versionstore.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginScript");

QJSValue makeResult(QJSEngine* pEngine, bool ok, const QString& error = QString()) {
    QJSValue result = pEngine != nullptr ? pEngine->newObject() : QJSValue();
    result.setProperty(QStringLiteral("ok"), ok);
    if (!error.isEmpty()) {
        result.setProperty(QStringLiteral("error"), error);
    }
    return result;
}

/// Invokes `callback` with a failure result. Shared by the proxies that must
/// report an error without starting the asynchronous work.
void callWithError(
        QJSEngine* pEngine, const QJSValue& callback, const QString& error) {
    if (callback.isCallable()) {
        callback.call({makeResult(pEngine, false, error)});
    }
}

QString jsString(const QJSValue& object, const char* key, const QString& fallback = QString()) {
    const QJSValue value = object.property(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        return fallback;
    }
    return value.toString();
}

bool jsBool(const QJSValue& object, const char* key, bool fallback = false) {
    const QJSValue value = object.property(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        return fallback;
    }
    return value.toBool();
}

double jsNumber(const QJSValue& object, const char* key, double fallback = 0.0) {
    const QJSValue value = object.property(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) {
        return fallback;
    }
    return value.toNumber();
}

bool isWithinPath(const QString& path, const QString& root) {
    if (root.isEmpty()) {
        return false;
    }
    if (path == root) {
        return true;
    }
    return path.startsWith(root + QDir::separator());
}

/// True if `path` is inside `root`, accepting both the root as given (which may
/// be a symlink, e.g. an installed plugin directory) and its canonical form.
bool isWithinRootOrCanonical(const QString& path, const QString& root) {
    if (isWithinPath(path, root)) {
        return true;
    }
    const QFileInfo rootInfo(root);
    const QString canonicalRoot = rootInfo.canonicalFilePath().isEmpty()
            ? rootInfo.absoluteFilePath()
            : rootInfo.canonicalFilePath();
    if (isWithinPath(path, canonicalRoot)) {
        return true;
    }
    // `path` may itself be expressed through the symlink; resolve its deepest
    // existing ancestor and retry.
    const QFileInfo pathInfo(path);
    QString ancestor = pathInfo.absolutePath();
    QString suffix = pathInfo.fileName();
    while (!ancestor.isEmpty()) {
        const QFileInfo ancestorInfo(ancestor);
        const QString canonicalAncestor = ancestorInfo.canonicalFilePath();
        if (!canonicalAncestor.isEmpty()) {
            const QString canonicalPath = suffix.isEmpty()
                    ? canonicalAncestor
                    : QDir(canonicalAncestor).filePath(suffix);
            return isWithinPath(canonicalPath, canonicalRoot) ||
                    isWithinPath(canonicalPath, root);
        }
        const QString name = ancestorInfo.fileName();
        if (name.isEmpty()) {
            break;
        }
        suffix = suffix.isEmpty() ? name : name + QLatin1Char('/') + suffix;
        const QString parent = ancestorInfo.absolutePath();
        if (parent == ancestor) {
            break;
        }
        ancestor = parent;
    }
    return false;
}

/// Returns the cleaned absolute form of `path`, interpreting a relative `path`
/// as relative to `baseDir`. Does not resolve symlinks and does not clean up
/// beyond Qt's lexical ".." handling.
QString cleanedAbsolutePath(const QString& baseDir, const QString& path) {
    return QFileInfo(QDir(baseDir).filePath(path)).absoluteFilePath();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PluginApi
// ---------------------------------------------------------------------------

PluginApi::PluginApi(QString pluginId,
        QString pluginDir,
        PluginCapabilitySet requestedCapabilities,
        PluginManager* pManager,
        QObject* pParent)
        : QObject(pParent),
          m_pluginId(std::move(pluginId)),
          m_pluginDir(std::move(pluginDir)),
          m_requestedCapabilities(requestedCapabilities),
          m_pManager(pManager) {
    m_pControls = new ControlsProxy(this);
    m_pMenu = new MenuProxy(this);
    m_pUi = new UiProxy(this);
    m_pNet = new NetProxy(this);
    m_pFiles = new FilesProxy(this);
    m_pLibrary = new LibraryProxy(this);
    m_pSettings = new SettingsProxy(this);
    m_pAudio = new AudioProxy(this);
    m_pDownload = new DownloadProxy(this);
    m_pProcess = new ProcessProxy(this);
    m_pCrypto = new CryptoProxy(this);
    m_pRemote = new RemoteProxy(this);
}

PluginApi::~PluginApi() {
    if (m_pManager != nullptr && m_pManager->remoteSources() != nullptr) {
        m_pManager->remoteSources()->removePluginSources(m_pluginId);
    }
}

QObject* PluginApi::controls() {
    return m_pControls;
}
QObject* PluginApi::menu() {
    return m_pMenu;
}
QObject* PluginApi::ui() {
    return m_pUi;
}
QObject* PluginApi::net() {
    return m_pNet;
}
QObject* PluginApi::files() {
    return m_pFiles;
}
QObject* PluginApi::library() {
    return m_pLibrary;
}
QObject* PluginApi::settings() {
    return m_pSettings;
}
QObject* PluginApi::audio() {
    return m_pAudio;
}
QObject* PluginApi::download() {
    return m_pDownload;
}
QObject* PluginApi::process() {
    return m_pProcess;
}
QObject* PluginApi::crypto() {
    return m_pCrypto;
}
QObject* PluginApi::remote() {
    return m_pRemote;
}

QString PluginApi::mixxxVersion() const {
    return VersionStore::version();
}

bool PluginApi::checkCapability(PluginCapability capability) {
    if (capability == PluginCapability::Invalid || m_bypassPermissionChecks) {
        return true;
    }
    if (!m_requestedCapabilities.testFlag(capability)) {
        if (!m_deniedLogged.testFlag(capability)) {
            kLogger.warning() << "Plugin" << m_pluginId
                               << "tried to use capability"
                               << capabilityToString(capability)
                               << "without requesting it in the manifest.";
            m_deniedLogged |= capability;
        }
        return false;
    }
    if (m_pManager != nullptr &&
            !m_pManager->permissions()->isGranted(m_pluginId, capability)) {
        if (!m_deniedLogged.testFlag(capability)) {
            kLogger.warning() << "Plugin" << m_pluginId
                               << "was denied capability"
                               << capabilityToString(capability)
                               << "by the user.";
            m_deniedLogged |= capability;
        }
        return false;
    }
    return true;
}

void PluginApi::log(const QString& message) {
    kLogger.info().noquote() << QStringLiteral("[%1] %2").arg(m_pluginId, message);
}

bool PluginApi::hasCapability(const QString& capability) const {
    const PluginCapability parsed = capabilityFromString(capability);
    if (parsed == PluginCapability::Invalid) {
        return false;
    }
    if (m_bypassPermissionChecks) {
        return true;
    }
    return m_requestedCapabilities.testFlag(parsed) &&
            (m_pManager == nullptr ||
                    m_pManager->permissions()->isGranted(m_pluginId, parsed));
}

// ---------------------------------------------------------------------------
// ControlsProxy
// ---------------------------------------------------------------------------

ControlsProxy::ControlsProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

double ControlsProxy::get(const QString& group, const QString& item) const {
    if (!m_pApi->checkCapability(PluginCapability::ControlsRead)) {
        return 0.0;
    }
    return ControlObject::get(ConfigKey(group, item));
}

bool ControlsProxy::set(
        const QString& group, const QString& item, double value) {
    if (!m_pApi->checkCapability(PluginCapability::ControlsWrite)) {
        return false;
    }
    const ConfigKey key(group, item);
    if (!ControlObject::exists(key)) {
        return false;
    }
    ControlObject::set(key, value);
    return true;
}

bool ControlsProxy::trigger(const QString& group, const QString& item) {
    return set(group, item, 1.0);
}

bool ControlsProxy::exists(const QString& group, const QString& item) const {
    if (!m_pApi->checkCapability(PluginCapability::ControlsRead)) {
        return false;
    }
    return ControlObject::exists(ConfigKey(group, item));
}

// ---------------------------------------------------------------------------
// MenuProxy
// ---------------------------------------------------------------------------

MenuProxy::MenuProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QString MenuProxy::addItem(const QJSValue& options) {
    if (!m_pApi->checkCapability(PluginCapability::UiMenu)) {
        return QString();
    }
    PluginMenuRegistry* pRegistry = m_pApi->manager() != nullptr
            ? m_pApi->manager()->menuRegistry()
            : nullptr;
    if (pRegistry == nullptr) {
        return QString();
    }

    PluginMenuItem item;
    item.pluginId = m_pApi->pluginId();
    item.topLevelMenu = jsString(options, "menu", QStringLiteral("Plugins"));
    item.subMenu = jsString(options, "submenu");
    item.text = jsString(options, "text");
    item.shortcut = jsString(options, "shortcut");
    item.checkable = jsBool(options, "checkable");
    item.checked = jsBool(options, "checked");
    item.enabled = jsBool(options, "enabled", true);
    if (item.text.isEmpty()) {
        return QString();
    }

    const QJSValue callback = options.property(QStringLiteral("callback"));
    if (callback.isCallable()) {
        item.trigger = [callback]() {
            callback.call();
        };
    }
    const QJSValue toggleCallback = options.property(QStringLiteral("onToggle"));
    if (toggleCallback.isCallable()) {
        item.toggle = [toggleCallback](bool checked) {
            toggleCallback.call({checked});
        };
    }

    return pRegistry->addMenuItem(std::move(item));
}

void MenuProxy::removeItem(const QString& itemId) {
    if (!m_pApi->checkCapability(PluginCapability::UiMenu)) {
        return;
    }
    if (m_pApi->manager() != nullptr &&
            m_pApi->manager()->menuRegistry() != nullptr) {
        m_pApi->manager()->menuRegistry()->removeMenuItem(itemId);
    }
}

void MenuProxy::clear() {
    if (m_pApi->manager() != nullptr &&
            m_pApi->manager()->menuRegistry() != nullptr) {
        m_pApi->manager()->menuRegistry()->removePluginItems(m_pApi->pluginId());
    }
}

// ---------------------------------------------------------------------------
// UiProxy
// ---------------------------------------------------------------------------

UiProxy::UiProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QString UiProxy::addPanel(const QJSValue& options) {
    return addPanelImpl(options, false);
}

QString UiProxy::addDialog(const QJSValue& options) {
    return addPanelImpl(options, true);
}

QString UiProxy::addPanelImpl(const QJSValue& options, bool dialog) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel)) {
        return QString();
    }
    if (m_pApi->manager() == nullptr) {
        return QString();
    }

    PluginPanelSpec spec;
    spec.pluginId = m_pApi->pluginId();
    spec.title = jsString(options, "title", m_pApi->pluginId());
    spec.area = jsString(options, "area", QStringLiteral("right"));
    spec.qmlFile = jsString(options, "qml");
    spec.webFile = jsString(options, "web");
    spec.audioScope = jsBool(options, "audio", false);
    spec.chrome = jsBool(options, "chrome", true);
    spec.aspectRatio = jsNumber(options, "aspectRatio", 0.0);
    spec.dialog = dialog || jsBool(options, "dialog", false);
    if (!spec.webFile.isEmpty() &&
            !m_pApi->checkCapability(PluginCapability::UiWeb)) {
        return QString();
    }
    if (spec.audioScope &&
            !m_pApi->checkCapability(PluginCapability::AudioScope)) {
        spec.audioScope = false;
    }

    QHash<QString, QJSValue> controlCallbacks;
    const QJSValue controls = options.property(QStringLiteral("controls"));
    if (controls.isArray()) {
        const int count = controls.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < count; ++i) {
            const QJSValue entry = controls.property(i);
            if (!entry.isObject()) {
                continue;
            }
            PluginPanelControl control;
            control.id = jsString(entry, "id");
            control.type = jsString(entry, "type", QStringLiteral("label"));
            control.text = jsString(entry, "text", jsString(entry, "label"));
            const QJSValue value = entry.property(QStringLiteral("value"));
            if (!value.isUndefined() && !value.isNull()) {
                control.value = value.toVariant();
            }
            control.min = jsNumber(entry, "min", 0.0);
            control.max = jsNumber(entry, "max", 1.0);
            control.step = jsNumber(entry, "step", 0.0);
            control.decimals = static_cast<int>(jsNumber(entry, "decimals", 2));
            control.checked = jsBool(entry, "checked", false);
            control.iconWidth = static_cast<int>(jsNumber(entry, "iconWidth", 0));
            control.iconHeight = static_cast<int>(jsNumber(entry, "iconHeight", 0));
            const QJSValue choices = entry.property(QStringLiteral("choices"));
            if (choices.isArray()) {
                const int choiceCount = choices.property(QStringLiteral("length")).toInt();
                for (int c = 0; c < choiceCount; ++c) {
                    control.choices.append(choices.property(c).toString());
                }
            }
            const QJSValue itemsValue = entry.property(QStringLiteral("items"));
            if (itemsValue.isArray()) {
                const int itemCount = itemsValue.property(QStringLiteral("length")).toInt();
                for (int c = 0; c < itemCount; ++c) {
                    const QJSValue item = itemsValue.property(c);
                    if (item.isObject()) {
                        control.items.append(
                                item.property(QStringLiteral("text")).toString());
                        control.itemValues.append(
                                item.property(QStringLiteral("value")).toVariant());
                        control.itemIcons.append(
                                item.property(QStringLiteral("icon")).toString());
                    } else {
                        control.items.append(item.toString());
                        control.itemValues.append(QVariant());
                        control.itemIcons.append(QString());
                    }
                }
            }
            const QJSValue callback = entry.property(QStringLiteral("callback"));
            if (callback.isCallable() && !control.id.isEmpty()) {
                controlCallbacks.insert(control.id, callback);
            }
            spec.controls.append(control);
        }
    }

    const QJSValue onChange = options.property(QStringLiteral("onChange"));
    if (onChange.isCallable() || !controlCallbacks.isEmpty()) {
        QJSEngine* pEngine = m_pApi->engine();
        spec.onChange = [pEngine, onChange, controlCallbacks](
                                const QString& id, const QVariant& value) {
            const QJSValue callback = controlCallbacks.value(id);
            if (callback.isCallable()) {
                if (value.isValid()) {
                    callback.call({pEngine != nullptr
                                           ? pEngine->toScriptValue(value)
                                           : QJSValue(value.toString())});
                } else {
                    callback.call();
                }
                return;
            }
            if (onChange.isCallable()) {
                QJSValueList args;
                args << QJSValue(id);
                if (value.isValid()) {
                    args << (pEngine != nullptr
                                    ? pEngine->toScriptValue(value)
                                    : QJSValue(value.toString()));
                }
                onChange.call(args);
            }
        };
    }

    const QJSValue onClose = options.property(QStringLiteral("onClose"));
    if (onClose.isCallable()) {
        spec.onClose = [onClose]() {
            onClose.call();
        };
    }

    const QJSValue onWebMessage = options.property(QStringLiteral("onWebMessage"));
    if (onWebMessage.isCallable()) {
        QJSEngine* pEngine = m_pApi->engine();
        spec.onWebMessage = [pEngine, onWebMessage](const QVariant& data) {
            onWebMessage.call({pEngine != nullptr ? pEngine->toScriptValue(data)
                                                  : QJSValue(data.toString())});
        };
    }

    return m_pApi->manager()->addPanel(spec);
}

bool UiProxy::setPanelValue(const QString& panelId,
        const QString& controlId,
        const QJSValue& value) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelValue(panelId, controlId, value.toVariant());
}

bool UiProxy::setPanelItems(const QString& panelId,
        const QString& controlId,
        const QStringList& items) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelItems(panelId, controlId, items);
}

bool UiProxy::setPanelList(const QString& panelId,
        const QString& controlId,
        const QJSValue& items) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    QStringList texts;
    QVariantList values;
    QStringList icons;
    if (items.isArray()) {
        const int count = items.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < count; ++i) {
            const QJSValue item = items.property(i);
            if (item.isObject()) {
                texts.append(item.property(QStringLiteral("text")).toString());
                values.append(item.property(QStringLiteral("value")).toVariant());
                icons.append(item.property(QStringLiteral("icon")).toString());
            } else {
                texts.append(item.toString());
                values.append(QVariant());
                icons.append(QString());
            }
        }
    }
    return m_pApi->manager()->setPanelListItems(panelId, controlId, texts, values, icons);
}

bool UiProxy::setPanelListItemIcon(const QString& panelId,
        const QString& controlId,
        int index,
        const QString& iconPath) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelListItemIcon(panelId, controlId, index, iconPath);
}

bool UiProxy::setPanelEnabled(const QString& panelId,
        const QString& controlId,
        bool enabled) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelEnabled(panelId, controlId, enabled);
}

bool UiProxy::showPanel(const QString& panelId) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelVisible(panelId, true);
}

bool UiProxy::hidePanel(const QString& panelId) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelVisible(panelId, false);
}

bool UiProxy::panelVisible(const QString& panelId) const {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->isPanelVisible(panelId);
}

bool UiProxy::setPanelGeometry(const QString& panelId,
        int x,
        int y,
        int width,
        int height) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelGeometry(panelId, x, y, width, height);
}

QVariantMap UiProxy::panelGeometry(const QString& panelId) const {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return QVariantMap();
    }
    return m_pApi->manager()->panelGeometry(panelId);
}

bool UiProxy::panelIsDialog(const QString& panelId) const {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->isPanelDialog(panelId);
}

bool UiProxy::setPanelFullscreen(const QString& panelId, bool fullscreen) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelFullscreen(panelId, fullscreen);
}

bool UiProxy::togglePanelFullscreen(const QString& panelId) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->togglePanelFullscreen(panelId);
}

bool UiProxy::panelFullscreen(const QString& panelId) const {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->isPanelFullscreen(panelId);
}

bool UiProxy::setPanelAlwaysOnTop(const QString& panelId, bool onTop) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->setPanelAlwaysOnTop(panelId, onTop);
}

bool UiProxy::webEval(const QString& panelId, const QString& code) {
    if (!m_pApi->checkCapability(PluginCapability::UiWeb) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->evaluatePanelWeb(panelId, code);
}

void UiProxy::removePanel(const QString& panelId) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel)) {
        return;
    }
    if (m_pApi->manager() != nullptr) {
        m_pApi->manager()->removePanel(panelId);
    }
}

void UiProxy::notify(const QString& title, const QString& message) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel)) {
        return;
    }
    auto* pBox = new QMessageBox(QMessageBox::Information,
            title.isEmpty() ? m_pApi->pluginId() : title,
            message,
            QMessageBox::Ok,
            nullptr);
    pBox->setAttribute(Qt::WA_DeleteOnClose);
    pBox->setModal(false);
    pBox->show();
}

QString UiProxy::pickFolder(const QString& title, const QString& startDir) {
    if (!m_pApi->checkCapability(PluginCapability::FilesPaths) ||
            m_pApi->manager() == nullptr) {
        return QString();
    }
    const QString dir = QFileDialog::getExistingDirectory(nullptr,
            title.isEmpty() ? tr("Choose folder") : title,
            startDir,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return QString();
    }
    const QFileInfo info(dir);
    const QString canonical = info.canonicalFilePath().isEmpty()
            ? info.absoluteFilePath()
            : info.canonicalFilePath();
    m_pApi->manager()->permissions()->addAllowedPath(m_pApi->pluginId(), canonical);
    return dir;
}

QString UiProxy::pickFile(
        const QString& title, const QString& startDir, const QString& filter) {
    if (!m_pApi->checkCapability(PluginCapability::FilesPaths) ||
            m_pApi->manager() == nullptr) {
        return QString();
    }
    const QString file = QFileDialog::getOpenFileName(
            nullptr, title.isEmpty() ? tr("Choose file") : title, startDir, filter);
    if (file.isEmpty()) {
        return QString();
    }
    m_pApi->manager()->permissions()->addAllowedPath(
            m_pApi->pluginId(), QFileInfo(file).absolutePath());
    return file;
}

bool UiProxy::openInFileManager(const QString& path) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel) || path.isEmpty()) {
        return false;
    }
    mixxx::DesktopHelper::openUrl(QUrl::fromLocalFile(path));
    return true;
}

void UiProxy::copyToClipboard(const QString& text) {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel)) {
        return;
    }
    QGuiApplication::clipboard()->setText(text);
}

QString UiProxy::clipboardText() const {
    if (!m_pApi->checkCapability(PluginCapability::UiPanel)) {
        return QString();
    }
    return QGuiApplication::clipboard()->text();
}

// ---------------------------------------------------------------------------
// NetProxy
// ---------------------------------------------------------------------------

NetProxy::NetProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

void NetProxy::setIgnoreSslErrors(bool ignore) {
    if (!m_pApi->checkCapability(PluginCapability::Network)) {
        return;
    }
    m_ignoreSslErrors = ignore;
}

void NetProxy::attachIgnoreSsl(QNetworkReply* pReply) {
    if (pReply == nullptr || !m_ignoreSslErrors) {
        return;
    }
    connect(pReply, &QNetworkReply::sslErrors, this, [pReply](const QList<QSslError>&) {
        pReply->ignoreSslErrors();
    });
}

void NetProxy::get(const QString& url, const QJSValue& callback) {
    if (!m_pApi->checkCapability(PluginCapability::Network)) {
        return;
    }
    QNetworkAccessManager* pNetwork = m_pApi->manager() != nullptr
            ? m_pApi->manager()->networkManager()
            : nullptr;
    if (pNetwork == nullptr) {
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* pReply = pNetwork->get(request);
    attachIgnoreSsl(pReply);
    handleReply(pReply, callback);
}

void NetProxy::post(
        const QString& url, const QString& body, const QJSValue& callback) {
    if (!m_pApi->checkCapability(PluginCapability::Network)) {
        return;
    }
    QNetworkAccessManager* pNetwork = m_pApi->manager() != nullptr
            ? m_pApi->manager()->networkManager()
            : nullptr;
    if (pNetwork == nullptr) {
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/json; charset=utf-8"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* pReply = pNetwork->post(request, body.toUtf8());
    attachIgnoreSsl(pReply);
    handleReply(pReply, callback);
}

void NetProxy::download(
        const QString& url, const QString& relativePath, const QJSValue& callback) {
    downloadImpl(url, relativePath, callback, QJSValue());
}

void NetProxy::downloadWithProgress(const QString& url,
        const QString& relativePath,
        const QJSValue& callback,
        const QJSValue& onProgress) {
    downloadImpl(url, relativePath, callback, onProgress);
}

void NetProxy::downloadTo(const QString& url,
        const QString& absolutePath,
        const QJSValue& callback) {
    downloadToImpl(url, absolutePath, callback, QJSValue());
}

void NetProxy::downloadToWithProgress(const QString& url,
        const QString& absolutePath,
        const QJSValue& callback,
        const QJSValue& onProgress) {
    downloadToImpl(url, absolutePath, callback, onProgress);
}

/// Returns the cleaned absolute path of `path` when it is inside an approved
/// root; an empty string otherwise.
QString NetProxy::resolveApprovedTarget(const QString& path) const {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (isWithinRootOrCanonical(absolute, m_pApi->pluginDir())) {
        return absolute;
    }
    if (m_pApi->manager() == nullptr) {
        return QString();
    }
    const QStringList allowed =
            m_pApi->manager()->permissions()->allowedPaths(m_pApi->pluginId());
    for (const QString& allowedPath : allowed) {
        if (isWithinRootOrCanonical(absolute, allowedPath)) {
            return absolute;
        }
    }
    return QString();
}

void NetProxy::downloadImpl(const QString& url,
        const QString& relativePath,
        const QJSValue& callback,
        const QJSValue& onProgress) {
    if (!m_pApi->checkCapability(PluginCapability::Network)) {
        return;
    }
    // QDir::filePath() neither cleans ".." nor rejects absolute paths, so the
    // target must be validated explicitly. Approved paths outside the plugin
    // directory are only allowed through downloadTo().
    const QString target = cleanedAbsolutePath(m_pApi->pluginDir(), relativePath);
    if (!isWithinRootOrCanonical(target, m_pApi->pluginDir())) {
        callWithError(m_pApi->engine(),
                callback,
                tr("Download path must stay inside the plugin directory: %1")
                        .arg(relativePath));
        return;
    }
    startDownload(url, target, callback, onProgress);
}

void NetProxy::downloadToImpl(const QString& url,
        const QString& absolutePath,
        const QJSValue& callback,
        const QJSValue& onProgress) {
    if (!m_pApi->checkCapability(PluginCapability::Network)) {
        return;
    }
    if (!m_pApi->checkCapability(PluginCapability::FilesPaths)) {
        return;
    }
    const QString target = resolveApprovedTarget(absolutePath);
    if (target.isEmpty()) {
        callWithError(m_pApi->engine(),
                callback,
                tr("Path is not approved (use mixxx.ui.pickFolder() first): %1")
                        .arg(absolutePath));
        return;
    }
    startDownload(url, target, callback, onProgress);
}

void NetProxy::startDownload(const QString& url,
        const QString& target,
        const QJSValue& callback,
        const QJSValue& onProgress) {
    if (!m_pApi->checkCapability(PluginCapability::Network)) {
        return;
    }
    QNetworkAccessManager* pNetwork = m_pApi->manager() != nullptr
            ? m_pApi->manager()->networkManager()
            : nullptr;
    if (pNetwork == nullptr) {
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* pReply = pNetwork->get(request);
    attachIgnoreSsl(pReply);
    m_callbacks.insert(pReply, callback);

    if (onProgress.isCallable()) {
        m_progressCallbacks.insert(pReply, onProgress);
        connect(pReply,
                &QNetworkReply::downloadProgress,
                this,
                [this, pReply](qint64 received, qint64 total) {
                    const QJSValue progressCb = m_progressCallbacks.value(pReply);
                    if (progressCb.isCallable()) {
                        progressCb.call({QJSValue(static_cast<double>(received)),
                                QJSValue(static_cast<double>(total))});
                    }
                });
    }

    connect(pReply, &QNetworkReply::finished, this, [this, pReply, target]() {
        m_progressCallbacks.remove(pReply);
        const QJSValue cb = m_callbacks.take(pReply);
        pReply->deleteLater();
        QJSEngine* pEngine = m_pApi->engine();
        QJSValue result = makeResult(pEngine,
                pReply->error() == QNetworkReply::NoError,
                pReply->errorString());
        if (pReply->error() == QNetworkReply::NoError) {
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile file(target);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(pReply->readAll());
                result.setProperty(QStringLiteral("path"), target);
            } else {
                result.setProperty(QStringLiteral("ok"), false);
                result.setProperty(QStringLiteral("error"),
                        tr("Cannot write to %1").arg(target));
            }
        }
        if (cb.isCallable()) {
            cb.call({result});
        }
    });
}

void NetProxy::handleReply(
        QNetworkReply* pReply, const QJSValue& callback) {
    m_callbacks.insert(pReply, callback);
    connect(pReply, &QNetworkReply::finished, this, [this, pReply]() {
        const QJSValue cb = m_callbacks.take(pReply);
        pReply->deleteLater();
        QJSEngine* pEngine = m_pApi->engine();
        const bool ok = pReply->error() == QNetworkReply::NoError;
        QJSValue result = makeResult(pEngine, ok, pReply->errorString());
        result.setProperty(QStringLiteral("status"),
                pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        result.setProperty(QStringLiteral("body"),
                QString::fromUtf8(pReply->readAll()));
        if (cb.isCallable()) {
            cb.call({result});
        }
    });
}

// ---------------------------------------------------------------------------
// FilesProxy
// ---------------------------------------------------------------------------

FilesProxy::FilesProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QString FilesProxy::resolve(const QString& relativePath) const {
    const QString absolute = cleanedAbsolutePath(m_pApi->pluginDir(), relativePath);
    // Always allow the plugin's own directory (which may be a symlink).
    if (isWithinRootOrCanonical(absolute, m_pApi->pluginDir())) {
        return absolute;
    }
    // Outside the plugin directory requires an approved path (files.paths).
    if (m_pApi->manager() != nullptr) {
        const QStringList allowed =
                m_pApi->manager()->permissions()->allowedPaths(m_pApi->pluginId());
        for (const QString& allowedPath : allowed) {
            if (isWithinRootOrCanonical(absolute, allowedPath)) {
                return absolute;
            }
        }
    }
    return QString();
}

bool FilesProxy::allowPath(const QString& path) {
    if (!m_pApi->checkCapability(PluginCapability::FilesPaths) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath().isEmpty()
            ? info.absoluteFilePath()
            : info.canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    m_pApi->manager()->permissions()->addAllowedPath(m_pApi->pluginId(), canonical);
    return true;
}

QStringList FilesProxy::allowedPaths() const {
    if (!m_pApi->checkCapability(PluginCapability::FilesPaths) ||
            m_pApi->manager() == nullptr) {
        return QStringList();
    }
    return m_pApi->manager()->permissions()->allowedPaths(m_pApi->pluginId());
}

bool FilesProxy::revokePath(const QString& path) {
    if (!m_pApi->checkCapability(PluginCapability::FilesPaths) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    m_pApi->manager()->permissions()->removeAllowedPath(m_pApi->pluginId(), path);
    return true;
}

QString FilesProxy::readText(const QString& relativePath) const {
    if (!m_pApi->checkCapability(PluginCapability::FilesRead)) {
        return QString();
    }
    const QString path = resolve(relativePath);
    if (path.isEmpty()) {
        return QString();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool FilesProxy::writeText(const QString& relativePath, const QString& contents) {
    if (!m_pApi->checkCapability(PluginCapability::FilesWrite)) {
        return false;
    }
    const QString path = resolve(relativePath);
    if (path.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    return file.write(contents.toUtf8()) >= 0;
}

bool FilesProxy::exists(const QString& relativePath) const {
    if (!m_pApi->checkCapability(PluginCapability::FilesRead)) {
        return false;
    }
    const QString path = resolve(relativePath);
    if (path.isEmpty()) {
        return false;
    }
    return QFileInfo::exists(path);
}

bool FilesProxy::remove(const QString& path) const {
    if (!m_pApi->checkCapability(PluginCapability::FilesWrite)) {
        return false;
    }
    const QString resolved = resolve(path);
    if (resolved.isEmpty() || !QFileInfo::exists(resolved)) {
        return false;
    }
    return QFile::remove(resolved);
}

QVariantList FilesProxy::list(const QString& relativeDir) const {
    QVariantList result;
    if (!m_pApi->checkCapability(PluginCapability::FilesRead)) {
        return result;
    }
    const QString path = resolve(relativeDir);
    if (path.isEmpty()) {
        return result;
    }
    const QDir dir(path);
    const QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
    for (const QString& entry : entries) {
        result.append(entry);
    }
    return result;
}

QString FilesProxy::absolutePath(const QString& relativePath) const {
    return resolve(relativePath);
}

// ---------------------------------------------------------------------------
// LibraryProxy
// ---------------------------------------------------------------------------

LibraryProxy::LibraryProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

int LibraryProxy::trackCount() const {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRead) ||
            m_pApi->manager() == nullptr) {
        return 0;
    }
    const auto pLibrary = m_pApi->manager()->host().pLibrary;
    if (!pLibrary) {
        return 0;
    }
    const LibraryTableModel* pModel = pLibrary->trackTableModel();
    return pModel != nullptr ? pModel->rowCount() : 0;
}

QVariantMap LibraryProxy::getTrack(const QString& location) const {
    QVariantMap result;
    if (!m_pApi->checkCapability(PluginCapability::LibraryRead) ||
            m_pApi->manager() == nullptr) {
        return result;
    }
    const auto pCollectionManager = m_pApi->manager()->host().pTrackCollectionManager;
    if (!pCollectionManager) {
        return result;
    }
    const TrackPointer pTrack = pCollectionManager->getTrackByRef(
            TrackRef::fromFileInfo(mixxx::FileInfo(location)));
    if (!pTrack) {
        return result;
    }
    result.insert(QStringLiteral("location"), pTrack->getLocation());
    result.insert(QStringLiteral("title"), pTrack->getTitle());
    result.insert(QStringLiteral("artist"), pTrack->getArtist());
    result.insert(QStringLiteral("album"), pTrack->getAlbum());
    result.insert(QStringLiteral("albumArtist"), pTrack->getAlbumArtist());
    result.insert(QStringLiteral("genre"), pTrack->getGenre());
    result.insert(QStringLiteral("comment"), pTrack->getComment());
    result.insert(QStringLiteral("year"), pTrack->getYear());
    result.insert(QStringLiteral("trackNumber"), pTrack->getTrackNumber());
    result.insert(QStringLiteral("duration"), pTrack->getDuration());
    result.insert(QStringLiteral("bitrate"), pTrack->getBitrate());
    result.insert(QStringLiteral("sampleRate"), pTrack->getSampleRate().toDouble());
    return result;
}

bool LibraryProxy::loadTrack(const QString& location, int deck, bool play) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryWrite) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    const auto pLibrary = m_pApi->manager()->host().pLibrary;
    if (!pLibrary) {
        return false;
    }
    if (deck < 1) {
        return false;
    }
    const QString group = PlayerManager::groupForDeck(deck - 1);
    pLibrary->slotLoadLocationToPlayer(location, group, play);
    return true;
}

void LibraryProxy::search(const QString& query) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRead) ||
            m_pApi->manager() == nullptr) {
        return;
    }
    const auto pLibrary = m_pApi->manager()->host().pLibrary;
    if (pLibrary) {
        pLibrary->searchTracksInCollection(query);
    }
}

void LibraryProxy::refresh() {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRead) ||
            m_pApi->manager() == nullptr) {
        return;
    }
    const auto pLibrary = m_pApi->manager()->host().pLibrary;
    if (pLibrary) {
        pLibrary->slotRefreshLibraryModels();
    }
}

void LibraryProxy::rescan() {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRead) ||
            m_pApi->manager() == nullptr) {
        return;
    }
    const auto pCollectionManager = m_pApi->manager()->host().pTrackCollectionManager;
    if (pCollectionManager) {
        pCollectionManager->startLibraryScan();
    }
}

bool LibraryProxy::addFolder(const QString& path) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryWrite) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    const auto pLibrary = m_pApi->manager()->host().pLibrary;
    if (!pLibrary) {
        return false;
    }
    const bool added = pLibrary->requestAddDir(path);
    const auto pCollectionManager = m_pApi->manager()->host().pTrackCollectionManager;
    if (pCollectionManager) {
        pCollectionManager->startLibraryScan();
    }
    return added;
}

QStringList LibraryProxy::folders() const {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRead) ||
            m_pApi->manager() == nullptr) {
        return QStringList();
    }
    const auto pCollectionManager = m_pApi->manager()->host().pTrackCollectionManager;
    if (!pCollectionManager) {
        return QStringList();
    }
    const TrackCollection* pCollection = pCollectionManager->internalCollection();
    return pCollection != nullptr ? pCollection->getRootDirStrings() : QStringList();
}

// ---------------------------------------------------------------------------
// SettingsProxy
// ---------------------------------------------------------------------------

SettingsProxy::SettingsProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QString SettingsProxy::settingKey(const QString& key) const {
    return QStringLiteral("Settings_%1").arg(key);
}

QVariant SettingsProxy::get(const QString& key, const QVariant& defaultValue) const {
    if (m_pApi->manager() == nullptr) {
        return defaultValue;
    }
    const UserSettingsPointer pConfig = m_pApi->manager()->host().pConfig;
    const QString group = QStringLiteral("[Plugin:%1]").arg(m_pApi->pluginId());
    const QString value = pConfig->getValueString(ConfigKey(group, settingKey(key)));
    if (value.isEmpty()) {
        return defaultValue;
    }
    // Preserve the type implied by the caller's default value. Without a
    // (typed) default the raw stored string is returned.
    switch (defaultValue.typeId()) {
    case QMetaType::Bool:
        return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 ||
                value == QLatin1String("1");
    case QMetaType::Int:
    case QMetaType::LongLong:
        return value.toLongLong();
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble();
    default:
        return value;
    }
}

void SettingsProxy::set(const QString& key, const QVariant& value) {
    if (m_pApi->manager() == nullptr) {
        return;
    }
    const UserSettingsPointer pConfig = m_pApi->manager()->host().pConfig;
    const QString group = QStringLiteral("[Plugin:%1]").arg(m_pApi->pluginId());
    pConfig->setValue(ConfigKey(group, settingKey(key)), value.toString());
}

void SettingsProxy::remove(const QString& key) {
    if (m_pApi->manager() == nullptr) {
        return;
    }
    const UserSettingsPointer pConfig = m_pApi->manager()->host().pConfig;
    const QString group = QStringLiteral("[Plugin:%1]").arg(m_pApi->pluginId());
    pConfig->remove(ConfigKey(group, settingKey(key)));
}

// ---------------------------------------------------------------------------
// AudioProxy
// ---------------------------------------------------------------------------

AudioProxy::AudioProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QString AudioProxy::addNativeNode(const QString& libraryPath) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr) {
        return QString();
    }
    // The library must live inside the plugin directory; reject absolute paths
    // or ".." escapes that would let a plugin load arbitrary shared objects.
    const QString absoluteLibraryPath =
            cleanedAbsolutePath(m_pApi->pluginDir(), libraryPath);
    if (!isWithinRootOrCanonical(absoluteLibraryPath, m_pApi->pluginDir())) {
        m_pApi->log(QStringLiteral(
                "Refusing to load a native node outside the plugin directory: %1")
                            .arg(libraryPath));
        return QString();
    }
    QString error;
    const QString nodeId = m_pApi->manager()->addNativeGraphNode(
            m_pApi->pluginId(), absoluteLibraryPath, &error);
    if (nodeId.isEmpty() && !error.isEmpty()) {
        m_pApi->log(QStringLiteral("Failed to add native node: %1").arg(error));
    }
    return nodeId;
}

bool AudioProxy::removeNode(const QString& nodeId) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr) {
        return false;
    }
    return m_pApi->manager()->removeGraphNode(nodeId);
}

QVariantList AudioProxy::nodes() const {
    QVariantList result;
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return result;
    }
    AudioGraph* pGraph = m_pApi->manager()->audioGraph();
    for (const QString& nodeId : pGraph->nodeIds()) {
        const std::shared_ptr<PluginNode> pNode = pGraph->node(nodeId);
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), nodeId);
        entry.insert(QStringLiteral("name"), pNode ? pNode->name() : QString());
        entry.insert(QStringLiteral("inputs"), pNode ? static_cast<int>(pNode->numInputs()) : 0);
        entry.insert(QStringLiteral("outputs"), pNode ? static_cast<int>(pNode->numOutputs()) : 0);
        result.append(entry);
    }
    return result;
}

bool AudioProxy::connect(const QString& sourceNodeId,
        int sourceChannel,
        const QString& destNodeId,
        int destChannel) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return false;
    }
    return m_pApi->manager()->audioGraph()->connect(sourceNodeId,
            static_cast<uint32_t>(sourceChannel),
            destNodeId,
            static_cast<uint32_t>(destChannel));
}

bool AudioProxy::connectInput(
        int inputChannel, const QString& destNodeId, int destChannel) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return false;
    }
    return m_pApi->manager()->audioGraph()->connectExternalInput(
            static_cast<uint32_t>(inputChannel),
            destNodeId,
            static_cast<uint32_t>(destChannel));
}

bool AudioProxy::connectOutput(
        const QString& sourceNodeId, int sourceChannel, int outputChannel) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return false;
    }
    return m_pApi->manager()->audioGraph()->connectToExternalOutput(sourceNodeId,
            static_cast<uint32_t>(sourceChannel),
            static_cast<uint32_t>(outputChannel));
}

bool AudioProxy::disconnect(const QString& destNodeId, int destChannel) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return false;
    }
    return m_pApi->manager()->audioGraph()->disconnect(
            destNodeId, static_cast<uint32_t>(destChannel));
}

bool AudioProxy::setParam(const QString& nodeId, int paramId, double value) {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return false;
    }
    const std::shared_ptr<PluginNode> pNode =
            m_pApi->manager()->audioGraph()->node(nodeId);
    if (!pNode) {
        return false;
    }
    mixxx_param_event event;
    event.id = static_cast<uint32_t>(paramId);
    event.type = MIXXX_PARAM_FLOAT;
    event.value.f = static_cast<float>(value);
    event.sequence = 0;
    return pNode->pushParam(event);
}

void AudioProxy::clear() {
    if (!m_pApi->checkCapability(PluginCapability::AudioGraph) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return;
    }
    m_pApi->manager()->audioGraph()->clear();
}

QVariantMap AudioProxy::readScope() const {
    QVariantMap result;
    if (!m_pApi->checkCapability(PluginCapability::AudioScope) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->audioGraph() == nullptr) {
        return result;
    }
    AudioGraph* pGraph = m_pApi->manager()->audioGraph();
    const int frames = AudioScopeTap::kWindowFrames;
    float left[AudioScopeTap::kWindowFrames];
    float right[AudioScopeTap::kWindowFrames];
    float mono[AudioScopeTap::kWindowFrames];
    pGraph->readAudioScope(frames, left, right, mono);

    QVariantList leftList;
    QVariantList rightList;
    QVariantList monoList;
    leftList.reserve(frames);
    rightList.reserve(frames);
    monoList.reserve(frames);
    for (int i = 0; i < frames; ++i) {
        leftList.append(left[i]);
        rightList.append(right[i]);
        monoList.append(mono[i]);
    }
    result.insert(QStringLiteral("sampleRate"), pGraph->sampleRate());
    result.insert(QStringLiteral("frames"), frames);
    result.insert(QStringLiteral("left"), leftList);
    result.insert(QStringLiteral("right"), rightList);
    result.insert(QStringLiteral("mono"), monoList);
    return result;
}

// ---------------------------------------------------------------------------
// DownloadProxy
// ---------------------------------------------------------------------------

DownloadProxy::DownloadProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

void DownloadProxy::installFromUrl(
        const QString& url, const QString& sha256, const QJSValue& callback) {
    if (!m_pApi->checkCapability(PluginCapability::Download) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->store() == nullptr) {
        return;
    }
    PluginStore* pStore = m_pApi->manager()->store();
    const quint64 requestId = ++m_installCounter;
    m_pendingInstalls.insert(requestId, callback);
    pStore->installFromUrl(QUrl(url), sha256,
            [guard = QPointer<DownloadProxy>(this), requestId](
                    bool success, const QString& pluginId, const QString& error) {
                if (guard.isNull()) {
                    // The plugin (and its engine) was unloaded while the
                    // download was in flight; do not touch the callback.
                    return;
                }
                const QJSValue cb = guard->m_pendingInstalls.take(requestId);
                QJSEngine* pEngine = guard->m_pApi->engine();
                QJSValue result = makeResult(pEngine, success, error);
                result.setProperty(QStringLiteral("pluginId"), pluginId);
                if (cb.isCallable()) {
                    cb.call({result});
                }
            });
}

// ---------------------------------------------------------------------------
// CryptoProxy
// ---------------------------------------------------------------------------

CryptoProxy::CryptoProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QString CryptoProxy::md5(const QString& text) const {
    return QString::fromLatin1(QCryptographicHash::hash(
                                       text.toUtf8(), QCryptographicHash::Md5)
                                       .toHex());
}

QString CryptoProxy::randomHex(int byteCount) const {
    const int count = qBound(1, byteCount, 64);
    QByteArray bytes(count, 0);
    for (int i = 0; i < count; ++i) {
        bytes[i] = static_cast<char>(
                QRandomGenerator::global()->generate() & 0xff);
    }
    return QString::fromLatin1(bytes.toHex());
}

// ---------------------------------------------------------------------------
// RemoteProxy
// ---------------------------------------------------------------------------

RemoteProxy::RemoteProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

namespace {
QString slugify(const QString& text) {
    QString slug;
    for (const QChar& ch : text) {
        if (ch.isLetterOrNumber()) {
            slug.append(ch.toLower());
        } else if (!slug.endsWith(QLatin1Char('-'))) {
            slug.append(QLatin1Char('-'));
        }
    }
    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug;
}
} // anonymous namespace

QString RemoteProxy::addSource(const QJSValue& spec) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr) {
        return QString();
    }
    RemoteSourceManager* pSources = m_pApi->manager()->remoteSources();
    if (pSources == nullptr) {
        return QString();
    }

    PluginRemoteSource source;
    source.pluginId = m_pApi->pluginId();
    source.engine = m_pApi->engine();
    source.name = jsString(spec, "name", m_pApi->pluginId());
    source.search = spec.property(QStringLiteral("search"));
    source.resolve = spec.property(QStringLiteral("resolve"));
    source.download = spec.property(QStringLiteral("download"));
    source.isDownloaded = spec.property(QStringLiteral("isDownloaded"));
    source.removeDownload = spec.property(QStringLiteral("removeDownload"));
    source.coverArt = spec.property(QStringLiteral("coverArt"));
    source.count = spec.property(QStringLiteral("count"));

    if (!source.search.isCallable() || !source.resolve.isCallable()) {
        m_pApi->log(QStringLiteral(
                "mixxx.remote.addSource: search() and resolve() are required."));
        return QString();
    }

    QString id = jsString(spec, "id");
    if (id.isEmpty()) {
        id = source.pluginId + QLatin1Char('/') + slugify(source.name);
    }
    source.sourceId = id;
    return pSources->addSource(source);
}

void RemoteProxy::removeSource(const QString& sourceId) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->remoteSources() == nullptr) {
        return;
    }
    RemoteSourceManager* pSources = m_pApi->manager()->remoteSources();
    // Only allow removing sources that belong to this plugin.
    if (pSources->sourcePluginId(sourceId) == m_pApi->pluginId()) {
        pSources->removeSource(sourceId);
    }
}

void RemoteProxy::refresh(const QString& sourceId) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->remoteSources() == nullptr) {
        return;
    }
    m_pApi->manager()->remoteSources()->refresh(sourceId);
}

void RemoteProxy::reportProgress(const QString& sourceId,
        const QString& trackId,
        double received,
        double total) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->remoteSources() == nullptr) {
        return;
    }
    m_pApi->manager()->remoteSources()->reportProgress(
            sourceId,
            trackId,
            static_cast<qint64>(received),
            static_cast<qint64>(total));
}

void RemoteProxy::removeProgress(const QString& sourceId, const QString& trackId) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->remoteSources() == nullptr) {
        return;
    }
    m_pApi->manager()->remoteSources()->removeProgress(sourceId, trackId);
}

QString RemoteProxy::addTrackAction(const QJSValue& options) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->remoteSources() == nullptr) {
        return QString();
    }
    RemoteSourceManager* pSources = m_pApi->manager()->remoteSources();

    PluginRemoteTrackAction action;
    action.pluginId = m_pApi->pluginId();
    action.engine = m_pApi->engine();
    action.sourceId = jsString(options, "sourceId");
    action.text = jsString(options, "text");
    action.callback = options.property(QStringLiteral("callback"));
    if (action.sourceId.isEmpty() || action.text.isEmpty() ||
            !action.callback.isCallable()) {
        return QString();
    }
    if (pSources->sourcePluginId(action.sourceId) != m_pApi->pluginId()) {
        return QString();
    }
    action.actionId = QStringLiteral("%1:action:%2")
                              .arg(action.pluginId)
                              .arg(++m_actionCounter);
    pSources->addTrackAction(action.actionId, action);
    return action.actionId;
}

void RemoteProxy::removeTrackAction(const QString& actionId) {
    if (!m_pApi->checkCapability(PluginCapability::LibraryRemote) ||
            m_pApi->manager() == nullptr ||
            m_pApi->manager()->remoteSources() == nullptr) {
        return;
    }
    m_pApi->manager()->remoteSources()->removeTrackAction(actionId);
}

} // namespace plugins
} // namespace mixxx
