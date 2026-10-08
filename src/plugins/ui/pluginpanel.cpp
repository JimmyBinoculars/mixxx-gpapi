#include "plugins/ui/pluginpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSize>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "moc_pluginpanel.cpp"

namespace mixxx {
namespace plugins {

namespace {

// Sliders are integer based; use a fixed resolution that is fine for the
// parameter ranges plugins typically expose.
constexpr int kSliderResolution = 1000;
const char* const kValueLabelName = "PluginPanelValueLabel";

// Applies an icon to a list row when a non-empty, loadable path is given.
void applyItemIcon(QListWidgetItem* pItem, const QString& iconPath) {
    if (iconPath.isEmpty()) {
        return;
    }
    const QIcon icon(iconPath);
    if (!icon.isNull()) {
        pItem->setIcon(icon);
    }
}

} // anonymous namespace

PluginPanelWidget::PluginPanelWidget(
        const PluginPanelSpec& spec, QWidget* pParent)
        : QWidget(pParent),
          m_spec(spec) {
    auto* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(8, 8, 8, 8);
    pLayout->setSpacing(6);

    for (const PluginPanelControl& control : m_spec.controls) {
        QWidget* pWidget = buildControl(control);
        if (pWidget != nullptr) {
            pLayout->addWidget(pWidget);
            if (!control.id.isEmpty()) {
                m_controls.insert(control.id, pWidget);
            }
        }
    }
    pLayout->addStretch(1);
}

QWidget* PluginPanelWidget::buildControl(const PluginPanelControl& control) {
    const QString type = control.type.trimmed().toLower();
    const QString id = control.id;

    if (type == QLatin1String("separator")) {
        auto* pLine = new QFrame(this);
        pLine->setFrameShape(QFrame::HLine);
        pLine->setFrameShadow(QFrame::Sunken);
        return pLine;
    }

    if (type == QLatin1String("button")) {
        auto* pButton = new QPushButton(control.text, this);
        connect(pButton, &QPushButton::clicked, this, [this, id]() {
            emitChange(id, QVariant());
        });
        return pButton;
    }

    if (type == QLatin1String("checkbox")) {
        auto* pCheck = new QCheckBox(control.text, this);
        pCheck->setChecked(control.checked);
        connect(pCheck, &QCheckBox::toggled, this, [this, id](bool checked) {
            emitChange(id, checked);
        });
        return pCheck;
    }

    if (type == QLatin1String("number")) {
        auto* pSpin = new QDoubleSpinBox(this);
        pSpin->setRange(control.min, control.max);
        pSpin->setSingleStep(control.step > 0.0 ? control.step : 0.01);
        pSpin->setDecimals(control.decimals);
        pSpin->setValue(control.value.toDouble());
        connect(pSpin,
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,
                [this, id](double value) {
                    emitChange(id, value);
                });
        return pSpin;
    }

    if (type == QLatin1String("slider")) {
        auto* pRow = new QWidget(this);
        auto* pRowLayout = new QHBoxLayout(pRow);
        pRowLayout->setContentsMargins(0, 0, 0, 0);

        if (!control.text.isEmpty()) {
            pRowLayout->addWidget(new QLabel(control.text, pRow));
        }
        auto* pSlider = new QSlider(Qt::Horizontal, pRow);
        pSlider->setRange(0, kSliderResolution);
        const double range = control.max - control.min;
        const double normalized = range > 0.0
                ? (control.value.toDouble() - control.min) / range
                : 0.0;
        pSlider->setValue(static_cast<int>(normalized * kSliderResolution));
        auto* pValueLabel = new QLabel(pRow);
        pValueLabel->setObjectName(QLatin1String(kValueLabelName));
        pValueLabel->setMinimumWidth(48);
        pValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pValueLabel->setText(QString::number(control.value.toDouble(), 'f', control.decimals));
        pRowLayout->addWidget(pSlider, 1);
        pRowLayout->addWidget(pValueLabel);

        const double min = control.min;
        const double max = control.max;
        const int decimals = control.decimals;
        connect(pSlider, &QSlider::valueChanged, this,
                [this, id, min, max, decimals, pValueLabel](int raw) {
                    const double value = min +
                            (max - min) * (static_cast<double>(raw) / kSliderResolution);
                    pValueLabel->setText(QString::number(value, 'f', decimals));
                    emitChange(id, value);
                });
        return pRow;
    }

    if (type == QLatin1String("text")) {
        auto* pEdit = new QLineEdit(control.value.toString(), this);
        if (!control.text.isEmpty()) {
            pEdit->setPlaceholderText(control.text);
        }
        connect(pEdit, &QLineEdit::textChanged, this, [this, id](const QString& text) {
            emitChange(id, text);
        });
        return pEdit;
    }

    if (type == QLatin1String("list")) {
        auto* pList = new QListWidget(this);
        pList->setMinimumHeight(120);
        if (control.iconWidth > 0 && control.iconHeight > 0) {
            pList->setIconSize(QSize(control.iconWidth, control.iconHeight));
        }
        const int count = control.items.size();
        for (int i = 0; i < count; ++i) {
            auto* pItem = new QListWidgetItem(control.items.at(i), pList);
            if (i < control.itemValues.size()) {
                pItem->setData(Qt::UserRole, control.itemValues.at(i));
            }
            if (i < control.itemIcons.size()) {
                applyItemIcon(pItem, control.itemIcons.at(i));
            }
        }
        connect(pList,
                &QListWidget::currentRowChanged,
                this,
                [this, id, pList](int row) {
                    const QListWidgetItem* pItem = pList->item(row);
                    if (pItem == nullptr) {
                        return;
                    }
                    const QVariant value = pItem->data(Qt::UserRole);
                    emitChange(id, value.isValid() ? value : QVariant(pItem->text()));
                });
        return pList;
    }

    if (type == QLatin1String("progress")) {
        auto* pBar = new QProgressBar(this);
        pBar->setRange(0, 100);
        pBar->setValue(static_cast<int>(control.value.toDouble()));
        return pBar;
    }

    if (type == QLatin1String("choice")) {
        auto* pCombo = new QComboBox(this);
        pCombo->addItems(control.choices);
        if (control.value.isValid()) {
            const int index = pCombo->findText(control.value.toString());
            if (index >= 0) {
                pCombo->setCurrentIndex(index);
            }
        }
        connect(pCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                [this, id, pCombo](int) {
                    emitChange(id, pCombo->currentText());
                });
        return pCombo;
    }

    // Default: static label.
    auto* pLabel = new QLabel(control.text, this);
    pLabel->setWordWrap(true);
    return pLabel;
}

void PluginPanelWidget::emitChange(const QString& id, const QVariant& value) {
    if (m_spec.onChange) {
        m_spec.onChange(id, value);
    }
}

const PluginPanelControl* PluginPanelWidget::findControl(
        const QString& controlId) const {
    for (const PluginPanelControl& control : m_spec.controls) {
        if (control.id == controlId) {
            return &control;
        }
    }
    return nullptr;
}

void PluginPanelWidget::setValue(const QString& controlId, const QVariant& value) {
    QWidget* pWidget = m_controls.value(controlId, nullptr);
    if (pWidget == nullptr) {
        return;
    }
    if (auto* pCheck = qobject_cast<QCheckBox*>(pWidget)) {
        const QSignalBlocker blocker(pCheck);
        pCheck->setChecked(value.toBool());
    } else if (auto* pSpin = qobject_cast<QDoubleSpinBox*>(pWidget)) {
        const QSignalBlocker blocker(pSpin);
        pSpin->setValue(value.toDouble());
    } else if (auto* pEdit = qobject_cast<QLineEdit*>(pWidget)) {
        const QSignalBlocker blocker(pEdit);
        pEdit->setText(value.toString());
    } else if (auto* pCombo = qobject_cast<QComboBox*>(pWidget)) {
        const QSignalBlocker blocker(pCombo);
        const int index = pCombo->findText(value.toString());
        if (index >= 0) {
            pCombo->setCurrentIndex(index);
        }
    } else if (auto* pList = qobject_cast<QListWidget*>(pWidget)) {
        const QSignalBlocker blocker(pList);
        // Prefer the per-item value (reported by onChange) over the display
        // text, so setValue() round-trips what the list emitted.
        const QString wanted = value.toString();
        for (int i = 0; i < pList->count(); ++i) {
            QListWidgetItem* pItem = pList->item(i);
            if (pItem == nullptr) {
                continue;
            }
            const QVariant itemValue = pItem->data(Qt::UserRole);
            if ((itemValue.isValid() && itemValue.toString() == wanted) ||
                    pItem->text() == wanted) {
                pList->setCurrentItem(pItem);
                break;
            }
        }
    } else if (auto* pLabel = qobject_cast<QLabel*>(pWidget)) {
        pLabel->setText(value.toString());
    } else if (auto* pBar = qobject_cast<QProgressBar*>(pWidget)) {
        pBar->setValue(value.toInt());
    } else if (auto* pSlider = pWidget->findChild<QSlider*>()) {
        const QSignalBlocker blocker(pSlider);
        // Sliders are backed by an absolute [min, max] range; undo the mapping
        // applied by the valueChanged handler above.
        const PluginPanelControl* pControl = findControl(controlId);
        const double min = pControl != nullptr ? pControl->min : 0.0;
        const double max = pControl != nullptr ? pControl->max : 1.0;
        const int decimals = pControl != nullptr ? pControl->decimals : 2;
        const double range = max - min;
        const double normalized = range > 0.0 ? (value.toDouble() - min) / range : 0.0;
        pSlider->setValue(static_cast<int>(normalized * kSliderResolution));
        if (auto* pValueLabel =
                        pWidget->findChild<QLabel*>(QLatin1String(kValueLabelName))) {
            pValueLabel->setText(QString::number(value.toDouble(), 'f', decimals));
        }
    }
}

void PluginPanelWidget::setItems(const QString& controlId, const QStringList& items) {
    setListItems(controlId, items, QVariantList());
}

void PluginPanelWidget::setListItems(const QString& controlId,
        const QStringList& texts,
        const QVariantList& values,
        const QStringList& icons) {
    auto* pList = qobject_cast<QListWidget*>(m_controls.value(controlId, nullptr));
    if (pList == nullptr) {
        return;
    }
    const QSignalBlocker blocker(pList);
    pList->clear();
    for (int i = 0; i < texts.size(); ++i) {
        auto* pItem = new QListWidgetItem(texts.at(i), pList);
        if (i < values.size()) {
            pItem->setData(Qt::UserRole, values.at(i));
        }
        if (i < icons.size()) {
            applyItemIcon(pItem, icons.at(i));
        }
    }
}

void PluginPanelWidget::setListItemIcon(
        const QString& controlId, int index, const QString& iconPath) {
    auto* pList = qobject_cast<QListWidget*>(m_controls.value(controlId, nullptr));
    if (pList == nullptr || index < 0) {
        return;
    }
    QListWidgetItem* pItem = pList->item(index);
    if (pItem == nullptr) {
        return;
    }
    pItem->setIcon(iconPath.isEmpty() ? QIcon() : QIcon(iconPath));
}

void PluginPanelWidget::setControlEnabled(const QString& controlId, bool enabled) {
    QWidget* pWidget = m_controls.value(controlId, nullptr);
    if (pWidget != nullptr) {
        pWidget->setEnabled(enabled);
    }
}

} // namespace plugins
} // namespace mixxx
