// Embedded web view used as plugin panel content.
//
// This translation unit is only compiled when Qt WebEngine is available (see
// CMakeLists.txt). The Qt widget class lives here rather than in the header so
// AUTOMOC sees the Q_OBJECT classes without needing any compile definition.

#include "plugins/ui/pluginwebview.h"

#include <algorithm>
#include <utility>

#include <QByteArray>
#include <QHideEvent>
#include <QJsonDocument>
#include <QObject>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include "plugins/audio/audiograph.h"
#include "util/logger.h"

namespace mixxx {
namespace plugins {

namespace {

const Logger kLogger("PluginWebView");

/// ~60 Hz audio push cadence, matching a typical display refresh rate.
constexpr int kAudioIntervalMs = 16;

// Mapping from a float sample in [-1, 1] to unsigned 8-bit PCM, which is what
// the page expects (128 is silence).
constexpr float kPcmByteScale = 127.0f;
constexpr int kPcmByteOffset = 128;
constexpr int kPcmByteMax = 255;

} // namespace

/// Page-facing bridge. `postMessage` receives JSON from the web page and
/// forwards it to the host; `log` mirrors console output into the Mixxx log.
class PluginWebBridge : public QObject {
    Q_OBJECT
  public:
    explicit PluginWebBridge(QObject* pParent = nullptr)
            : QObject(pParent) {
    }

    Q_INVOKABLE void postMessage(const QString& json) {
        emit message(json);
    }
    Q_INVOKABLE void log(const QString& message) {
        kLogger.debug() << "page:" << message;
    }

  signals:
    void message(const QString& json);
};

class PluginWebView : public QWebEngineView {
    Q_OBJECT
  public:
    PluginWebView(AudioGraph* pGraph,
            const QString& filePath,
            bool audioScope,
            std::function<void(const QVariant&)> onMessage,
            QWidget* pParent = nullptr)
            : QWebEngineView(pParent),
              m_pGraph(pGraph),
              m_audioScope(audioScope) {
        // An unnamed profile is off-the-record: plugin web content cannot
        // persist cookies or cache to the user's profile.
        auto* pProfile = new QWebEngineProfile(this);
        auto* pPage = new QWebEnginePage(pProfile, this);
        setPage(pPage);

        QWebEngineSettings* pSettings = pProfile->settings();
        pSettings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
        pSettings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
        pSettings->setAttribute(QWebEngineSettings::WebGLEnabled, true);
        pSettings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);

        auto* pChannel = new QWebChannel(this);
        auto* pBridge = new PluginWebBridge(this);
        pChannel->registerObject(QStringLiteral("mixxxBridge"), pBridge);
        pPage->setWebChannel(pChannel);
        connect(pBridge,
                &PluginWebBridge::message,
                this,
                [onMessage = std::move(onMessage)](const QString& json) {
                    if (!onMessage) {
                        return;
                    }
                    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                    onMessage(doc.isNull() ? QVariant(json) : doc.toVariant());
                });

        if (m_audioScope && m_pGraph != nullptr) {
            m_pAudioTimer = new QTimer(this);
            m_pAudioTimer->setInterval(kAudioIntervalMs);
            connect(m_pAudioTimer, &QTimer::timeout, this, &PluginWebView::pushAudioFrame);
        }

        load(QUrl::fromLocalFile(filePath));
    }

    ~PluginWebView() override {
        if (m_scopeAcquired && m_pGraph != nullptr) {
            m_pGraph->releaseAudioScope();
            m_scopeAcquired = false;
        }
    }

    void evaluate(const QString& code) {
        page()->runJavaScript(code);
    }

  protected:
    void showEvent(QShowEvent* pEvent) override {
        QWebEngineView::showEvent(pEvent);
        if (m_audioScope && !m_scopeAcquired && m_pGraph != nullptr) {
            m_pGraph->acquireAudioScope();
            m_scopeAcquired = true;
        }
        if (m_pAudioTimer != nullptr && !m_pAudioTimer->isActive()) {
            m_pAudioTimer->start();
        }
    }

    void hideEvent(QHideEvent* pEvent) override {
        QWebEngineView::hideEvent(pEvent);
        if (m_pAudioTimer != nullptr) {
            m_pAudioTimer->stop();
        }
        if (m_scopeAcquired && m_pGraph != nullptr) {
            m_pGraph->releaseAudioScope();
            m_scopeAcquired = false;
        }
    }

  private:
    void pushAudioFrame() {
        if (m_pGraph == nullptr) {
            return;
        }
        constexpr int kFrames = AudioScopeTap::kWindowFrames;
        float left[kFrames];
        float right[kFrames];
        float mono[kFrames];
        m_pGraph->readAudioScope(kFrames, left, right, mono);

        auto toByte = [](float sample) {
            const int scaled =
                    static_cast<int>(sample * kPcmByteScale) + kPcmByteOffset;
            return static_cast<char>(std::clamp(scaled, 0, kPcmByteMax));
        };
        QByteArray leftBytes(kFrames, '\0');
        QByteArray rightBytes(kFrames, '\0');
        QByteArray monoBytes(kFrames, '\0');
        for (int i = 0; i < kFrames; ++i) {
            leftBytes[i] = toByte(left[i]);
            rightBytes[i] = toByte(right[i]);
            monoBytes[i] = toByte(mono[i]);
        }

        const QString script = QStringLiteral(
                "window.MixxxBridge && window.MixxxBridge.pushAudio("
                "\"%1\",\"%2\",\"%3\");")
                .arg(QString::fromLatin1(leftBytes.toBase64()),
                        QString::fromLatin1(rightBytes.toBase64()),
                        QString::fromLatin1(monoBytes.toBase64()));
        page()->runJavaScript(script);
    }

    AudioGraph* m_pGraph;
    bool m_audioScope;
    bool m_scopeAcquired = false;
    QTimer* m_pAudioTimer = nullptr;
};

bool isWebPanelSupported() {
    return true;
}

QWidget* createWebPanel(AudioGraph* pGraph,
        const QString& filePath,
        bool audioScope,
        std::function<void(const QVariant&)> onMessage,
        QWidget* pParent) {
    return new PluginWebView(
            pGraph, filePath, audioScope, std::move(onMessage), pParent);
}

bool evaluateWebPanel(QWidget* panel, const QString& code) {
    auto* pView = qobject_cast<PluginWebView*>(panel);
    if (pView == nullptr) {
        return false;
    }
    pView->evaluate(code);
    return true;
}

} // namespace plugins
} // namespace mixxx

#include "pluginwebview.moc"
