#include <gtest/gtest.h>

#include <QJSEngine>
#include <QStandardPaths>
#include <QVariantMap>
#include <memory>

#include "plugins/script/pluginjsproxies.h"
#include "plugins/script/processproxy.h"

namespace mixxx {
namespace plugins {

namespace {

// Returns the platform's `echo` executable, or an empty string when the
// platform has none (Windows ships echo as a shell builtin, not a program).
QString findEcho() {
    return QStandardPaths::findExecutable(QStringLiteral("echo"));
}

// Returns the platform's `false` executable, or an empty string when there is
// none. Used to exercise a process that exits with a non-zero status.
QString findFalse() {
    return QStandardPaths::findExecutable(QStringLiteral("false"));
}

} // namespace

// Exercises ProcessProxy through a live PluginApi. runSync() blocks until the
// child exits, so no event loop is needed.
class PluginProcessTest : public ::testing::Test {
  protected:
    // Builds a PluginApi requesting `capabilities` and returns its process
    // proxy, keeping the API (and its engine) alive for the test.
    ProcessProxy* makeProcessProxy(PluginCapabilitySet capabilities) {
        m_pApi = std::make_unique<PluginApi>(QStringLiteral("test"),
                QStringLiteral("/tmp"),
                capabilities,
                nullptr,
                nullptr);
        m_pApi->setEngine(&m_engine);
        return qobject_cast<ProcessProxy*>(m_pApi->process());
    }

    QJSEngine m_engine;
    std::unique_ptr<PluginApi> m_pApi;
};

TEST_F(PluginProcessTest, RunSyncCapturesStdoutAndExitCode) {
    const QString program = findEcho();
    if (program.isEmpty()) {
        GTEST_SKIP() << "No echo executable available on this platform.";
    }
    ProcessProxy* pProcess = makeProcessProxy(allCapabilities());
    ASSERT_NE(pProcess, nullptr);

    const QJSValue args = m_engine.evaluate(QStringLiteral("['hello', 'world']"));
    const QVariantMap result = pProcess->runSync(program, args, QJSValue());

    EXPECT_TRUE(result.value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("exitCode")).toInt(), 0);
    EXPECT_TRUE(result.value(QStringLiteral("stdout"))
                        .toString()
                        .contains(QStringLiteral("hello world")));
}

TEST_F(PluginProcessTest, RunSyncReportsNonZeroExitAsFailure) {
    const QString program = findFalse();
    if (program.isEmpty()) {
        GTEST_SKIP() << "No 'false' executable available on this platform.";
    }
    ProcessProxy* pProcess = makeProcessProxy(allCapabilities());
    ASSERT_NE(pProcess, nullptr);

    const QVariantMap result = pProcess->runSync(program, QJSValue(), QJSValue());

    EXPECT_FALSE(result.value(QStringLiteral("ok")).toBool());
    EXPECT_NE(result.value(QStringLiteral("exitCode")).toInt(), 0);
}

TEST_F(PluginProcessTest, MissingProgramFails) {
    ProcessProxy* pProcess = makeProcessProxy(allCapabilities());
    ASSERT_NE(pProcess, nullptr);

    const QVariantMap result = pProcess->runSync(
            QStringLiteral("/nonexistent/program/xyz"), QJSValue(), QJSValue());

    EXPECT_FALSE(result.value(QStringLiteral("ok")).toBool());
    EXPECT_FALSE(result.value(QStringLiteral("error")).toString().isEmpty());
}

TEST_F(PluginProcessTest, DeniedWithoutCapability) {
    // The program name is irrelevant: the call is denied before it is started.
    ProcessProxy* pProcess = makeProcessProxy(
            PluginCapabilitySet(PluginCapability::Network)); // no process.exec
    ASSERT_NE(pProcess, nullptr);

    const QVariantMap result = pProcess->runSync(
            QStringLiteral("echo"), QJSValue(), QJSValue());

    EXPECT_FALSE(result.value(QStringLiteral("ok")).toBool());
    EXPECT_FALSE(result.value(QStringLiteral("error")).toString().isEmpty());
}

} // namespace plugins
} // namespace mixxx
