// Stub implementation of the plugin web panel factory, used when this build
// has no Qt WebEngine support. See pluginwebview.cpp for the real one.

#include "plugins/ui/pluginwebview.h"

#include <utility>

namespace mixxx {
namespace plugins {

bool isWebPanelSupported() {
    return false;
}

QWidget* createWebPanel(AudioGraph* /*pGraph*/,
        const QString& /*filePath*/,
        bool /*audioScope*/,
        std::function<void(const QVariant&)> /*onMessage*/,
        QWidget* /*pParent*/) {
    return nullptr;
}

bool evaluateWebPanel(QWidget* /*panel*/, const QString& /*code*/) {
    return false;
}

} // namespace plugins
} // namespace mixxx
