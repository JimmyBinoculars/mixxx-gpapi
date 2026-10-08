#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QWidget>
#include <functional>

namespace mixxx {
namespace plugins {

/// A single declarative control inside a plugin panel.
///
/// `type` is one of:
///   "label", "separator", "button", "slider", "checkbox", "number",
///   "text", "choice".
struct PluginPanelControl {
    QString id;
    QString type = QStringLiteral("label");
    QString text;
    QVariant value;
    double min = 0.0;
    double max = 1.0;
    double step = 0.01;
    int decimals = 2;
    bool checked = false;
    QStringList choices;
    /// For type "list".
    QStringList items;
    /// Optional per-item values for type "list" (parallel to `items`). When
    /// present, the value is reported instead of the text.
    QVariantList itemValues;
    /// Optional per-item icon file paths for type "list" (parallel to `items`).
    /// Empty entries leave the corresponding row without an icon.
    QStringList itemIcons;
    /// Icon display size for type "list". Zero means the style default.
    int iconWidth = 0;
    int iconHeight = 0;
};

/// Declarative description of a plugin-contributed dock panel.
///
/// This is deliberately independent of QML so panels work in every build. A
/// spec may instead (or additionally) carry a `qmlFile`, which is used when the
/// build has QML support; otherwise `controls` are rendered natively.
struct PluginPanelSpec {
    QString pluginId;
    QString title;
    QString area = QStringLiteral("right");
    QString qmlFile;
    /// HTML file relative to the plugin directory, rendered in an embedded
    /// web view. Requires a build with Qt WebEngine and the `ui.web`
    /// capability.
    QString webFile;
    /// When true, attach the master-output audio scope to the web view so it
    /// can drive visualizations. Requires the `audio.scope` capability.
    bool audioScope = false;
    /// When true the panel is realized as a modeless top-level QDialog instead
    /// of a dock widget, so opening it never reflows the main window.
    bool dialog = false;
    /// When false, dialog panels omit the window chrome (no Close button,
    /// smaller minimum size) so visual content can fill the window.
    bool chrome = true;
    /// When > 0, dialog panels keep this width/height ratio while being resized
    /// (e.g. 16.0 / 9.0). Ignored while fullscreen or maximized.
    double aspectRatio = 0.0;
    QList<PluginPanelControl> controls;
    /// Invoked on the GUI thread when a control changes. `id` is the control
    /// id and `value` its new value; buttons pass an invalid QVariant.
    std::function<void(const QString& id, const QVariant& value)> onChange;
    /// Invoked on the GUI thread when the embedded web page posts a message
    /// through the bridge.
    std::function<void(const QVariant& data)> onWebMessage;
    /// Invoked when the user closes the dock panel.
    std::function<void()> onClose;
};

/// Renders a PluginPanelSpec into a plain QWidget and reports changes through
/// the spec's onChange callback. Requires no QML runtime.
class PluginPanelWidget : public QWidget {
    Q_OBJECT
  public:
    explicit PluginPanelWidget(
            const PluginPanelSpec& spec, QWidget* pParent = nullptr);

    /// Updates an existing control's value without invoking onChange.
    void setValue(const QString& controlId, const QVariant& value);

    /// Replaces the items of a "list" control without invoking onChange.
    void setItems(const QString& controlId, const QStringList& items);

    /// Replaces the items and their values of a "list" control. When a value is
    /// present it is reported instead of the text. `icons` is optional and
    /// parallel to `texts`; empty entries leave a row without an icon.
    void setListItems(const QString& controlId,
            const QStringList& texts,
            const QVariantList& values,
            const QStringList& icons = QStringList());

    /// Sets (or clears, with an empty path) the icon of a single list row
    /// without disturbing the rest of the list or its selection.
    void setListItemIcon(const QString& controlId, int index, const QString& iconPath);

    /// Enables/disables a control (e.g. while a job is running).
    void setControlEnabled(const QString& controlId, bool enabled);

  private:
    QWidget* buildControl(const PluginPanelControl& control);
    void emitChange(const QString& id, const QVariant& value);
    /// Returns the spec of the control with `controlId`, or nullptr.
    const PluginPanelControl* findControl(const QString& controlId) const;

    const PluginPanelSpec m_spec;
    QHash<QString, QWidget*> m_controls;
};

} // namespace plugins
} // namespace mixxx
