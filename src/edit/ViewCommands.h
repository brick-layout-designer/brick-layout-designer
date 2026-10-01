#pragma once

#include "../core/SavedView.h"

#include <QUndoCommand>

#include <vector>

namespace bld::core { class Map; }

namespace bld::edit {

// Replaces the layout's saved views (add, rename, change or delete one) in
// one undoable step. Views are sidecar data, so the scene doesn't change;
// the step marks the layout changed and, live, sends the new list.
class SetViewsCommand : public QUndoCommand {
public:
    SetViewsCommand(core::Map& map, std::vector<core::SavedView> next, const QString& text,
                    QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<core::SavedView> before_;
    std::vector<core::SavedView> after_;
};

}  // namespace bld::edit
