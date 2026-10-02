// MapView clipboard operations (copy / cut / paste / duplicate) split out
// of MapView.cpp so the main file can focus on construction and event
// handlers. These all touch MapView's private state via the class
// declaration in MapView.h.

#include "MapView.h"
#include "BudgetSession.h"

#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../rendering/SceneBuilder.h"
#include "MapViewInternal.h"

#include <QCursor>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUndoStack>

namespace bld::ui {

using detail::kBrickDataGuid;
using detail::isBrickItem;

void MapView::copySelection() {
    clipboard_.clear();
    if (!map_) return;
    // selectedItems() returns items in no particular order. Copying in
    // that order would scramble back-to-front z-ordering within the
    // source layer on paste. Collect the selected guids, then walk each
    // brick layer's vector in order so within-layer z-order (earlier in
    // the vector = further back) survives copy-paste.
    QSet<QString> selectedGuids;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isBrickItem(it)) selectedGuids.insert(it->data(kBrickDataGuid).toString());
    }
    if (selectedGuids.isEmpty()) return;
    for (const auto& L : map_->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            if (selectedGuids.contains(b.guid)) {
                clipboard_.push_back({ L->name, b });
            }
        }
    }
}

void MapView::cutSelection() {
    copySelection();
    deleteSelected();
}

void MapView::pasteClipboard() {
    if (!map_ || clipboard_.empty()) return;

    // Compute the clipboard group's own centre across every entry regardless
    // of source layer, so the paste group stays rigid.
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    QPoint viewPos = viewport()->mapFromGlobal(QCursor::pos());
    QPointF targetSceneCentrePx = viewport()->rect().contains(viewPos)
        ? mapToScene(viewPos)
        : mapToScene(viewport()->rect().center());
    const QPointF targetCentreStuds(targetSceneCentrePx.x() / pxPerStud,
                                    targetSceneCentrePx.y() / pxPerStud);
    QPointF srcCentre;
    for (const auto& src : clipboard_) srcCentre += src.brick.displayArea.center();
    srcCentre /= clipboard_.size();
    const QPointF translation = targetCentreStuds - srcCentre;

    // Group clipboard entries by source layer name so each group lands on
    // a matching layer in the current map (creating one if none exists).
    // Preserves the original layering — a multi-layer copy pastes back as
    // a multi-layer set.
    QHash<QString, std::vector<core::Brick>> byLayer;
    QStringList layerOrder;
    QSet<QString> newGuids;
    // With Use Budget Limitation, bricks beyond their budget are left out
    // (as BlueBrick's duplicate does).
    const bool limited = budget_ && budget_->exists() && budget_->useBudgetLimitation();
    auto usage = limited ? edit::countPartUsage(*map_) : QHash<QString, int>{};
    int refused = 0;
    for (const auto& src : clipboard_) {
        if (limited) {
            const QString id = src.brick.partNumber.toUpper();
            const int limit = budget_->limit(src.brick.partNumber);
            if (limit >= 0 && usage.value(id, 0) + 1 > limit) { ++refused; continue; }
            ++usage[id];
        }
        core::Brick b = src.brick;
        b.guid = core::newBbmId();
        newGuids.insert(b.guid);
        b.displayArea.translate(translation);
        b.myGroupId.clear();
        const QString key = src.sourceLayerName.isEmpty()
            ? QStringLiteral("Bricks") : src.sourceLayerName;
        if (!byLayer.contains(key)) layerOrder << key;
        byLayer[key].push_back(std::move(b));
    }

    auto findOrCreateLayer = [this](const QString& name) -> int {
        for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
            auto* L = map_->layers()[i].get();
            if (L && L->kind() == core::LayerKind::Brick && L->name == name) return i;
        }
        auto L = std::make_unique<core::LayerBrick>();
        L->guid = core::newBbmId();
        L->name = name.isEmpty() ? QStringLiteral("Bricks") : name;
        const int idx = static_cast<int>(map_->layers().size());
        map_->layers().push_back(std::move(L));
        return idx;
    };

    if (refused > 0) reportBudgetRefusal();
    if (layerOrder.isEmpty()) return;

    undoStack_->beginMacro(tr("Paste (%1 parts across %2 sheet(s))")
                               .arg(clipboard_.size()).arg(layerOrder.size()));
    for (const QString& name : layerOrder) {
        const int li = findOrCreateLayer(name);
        if (li < 0) continue;
        auto& bricks = byLayer[name];
        undoStack_->push(new edit::AddBricksCommand(*map_, li, std::move(bricks)));
    }
    undoStack_->endMacro();

    // endMacro fires indexChanged → scene rebuild → selection restore by
    // guid. Explicitly re-select only the pasted bricks so the user's
    // "pasted bricks are active" expectation survives any future restore.
    emit layersChanged();
    scene()->clearSelection();
    for (QGraphicsItem* it : scene()->items()) {
        if (!isBrickItem(it)) continue;
        if (newGuids.contains(it->data(kBrickDataGuid).toString())) {
            it->setSelected(true);
        }
    }
}

void MapView::duplicateSelection() {
    copySelection();
    pasteClipboard();
}

}  // namespace bld::ui
