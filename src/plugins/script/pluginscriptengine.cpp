#include "plugins/script/pluginscriptengine.h"

#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QMessageBox>

#include "errordialoghandler.h"
#include "moc_pluginscriptengine.cpp"
#include "plugins/pluginmanager.h"
#include "plugins/script/pluginjsproxies.h"
#include "plugins/ui/pluginmenuregistry.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {
const Logger kLogger("PluginScript");
}

PluginScriptEngine::PluginScriptEngine(PluginManifest manifest,
        QString pluginDirectory,
        PluginManager* pManager,
        QObject* pParent)
        : QObject(pParent),
          m_manifest(std::move(manifest)),
          m_pluginDirectory(std::move(pluginDirectory)),
          m_pManager(pManager) {
    m_entryPath = QDir(m_pluginDirectory).filePath(m_manifest.entryFile());
    connect(&m_fileWatcher,
            &QFileSystemWatcher::fileChanged,
            this,
            &PluginScriptEngine::reload);
}

PluginScriptEngine::~PluginScriptEngine() {
    shutdown();
}

bool PluginScriptEngine::initialize() {
    if (m_pEngine != nullptr) {
        return true;
    }

    if (!QFileInfo::exists(m_entryPath)) {
        showError(tr("Plugin Script Error"),
                tr("Script entry file does not exist: %1").arg(m_entryPath));
        return false;
    }

    m_pEngine = new QJSEngine(this);
    m_pEngine->installExtensions(QJSEngine::ConsoleExtension);

    m_pApi = new PluginApi(m_manifest.id,
            m_pluginDirectory,
            m_manifest.requestedCapabilities,
            m_pManager,
            this);
    m_pApi->setEngine(m_pEngine);

    QJSValue globalObject = m_pEngine->globalObject();
    globalObject.setProperty(QStringLiteral("mixxx"),
            m_pEngine->newQObject(m_pApi));

    const QJSValue module = m_pEngine->importModule(m_entryPath);
    if (module.isError()) {
        showError(tr("Plugin Script Error"),
                tr("Failed to load '%1':\n%2")
                        .arg(m_entryPath, module.toString()));
        shutdown();
        return false;
    }

    const QJSValue initFunction = module.property(QStringLiteral("init"));
    if (!initFunction.isCallable()) {
        showError(tr("Plugin Script Error"),
                tr("'%1' does not export an init() function.").arg(m_entryPath));
        shutdown();
        return false;
    }

    const QJSValue returnValue =
            initFunction.call({m_pEngine->newQObject(m_pApi)});
    if (returnValue.isError()) {
        showError(tr("Plugin Script Error"),
                tr("init() threw an exception:\n%1").arg(returnValue.toString()));
        shutdown();
        return false;
    }

    const QJSValue shutdownFunction = module.property(QStringLiteral("shutdown"));
    if (shutdownFunction.isCallable()) {
        m_shutdownFunction = shutdownFunction;
    } else {
        kLogger.debug() << "Module exports no shutdown() function.";
    }

    if (!m_fileWatcher.addPath(m_entryPath)) {
        kLogger.warning() << "Failed to watch script file" << m_entryPath;
    }
    kLogger.info() << "Initialized script plugin" << m_manifest.id;
    return true;
}

void PluginScriptEngine::shutdown() {
    if (m_pEngine == nullptr) {
        return;
    }
    emit beforeShutdown();

    if (m_shutdownFunction.isCallable()) {
        const QJSValue returnValue = m_shutdownFunction.call();
        if (returnValue.isError()) {
            kLogger.warning() << "shutdown() threw:" << returnValue.toString();
        }
    }
    m_shutdownFunction = QJSValue();

    // Drop UI registered through the old engine before it is destroyed: the
    // panel and menu callbacks hold QJSValues tied to this engine. On a full
    // unload PluginManager has already done this; on a hot reload it has not.
    if (m_pManager != nullptr) {
        if (m_pManager->menuRegistry() != nullptr) {
            m_pManager->menuRegistry()->removePluginItems(m_manifest.id);
        }
        m_pManager->removePluginPanels(m_manifest.id);
    }

    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty()) {
        m_fileWatcher.removePaths(watched);
    }

    delete m_pApi;
    m_pApi = nullptr;
    delete m_pEngine;
    m_pEngine = nullptr;
}

void PluginScriptEngine::reload() {
    kLogger.info() << "Reloading script plugin" << m_manifest.id;
    shutdown();
    initialize();
}

void PluginScriptEngine::showError(const QString& title, const QString& message) {
    kLogger.warning() << title << message;
    ErrorDialogProperties* pProps = ErrorDialogHandler::instance()->newDialogProperties();
    pProps->setType(DLG_WARNING);
    pProps->setTitle(title);
    pProps->setText(m_manifest.name.isEmpty() ? m_manifest.id : m_manifest.name);
    pProps->setInfoText(message);
    pProps->setKey(m_manifest.id + title);
    pProps->addButton(QMessageBox::Ok);
    pProps->setDefaultButton(QMessageBox::Ok);
    pProps->setEscapeButton(QMessageBox::Ok);
    pProps->setModal(false);
    ErrorDialogHandler::instance()->requestErrorDialog(pProps);
}

} // namespace plugins
} // namespace mixxx
