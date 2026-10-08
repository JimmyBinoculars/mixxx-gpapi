#include "plugins/script/processproxy.h"

#include <QDir>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QTimer>

#include "plugins/script/pluginjsproxies.h"

#include "moc_processproxy.cpp"

namespace mixxx {
namespace plugins {

namespace {

/// Milliseconds to wait for a process to exit after kill() before giving up.
constexpr int kKillGracePeriodMs = 1000;
/// Milliseconds to wait after terminate() before escalating to kill().
constexpr int kTerminateGracePeriodMs = 2000;
/// Default timeout for the blocking runSync() helper.
constexpr int kDefaultRunSyncTimeoutMs = 30000;

QProcessEnvironment buildEnvironment(const QJSValue& options) {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QJSValue envObject = options.property(QStringLiteral("env"));
    if (envObject.isObject()) {
        QJSValueIterator it(envObject);
        while (it.hasNext()) {
            it.next();
            environment.insert(it.name(), it.value().toString());
        }
    }
    const QString pathPrefix = options.property(QStringLiteral("pathPrefix")).toString();
    if (!pathPrefix.isEmpty()) {
        const QString path = environment.value(QStringLiteral("PATH"));
        environment.insert(QStringLiteral("PATH"),
                path.isEmpty()
                        ? pathPrefix
                        : pathPrefix + QDir::listSeparator() + path);
    }
    return environment;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ProcessHandle
// ---------------------------------------------------------------------------

ProcessHandle::ProcessHandle(QProcess* pProcess,
        QString program,
        QJSEngine* pEngine,
        QObject* pParent)
        : QObject(pParent),
          m_pProcess(pProcess),
          m_program(std::move(program)),
          m_pEngine(pEngine) {
    m_pProcess->setParent(this);
    connect(m_pProcess,
            &QProcess::readyReadStandardOutput,
            this,
            [this]() {
                if (!m_stdoutCallback.isCallable()) {
                    // Leave the data buffered so onStdout() can flush it.
                    return;
                }
                const QString chunk =
                        QString::fromUtf8(m_pProcess->readAllStandardOutput());
                m_stdoutCallback.call({chunk});
            });
    connect(m_pProcess,
            &QProcess::readyReadStandardError,
            this,
            [this]() {
                if (!m_stderrCallback.isCallable()) {
                    // Leave the data buffered so onStderr() can flush it.
                    return;
                }
                const QString chunk =
                        QString::fromUtf8(m_pProcess->readAllStandardError());
                m_stderrCallback.call({chunk});
            });
    connect(m_pProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                QJSValue result = m_pEngine != nullptr ? m_pEngine->newObject() : QJSValue();
                result.setProperty(QStringLiteral("exitCode"), exitCode);
                result.setProperty(QStringLiteral("ok"),
                        status == QProcess::NormalExit && exitCode == 0);
                if (status == QProcess::CrashExit) {
                    result.setProperty(QStringLiteral("error"), tr("Process crashed."));
                }
                if (m_exitCallback.isCallable()) {
                    m_exitCallback.call({result});
                }
            });
}

ProcessHandle::~ProcessHandle() {
    if (m_pProcess != nullptr && m_pProcess->state() != QProcess::NotRunning) {
        m_pProcess->kill();
        m_pProcess->waitForFinished(kKillGracePeriodMs);
    }
}

bool ProcessHandle::running() const {
    return m_pProcess != nullptr && m_pProcess->state() != QProcess::NotRunning;
}

void ProcessHandle::write(const QString& data) {
    if (m_pProcess != nullptr) {
        m_pProcess->write(data.toUtf8());
    }
}

void ProcessHandle::closeStdin() {
    if (m_pProcess != nullptr) {
        m_pProcess->closeWriteChannel();
    }
}

void ProcessHandle::kill() {
    if (m_pProcess != nullptr && m_pProcess->state() != QProcess::NotRunning) {
        m_pProcess->terminate();
        QTimer::singleShot(kTerminateGracePeriodMs, m_pProcess, [this]() {
            if (m_pProcess != nullptr && m_pProcess->state() != QProcess::NotRunning) {
                m_pProcess->kill();
            }
        });
    }
}

void ProcessHandle::onStdout(const QJSValue& callback) {
    m_stdoutCallback = callback;
    // Flush anything already buffered.
    if (m_pProcess != nullptr && m_stdoutCallback.isCallable()) {
        const QByteArray pending = m_pProcess->readAllStandardOutput();
        if (!pending.isEmpty()) {
            m_stdoutCallback.call({QString::fromUtf8(pending)});
        }
    }
}

void ProcessHandle::onStderr(const QJSValue& callback) {
    m_stderrCallback = callback;
    if (m_pProcess != nullptr && m_stderrCallback.isCallable()) {
        const QByteArray pending = m_pProcess->readAllStandardError();
        if (!pending.isEmpty()) {
            m_stderrCallback.call({QString::fromUtf8(pending)});
        }
    }
}

void ProcessHandle::onExit(const QJSValue& callback) {
    m_exitCallback = callback;
}

// ---------------------------------------------------------------------------
// ProcessProxy
// ---------------------------------------------------------------------------

ProcessProxy::ProcessProxy(PluginApi* pApi)
        : QObject(pApi),
          m_pApi(pApi) {
}

QStringList ProcessProxy::jsArgs(const QJSValue& args) const {
    QStringList result;
    if (args.isArray()) {
        const int count = args.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < count; ++i) {
            result.append(args.property(i).toString());
        }
    } else if (!args.isUndefined() && !args.isNull()) {
        result.append(args.toString());
    }
    return result;
}

void ProcessProxy::applyOptions(QProcess* pProcess, const QJSValue& options) const {
    if (options.isObject()) {
        const QString cwd = options.property(QStringLiteral("cwd")).toString();
        if (!cwd.isEmpty()) {
            pProcess->setWorkingDirectory(cwd);
        }
    }
    pProcess->setProcessEnvironment(buildEnvironment(options));
}

void ProcessProxy::applyTimeout(QProcess* pProcess, const QJSValue& options) const {
    const int timeoutMs = options.isObject()
            ? options.property(QStringLiteral("timeoutMs")).toInt()
            : 0;
    if (timeoutMs <= 0) {
        return;
    }
    QTimer::singleShot(timeoutMs, pProcess, [pProcess]() {
        if (pProcess != nullptr && pProcess->state() != QProcess::NotRunning) {
            pProcess->kill();
        }
    });
}

void ProcessProxy::run(const QString& program,
        const QJSValue& args,
        const QJSValue& callback) {
    runWithOptions(program, args, QJSValue(), callback);
}

void ProcessProxy::runWithOptions(const QString& program,
        const QJSValue& args,
        const QJSValue& options,
        const QJSValue& callback) {
    if (!m_pApi->checkCapability(PluginCapability::Exec)) {
        return;
    }
    QJSEngine* pEngine = m_pApi->engine();
    auto* pProcess = new QProcess(this);
    pProcess->setProcessChannelMode(QProcess::SeparateChannels);
    applyOptions(pProcess, options);
    applyTimeout(pProcess, options);

    connect(pProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [pEngine, pProcess, callback](int exitCode, QProcess::ExitStatus status) {
                QJSValue result = pEngine != nullptr ? pEngine->newObject() : QJSValue();
                result.setProperty(QStringLiteral("ok"),
                        status == QProcess::NormalExit && exitCode == 0);
                result.setProperty(QStringLiteral("exitCode"), exitCode);
                result.setProperty(QStringLiteral("stdout"),
                        QString::fromUtf8(pProcess->readAllStandardOutput()));
                result.setProperty(QStringLiteral("stderr"),
                        QString::fromUtf8(pProcess->readAllStandardError()));
                if (status == QProcess::CrashExit) {
                    result.setProperty(QStringLiteral("error"),
                            tr("Process crashed or was killed."));
                }
                if (callback.isCallable()) {
                    callback.call({result});
                }
                pProcess->deleteLater();
            });
    connect(pProcess,
            &QProcess::errorOccurred,
            this,
            [pEngine, pProcess, callback](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) {
                    return;
                }
                QJSValue result = pEngine != nullptr ? pEngine->newObject() : QJSValue();
                result.setProperty(QStringLiteral("ok"), false);
                result.setProperty(QStringLiteral("exitCode"), -1);
                result.setProperty(QStringLiteral("stdout"), QString());
                result.setProperty(QStringLiteral("stderr"), QString());
                result.setProperty(QStringLiteral("error"), pProcess->errorString());
                if (callback.isCallable()) {
                    callback.call({result});
                }
                pProcess->deleteLater();
            });

