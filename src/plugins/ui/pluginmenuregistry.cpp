#include "plugins/ui/pluginmenuregistry.h"

#include <utility>

#include <QAction>
#include <QMenu>
#include <QKeySequence>

#include "moc_pluginmenuregistry.cpp"

namespace mixxx {
namespace plugins {

namespace {

/// Maps a plugin-supplied top-level menu name to a canonical key. Empty names,
/// unknown names and "plugins" all select the fallback Plugins menu.
QString normalizedTopLevel(const QString& topLevel) {
    const QString key = topLevel.trimmed().toLower();
    if (key == QLatin1String("file") || key == QLatin1String("view") ||
            key == QLatin1String("library") || key == QLatin1String("options") ||
            key == QLatin1String("help")) {
        return key;
    }
    return QStringLiteral("plugins");
}

} // namespace

PluginMenuRegistry::PluginMenuRegistry(QObject* pParent)
        : QObject(pParent) {
}

PluginMenuRegistry::~PluginMenuRegistry() {
    // The QActions are children of the menus, which outlive the registry, so
    // there is nothing to delete here explicitly.
}

void PluginMenuRegistry::attach(QMenu* pPluginsMenu,
        QMenu* pFileMenu,
        QMenu* pViewMenu,
        QMenu* pLibraryMenu,
        QMenu* pOptionsMenu,
        QMenu* pHelpMenu) {
    // A previous menu bar may still be alive: QMainWindow defers the deletion
    // of a replaced menu bar, so attach() can run before the old bar's
    // destructor calls detach(). Forget the pointers owned by the old bar
    // (its menus will delete the actions) and rebuild against the new menus.
    clearMaterializedState();

    m_pPluginsMenu = pPluginsMenu;
    m_pFileMenu = pFileMenu;
    m_pViewMenu = pViewMenu;
    m_pLibraryMenu = pLibraryMenu;
    m_pOptionsMenu = pOptionsMenu;
    m_pHelpMenu = pHelpMenu;

    // Materialize everything that was registered before attach() in a stable
    // order.
    for (const QString& itemId : std::as_const(m_itemOrder)) {
        materialize(itemId);
    }
}

void PluginMenuRegistry::detach(QMenu* pPluginsMenu) {
    if (m_pPluginsMenu != pPluginsMenu) {
        // A newer menu bar has already attached; do not forget its actions.
        return;
    }
    clearMaterializedState();
    m_pPluginsMenu = nullptr;
    m_pFileMenu = nullptr;
    m_pViewMenu = nullptr;
    m_pLibraryMenu = nullptr;
    m_pOptionsMenu = nullptr;
    m_pHelpMenu = nullptr;
}

QString PluginMenuRegistry::addMenuItem(PluginMenuItem item) {
    static quint64 s_counter = 0;
    const QString itemId = QStringLiteral("plugin-menu-%1").arg(++s_counter);

    m_items.insert(itemId, Entry{std::move(item), nullptr});
    m_itemOrder.append(itemId);
    // materialize() is a no-op while no menu bar is attached.
    materialize(itemId);
    emit itemAdded(itemId);
    return itemId;
}

void PluginMenuRegistry::removeMenuItem(const QString& itemId) {
    if (!m_items.contains(itemId)) {
        return;
    }
    dematerialize(itemId);
    m_itemSubMenuKeys.remove(itemId);
    m_items.remove(itemId);
    m_itemOrder.removeAll(itemId);
    emit itemRemoved(itemId);
}

void PluginMenuRegistry::removePluginItems(const QString& pluginId) {
    const QStringList ids = m_items.keys();
    for (const QString& itemId : ids) {
        if (m_items.value(itemId).item.pluginId == pluginId) {
            removeMenuItem(itemId);
        }
    }
}

QMenu* PluginMenuRegistry::menuForTopLevel(const QString& topLevel) const {
    const QString key = normalizedTopLevel(topLevel);
    if (key == QLatin1String("plugins")) {
        return m_pPluginsMenu;
    }

    QMenu* pRequestedMenu = nullptr;
    if (key == QLatin1String("file")) {
        pRequestedMenu = m_pFileMenu;
    } else if (key == QLatin1String("view")) {
        pRequestedMenu = m_pViewMenu;
    } else if (key == QLatin1String("library")) {
        pRequestedMenu = m_pLibraryMenu;
    } else if (key == QLatin1String("options")) {
        pRequestedMenu = m_pOptionsMenu;
    } else if (key == QLatin1String("help")) {
        pRequestedMenu = m_pHelpMenu;
    }

    // Fall back to the Plugins menu when the requested one is not attached, so
    // an item is never silently dropped.
    return pRequestedMenu != nullptr ? pRequestedMenu : m_pPluginsMenu;
}

void PluginMenuRegistry::clearMaterializedState() {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        it->pAction = nullptr;
        it->pParentMenu = nullptr;
    }
    m_subMenus.clear();
    m_subMenuParents.clear();
    m_itemSubMenuKeys.clear();
}

void PluginMenuRegistry::materialize(const QString& itemId) {
    auto it = m_items.find(itemId);
    if (it == m_items.end() || it->pAction != nullptr) {
        return;
    }

    PluginMenuItem& item = it->item;
    QMenu* pTopLevel = menuForTopLevel(item.topLevelMenu);
    if (pTopLevel == nullptr) {
        // Menus not attached yet; attach() will materialize later.
        return;
    }

    // Resolve/create the (optional) submenu chain. The key is normalized so
    // items that name the same top-level menu differently (e.g. omitted vs.
    // "Plugins", or different casing) share one submenu.
    QMenu* pParentMenu = pTopLevel;
    const QString subMenuKey =
            normalizedTopLevel(item.topLevelMenu) + QLatin1Char('|') + item.subMenu;
    m_itemSubMenuKeys.insert(itemId, subMenuKey);
    if (!item.subMenu.isEmpty()) {
        QMenu*& pSubMenu = m_subMenus[subMenuKey];
        if (pSubMenu == nullptr) {
            pSubMenu = pTopLevel->addMenu(item.subMenu);
            m_subMenuParents.insert(subMenuKey, pTopLevel);
        }
        pParentMenu = pSubMenu;
    }
    it->pParentMenu = pParentMenu;

    QAction* pAction = new QAction(item.text, pParentMenu);
    pAction->setEnabled(item.enabled);
    if (!item.shortcut.isEmpty()) {
        pAction->setShortcut(QKeySequence(item.shortcut));
        pAction->setShortcutContext(Qt::ApplicationShortcut);
    }
    if (item.checkable) {
        pAction->setCheckable(true);
        pAction->setChecked(item.checked);
        const std::function<void(bool)> toggle = item.toggle;
        if (toggle) {
            connect(pAction, &QAction::toggled, pParentMenu, [toggle](bool checked) {
                toggle(checked);
            });
        }
    }
    // `triggered` is also emitted for checkable items; forward it so a script
    // can observe clicks even when it does not track state.
    const std::function<void()> trigger = item.trigger;
    if (trigger) {
        connect(pAction, &QAction::triggered, pParentMenu, [trigger](bool) {
            trigger();
        });
    }

    pParentMenu->addAction(pAction);
    it->pAction = pAction;
}

void PluginMenuRegistry::dematerialize(const QString& itemId) {
    auto it = m_items.find(itemId);
    if (it == m_items.end() || it->pAction == nullptr) {
        return;
    }

    QAction* pAction = it->pAction;
    QMenu* pParentMenu = it->pParentMenu;
    if (pParentMenu != nullptr) {
        pParentMenu->removeAction(pAction);
    }
    delete pAction;
    it->pAction = nullptr;
    it->pParentMenu = nullptr;

    // Clean up an empty submenu.
    const QString subMenuKey = m_itemSubMenuKeys.value(itemId);
    if (!subMenuKey.isEmpty()) {
        QMenu* pSubMenu = m_subMenus.value(subMenuKey, nullptr);
        if (pSubMenu != nullptr && pSubMenu->actions().isEmpty()) {
            QMenu* pTopLevel = m_subMenuParents.value(subMenuKey, nullptr);
            if (pTopLevel != nullptr) {
                pTopLevel->removeAction(pSubMenu->menuAction());
            }
            m_subMenus.remove(subMenuKey);
            m_subMenuParents.remove(subMenuKey);
            pSubMenu->deleteLater();
        }
    }
}

} // namespace plugins
} // namespace mixxx
