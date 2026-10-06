#pragma once

#include "../core/Layer.h"  // for core::LayerKind

#include <QDockWidget>

class QListWidget;

namespace bld::core { class Map; }
namespace bld::rendering { class SceneBuilder; }

namespace bld::ui {

// Dockable panel listing the current map's layers, with a per-layer visibility
// checkbox that forwards to the SceneBuilder. Also emits signals for layer
// lifecycle operations so MainWindow can route them through the undo stack.
//
// Upstream-parity extras:
//   - Clicking a row selects it as the "active" layer — new bricks / text /
//     rulers added via MapView land on this layer, matching BlueBrick's
//     selectedLayerIndex semantics.
//   - Each row shows the sheet's name; an icon shows its kind, and the
//     tooltip has the kind, its number and its transparency.
//   - Layer Options... (double-click or context menu) opens a property
//     editor that edits name + transparency in-place. (Per-type extras
//     like grid size / area cell size land next.)
class LayerPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void setMap(core::Map* map, rendering::SceneBuilder* builder);

    // Currently-selected layer row, or -1 if none.
    int currentRow() const;

    // The kind in plain words, e.g. "Parts sheet" for a brick layer.
    static QString friendlyKindName(core::LayerKind kind);

signals:
    // MainWindow handles these by pushing the corresponding undo commands.
    void addLayerRequested(core::LayerKind kind);
    void deleteLayerRequested(int index);
    void moveLayerRequested(int index, int delta);
    void renameLayerRequested(int index, const QString& newName);
    void activeLayerChanged(int index);
    void layerOptionsRequested(int index);
    // A sheet was shown or hidden from its checkbox (Show all, Solo too).
    void layerVisibilityChanged(int index, bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    QListWidget* list_ = nullptr;
    rendering::SceneBuilder* builder_ = nullptr;
    core::Map* map_ = nullptr;
};

}
