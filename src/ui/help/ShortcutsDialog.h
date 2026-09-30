#pragma once

// Help › Keyboard shortcuts: every shortcut the menus have, grouped by
// menu and read from the actions themselves (so the list never goes out
// of date), plus the keys the map handles on its own.

#include <QDialog>
#include <QList>
#include <QString>

class QMenuBar;

namespace bld::ui::help {

struct Shortcut {
    QString keys;  // as shown, e.g. "Ctrl+S" ("⌘S" on a Mac)
    QString does;  // the menu item, without its & and "..."
};

struct ShortcutGroup {
    QString title;
    QList<Shortcut> items;
};

// The menus' shortcuts, one group per menu (sub-menus fold into their
// menu), in menu order; menus without any are left out. Then the map's
// own keys.
QList<ShortcutGroup> collectShortcuts(const QMenuBar* menuBar);

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(const QList<ShortcutGroup>& groups, QWidget* parent = nullptr);
};

}  // namespace bld::ui::help
