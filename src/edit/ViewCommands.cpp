#include "ViewCommands.h"

#include "../core/Map.h"

namespace bld::edit {

SetViewsCommand::SetViewsCommand(core::Map& map, std::vector<core::SavedView> next, const QString& text,
                                 QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), before_(map.sidecar.views), after_(std::move(next)) {
    setText(text);
}

void SetViewsCommand::redo() { map_.sidecar.views = after_; }
void SetViewsCommand::undo() { map_.sidecar.views = before_; }

}  // namespace bld::edit
