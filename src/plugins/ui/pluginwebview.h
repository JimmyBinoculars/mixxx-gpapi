#pragma once

#include <QString>
#include <QVariant>
#include <functional>

class QWidget;

namespace mixxx {
namespace plugins {

class AudioGraph;

/// True if this build includes Qt WebEngine support for plugin web panels.
bool isWebPanelSupported();

/// Creates an embedded web view for a plugin panel, or returns nullptr when
/// this build has no WebEngine support.
///
/// `onMessage` is invoked on the GUI thread for every JSON value the page posts
/// through the `mixxxBridge` channel.
QWidget* createWebPanel(AudioGraph* pGraph,
        const QString& filePath,
        bool audioScope,
        std::function<void(const QVariant&)> onMessage,
        QWidget* pParent = nullptr);

/// Evaluates JavaScript in a panel created by createWebPanel(). Returns false
/// if `panel` is null or is not a web panel.
bool evaluateWebPanel(QWidget* panel, const QString& code);

} // namespace plugins
} // namespace mixxx
