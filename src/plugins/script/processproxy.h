#pragma once

#include <QJSValue>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QJSEngine;

namespace mixxx {
namespace plugins {

class PluginApi;

/// Handle for a process started with `mixxx.process.spawn()`.
///
/// Output is streamed: register `onStdout`/`onStderr`/`onExit` callbacks to
/// receive chunks as they are produced. Requires the `process.exec` capability
/// (checked by the proxy before the handle is created).
class ProcessHandle : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString program READ program CONSTANT)
    Q_PROPERTY(bool running READ running)
  public:
    ProcessHandle(QProcess* pProcess,
            QString program,
            QJSEngine* pEngine,
            QObject* pParent = nullptr);
    ~ProcessHandle() override;

    QString program() const {
        return m_program;
    }
    bool running() const;

    /// Writes to the process's standard input.
    Q_INVOKABLE void write(const QString& data);
    /// Closes standard input (many tools finish when stdin closes).
    Q_INVOKABLE void closeStdin();
    /// Requests termination, then kills after a short grace period.
    Q_INVOKABLE void kill();

    Q_INVOKABLE void onStdout(const QJSValue& callback);
    Q_INVOKABLE void onStderr(const QJSValue& callback);
    Q_INVOKABLE void onExit(const QJSValue& callback);

  private:
    QProcess* m_pProcess;
    QString m_program;
    QJSEngine* m_pEngine;
    QJSValue m_stdoutCallback;
    QJSValue m_stderrCallback;
    QJSValue m_exitCallback;
};

/// mixxx.process — run external programs (requires `process.exec`).
///
/// Because this executes arbitrary programs, it is capability-gated and shown
/// in the consent dialog.
class ProcessProxy : public QObject {
    Q_OBJECT
  public:
    explicit ProcessProxy(PluginApi* pApi);

    /// Runs `program` and reports once when it exits. `callback(result)` with
    /// `{ok, exitCode, stdout, stderr, error?}`.
    Q_INVOKABLE void run(const QString& program,
            const QJSValue& args,
            const QJSValue& callback);

    /// As run(), with options: { cwd, env: {K: V}, pathPrefix, timeoutMs }.
    Q_INVOKABLE void runWithOptions(const QString& program,
            const QJSValue& args,
            const QJSValue& options,
            const QJSValue& callback);

    /// Starts `program` and returns a streaming ProcessHandle (or null on
    /// failure).
    Q_INVOKABLE QObject* spawn(const QString& program, const QJSValue& args);

    /// As spawn(), with options.
    Q_INVOKABLE QObject* spawnWithOptions(const QString& program,
            const QJSValue& args,
            const QJSValue& options);

    /// Blocking convenience wrapper (pass `{}` for options). Returns
    /// `{ok, exitCode, stdout, stderr, error?}`. Blocks the GUI thread, so use
    /// only for short commands.
    Q_INVOKABLE QVariantMap runSync(const QString& program,
            const QJSValue& args,
            const QJSValue& options);

  private:
    QStringList jsArgs(const QJSValue& args) const;
    void applyOptions(QProcess* pProcess, const QJSValue& options) const;
    void applyTimeout(QProcess* pProcess, const QJSValue& options) const;

    PluginApi* m_pApi;
};

} // namespace plugins
} // namespace mixxx
