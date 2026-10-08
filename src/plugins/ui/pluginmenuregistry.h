#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QAction;
class QMenu;

namespace mixxx {
namespace plugins {

/// Description of a menu entry contributed by a plugin.
///
/// The callbacks are invoked on the GUI thread. They are owned by the registry
/// and destroyed when the item/plugin is removed, so plugins must not keep
/// references to them.
struct PluginMenuItem {
    QString pluginId;
    /// Top-level menu to attach to: "File", "View", "Library", "Options",
    /// "Help" or "Plugins" (the default).
    QString topLevelMenu;
    /// Optional path of submenus separated by '/'.
    QString subMenu;
    QString text;
    QString shortcut;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
    std::function<void()> trigger;
    std::function<void(bool)> toggle;
};

/// Central registry that allows plugins to add entries to the main menu bar.
///
/// The registry is created by PluginManager before the GUI exists. Script
/// plugins may add items during their `init()` callback, which happens before
/// or after the menu bar is constructed. `attach()` therefore materializes any
/// already-registered items and toggles live materialization afterwards.
class PluginMenuRegistry : public QObject {
    Q_OBJECT
  public:
    explicit PluginMenuRegistry(QObject* pParent = nullptr);
    ~PluginMenuRegistry() override;

    /// Binds the registry to the real menus. Passing nullptr for a menu makes
    /// items targeting it fall back to the Plugins menu.
    void attach(QMenu* pPluginsMenu,
            QMenu* pFileMenu,
            QMenu* pViewMenu,
            QMenu* pLibraryMenu,
            QMenu* pOptionsMenu,
            QMenu* pHelpMenu);

    /// Called when the menu bar owning `pPluginsMenu` is about to be destroyed.
    /// Forgets all QAction pointers without deleting them (the menus own them).
    /// Keeps the item definitions so they can be re-materialized if a new menu
    /// bar is built. A no-op if a newer menu bar has already been attached, so
    /// the deferred destruction of a replaced menu bar cannot clobber it.
    void detach(QMenu* pPluginsMenu);

    /// Adds an item and returns its unique id (used for removal).
    QString addMenuItem(PluginMenuItem item);

    void removeMenuItem(const QString& itemId);
    void removePluginItems(const QString& pluginId);

    bool isEmpty() const {
        return m_items.isEmpty();
    }

    /// The fallback Plugins menu, valid after attach().
    QMenu* pluginsMenu() const {
        return m_pPluginsMenu;
    }

  signals:
    void itemAdded(const QString& itemId);
    void itemRemoved(const QString& itemId);

  private:
    QMenu* menuForTopLevel(const QString& topLevel) const;
    void materialize(const QString& itemId);
    void dematerialize(const QString& itemId);
    /// Forgets every materialized QAction/QMenu without deleting them. The
    /// menus that own them are destroyed by whoever is rebuilding the menu bar.
    void clearMaterializedState();

    struct Entry {
        PluginMenuItem item;
        QAction* pAction = nullptr;
        QMenu* pParentMenu = nullptr;
    };

    QHash<QString, Entry> m_items;
    /// Item ids in registration order, used to materialize menu entries
    /// deterministically (QHash iteration order is unspecified).
    QList<QString> m_itemOrder;
    /// Cache of submenus keyed by "<top-level>|<submenu path>".
    QHash<QString, QMenu*> m_subMenus;
    QHash<QString, QMenu*> m_subMenuParents;
    QHash<QString, QString> m_itemSubMenuKeys;

    QMenu* m_pPluginsMenu = nullptr;
    QMenu* m_pFileMenu = nullptr;
    QMenu* m_pViewMenu = nullptr;
    QMenu* m_pLibraryMenu = nullptr;
    QMenu* m_pOptionsMenu = nullptr;
    QMenu* m_pHelpMenu = nullptr;
};

} // namespace plugins
} // namespace mixxx