    pProcess->start(program, jsArgs(args));
}

QObject* ProcessProxy::spawn(const QString& program, const QJSValue& args) {
    return spawnWithOptions(program, args, QJSValue());
}

QObject* ProcessProxy::spawnWithOptions(const QString& program,
        const QJSValue& args,
        const QJSValue& options) {
    if (!m_pApi->checkCapability(PluginCapability::Exec)) {
        return nullptr;
    }
    auto* pProcess = new QProcess();
    pProcess->setProcessChannelMode(QProcess::SeparateChannels);
    applyOptions(pProcess, options);
    applyTimeout(pProcess, options);
    auto* pHandle = new ProcessHandle(pProcess, program, m_pApi->engine(), this);
    pProcess->start(program, jsArgs(args));
    return pHandle;
}

QVariantMap ProcessProxy::runSync(const QString& program,
        const QJSValue& args,
        const QJSValue& options) {
    QVariantMap result;
    if (!m_pApi->checkCapability(PluginCapability::Exec)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), tr("Permission denied."));
        return result;
    }
    int timeoutMs = options.isObject()
            ? options.property(QStringLiteral("timeoutMs")).toInt()
            : 0;
    if (timeoutMs <= 0) {
        timeoutMs = kDefaultRunSyncTimeoutMs;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    applyOptions(&process, options);
    process.start(program, jsArgs(args));
    if (!process.waitForStarted(timeoutMs)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("exitCode"), -1);
        result.insert(QStringLiteral("error"), process.errorString());
        result.insert(QStringLiteral("stdout"), QString());
        result.insert(QStringLiteral("stderr"), QString());
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(kKillGracePeriodMs);
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("exitCode"), -1);
        result.insert(QStringLiteral("error"), tr("Timed out."));
        result.insert(QStringLiteral("stdout"),
                QString::fromUtf8(process.readAllStandardOutput()));
        result.insert(QStringLiteral("stderr"),
                QString::fromUtf8(process.readAllStandardError()));
        return result;
    }
    result.insert(QStringLiteral("ok"),
            process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
    result.insert(QStringLiteral("exitCode"), process.exitCode());
    result.insert(QStringLiteral("stdout"),
            QString::fromUtf8(process.readAllStandardOutput()));
    result.insert(QStringLiteral("stderr"),
            QString::fromUtf8(process.readAllStandardError()));
    return result;
}

} // namespace plugins
} // namespace mixxx
