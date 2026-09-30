#pragma once

// The compare window for reconnecting with offline edits (DESKTOP-LIVE-SYNC
// "Offline edits and reconnecting"): what you changed offline, beside what
// the server did, and for each change that clashes Keep mine, Keep server
// or Keep both. The result is applied as an ordinary live edit.

#include "LayoutMerge.h"

#include <QDialog>
#include <QHash>
#include <QList>

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace bld::sync {

class CompareDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action {
        Later,          // closed: the offline edits keep waiting
        Apply,          // merge with choices()
        Discard,        // throw my offline edits away
        ReplaceServer,  // put my version in place of the server's
        SaveAsNew,      // publish my version as a new layout; the server's stays
    };

    explicit CompareDialog(const QList<merge::ItemChange>& changes, QWidget* parent = nullptr);

    Action action() const { return action_; }
    // For each change of mine: what to keep (by key).
    QHash<QString, merge::Choice> choices() const;

    // Rows, for tests: one per change of mine, and the server's own after.
    QTreeWidget* list() const { return list_; }
    // Set every row that has the choice to `choice` (All mine / All server).
    void chooseAll(merge::Choice choice);
    void choose(const QString& key, merge::Choice choice);
    // Clicks a button as the user would (confirming what asks).
    void finish(Action action);

signals:
    // A row was picked: show the item on the map.
    void highlight(const bld::sync::merge::ItemChange& change);

private:
    QList<merge::ItemChange> changes_;
    QTreeWidget* list_ = nullptr;
    QHash<QString, QComboBox*> combos_;
    Action action_ = Action::Later;
};

}  // namespace bld::sync
