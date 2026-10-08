#include <gtest/gtest.h>

#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QSize>
#include <QTemporaryDir>

#include "plugins/ui/pluginpanel.h"

namespace mixxx {
namespace plugins {

namespace {

PluginPanelControl makeControl(const QString& id, const QString& type) {
    PluginPanelControl control;
    control.id = id;
    control.type = type;
    return control;
}

} // namespace

TEST(PluginPanelWidgetTest, SetValueUpdatesLabelAndProgress) {
    PluginPanelSpec spec;
    PluginPanelControl label = makeControl(
            QStringLiteral("status"), QStringLiteral("label"));
    label.text = QStringLiteral("Idle");
    PluginPanelControl progress = makeControl(
            QStringLiteral("progress"), QStringLiteral("progress"));
    spec.controls << label << progress;

    PluginPanelWidget widget(spec);
    widget.setValue(QStringLiteral("status"), QStringLiteral("Downloading"));
    widget.setValue(QStringLiteral("progress"), 42);

    auto* pLabel = widget.findChild<QLabel*>();
    ASSERT_NE(pLabel, nullptr);
    EXPECT_EQ(pLabel->text(), QStringLiteral("Downloading"));
    auto* pBar = widget.findChild<QProgressBar*>();
    ASSERT_NE(pBar, nullptr);
    EXPECT_EQ(pBar->value(), 42);
}

// setValue() is documented to update a control without invoking onChange.
TEST(PluginPanelWidgetTest, SetValueDoesNotEmitChange) {
    PluginPanelSpec spec;
    spec.controls << makeControl(
            QStringLiteral("progress"), QStringLiteral("progress"));

    bool fired = false;
    spec.onChange = [&fired](const QString&, const QVariant&) { fired = true; };

    PluginPanelWidget widget(spec);
    widget.setValue(QStringLiteral("progress"), 42);

    EXPECT_FALSE(fired);
}

TEST(PluginPanelWidgetTest, ButtonClickEmitsChangeWithInvalidValue) {
    PluginPanelSpec spec;
    PluginPanelControl button = makeControl(
            QStringLiteral("go"), QStringLiteral("button"));
    button.text = QStringLiteral("Go");
    spec.controls << button;

    QString emittedId;
    QVariant emittedValue{QStringLiteral("sentinel")};
    bool fired = false;
    spec.onChange = [&emittedId, &emittedValue, &fired](
                            const QString& id, const QVariant& value) {
        emittedId = id;
        emittedValue = value;
        fired = true;
    };

    PluginPanelWidget widget(spec);
    auto* pButton = widget.findChild<QPushButton*>();
    ASSERT_NE(pButton, nullptr);
    pButton->click();

    EXPECT_TRUE(fired);
    EXPECT_EQ(emittedId, QStringLiteral("go"));
    EXPECT_FALSE(emittedValue.isValid());
}

TEST(PluginPanelWidgetTest, ListReportsAssociatedValue) {
    PluginPanelSpec spec;
    spec.controls << makeControl(QStringLiteral("results"), QStringLiteral("list"));

    QString emitted;
    bool fired = false;
    spec.onChange = [&emitted, &fired](const QString&, const QVariant& value) {
        emitted = value.toString();
        fired = true;
    };

    PluginPanelWidget widget(spec);
    widget.setListItems(QStringLiteral("results"),
            {QStringLiteral("A"), QStringLiteral("B")},
            {QVariant(QStringLiteral("id-a")), QVariant(QStringLiteral("id-b"))});

    auto* pList = widget.findChild<QListWidget*>();
    ASSERT_NE(pList, nullptr);
    pList->setCurrentRow(1);
    EXPECT_TRUE(fired);
    EXPECT_EQ(emitted, QStringLiteral("id-b"));
}

TEST(PluginPanelWidgetTest, ListFallsBackToTextWithoutValue) {
    PluginPanelSpec spec;
    spec.controls << makeControl(QStringLiteral("results"), QStringLiteral("list"));

    QString emitted;
    spec.onChange = [&emitted](const QString&, const QVariant& value) {
        emitted = value.toString();
    };

    PluginPanelWidget widget(spec);
    widget.setItems(QStringLiteral("results"), {QStringLiteral("plain")});
    auto* pList = widget.findChild<QListWidget*>();
    ASSERT_NE(pList, nullptr);
    pList->setCurrentRow(0);
    EXPECT_EQ(emitted, QStringLiteral("plain"));
}

TEST(PluginPanelWidgetTest, ListAppliesIconsAndSize) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString iconPath = dir.filePath(QStringLiteral("thumb.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(iconPath));

    PluginPanelSpec spec;
    PluginPanelControl list = makeControl(
            QStringLiteral("results"), QStringLiteral("list"));
    list.iconWidth = 64;
    list.iconHeight = 36;
    spec.controls << list;

    PluginPanelWidget widget(spec);
    widget.setListItems(QStringLiteral("results"),
            {QStringLiteral("A"), QStringLiteral("B")},
            {QVariant(QStringLiteral("id-a")), QVariant(QStringLiteral("id-b"))},
            {iconPath, QString()});

    auto* pList = widget.findChild<QListWidget*>();
    ASSERT_NE(pList, nullptr);
    EXPECT_EQ(pList->iconSize(), QSize(64, 36));
    ASSERT_NE(pList->item(0), nullptr);
    EXPECT_FALSE(pList->item(0)->icon().isNull());
    ASSERT_NE(pList->item(1), nullptr);
    EXPECT_TRUE(pList->item(1)->icon().isNull());
}

TEST(PluginPanelWidgetTest, SetListItemIconUpdatesOneRow) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString iconPath = dir.filePath(QStringLiteral("row.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    ASSERT_TRUE(image.save(iconPath));

    PluginPanelSpec spec;
    spec.controls << makeControl(QStringLiteral("results"), QStringLiteral("list"));

    PluginPanelWidget widget(spec);
    widget.setItems(QStringLiteral("results"),
            {QStringLiteral("A"), QStringLiteral("B")});
    auto* pList = widget.findChild<QListWidget*>();
    ASSERT_NE(pList, nullptr);
    pList->setCurrentRow(1);

    widget.setListItemIcon(QStringLiteral("results"), 0, iconPath);
    ASSERT_NE(pList->item(0), nullptr);
    EXPECT_FALSE(pList->item(0)->icon().isNull());
    EXPECT_TRUE(pList->item(1)->icon().isNull());
    EXPECT_EQ(pList->currentRow(), 1);
}

// An empty path clears the row's icon without disturbing the selection.
TEST(PluginPanelWidgetTest, SetListItemIconClearsWithEmptyPath) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString iconPath = dir.filePath(QStringLiteral("row.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::green);
    ASSERT_TRUE(image.save(iconPath));

    PluginPanelSpec spec;
    spec.controls << makeControl(QStringLiteral("results"), QStringLiteral("list"));

    PluginPanelWidget widget(spec);
    widget.setListItems(QStringLiteral("results"),
            {QStringLiteral("A")},
            {QVariant(QStringLiteral("id-a"))},
            {iconPath});
    auto* pList = widget.findChild<QListWidget*>();
    ASSERT_NE(pList, nullptr);
    ASSERT_NE(pList->item(0), nullptr);
    ASSERT_FALSE(pList->item(0)->icon().isNull());

    widget.setListItemIcon(QStringLiteral("results"), 0, QString());

    EXPECT_TRUE(pList->item(0)->icon().isNull());
}

TEST(PluginPanelWidgetTest, SetControlEnabled) {
    PluginPanelSpec spec;
    PluginPanelControl button = makeControl(
            QStringLiteral("go"), QStringLiteral("button"));
    button.text = QStringLiteral("Go");
    spec.controls << button;

    PluginPanelWidget widget(spec);
    auto* pButton = widget.findChild<QPushButton*>();
    ASSERT_NE(pButton, nullptr);
    EXPECT_TRUE(pButton->isEnabled());
    widget.setControlEnabled(QStringLiteral("go"), false);
    EXPECT_FALSE(pButton->isEnabled());
}

} // namespace plugins
} // namespace mixxx
