#pragma once

#include <optional>

#include "ModuleLabels.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QString>

class QGraphicsItem;
class QGraphicsScene;

namespace bld::core { class Layer; class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::rendering {

// Populates a QGraphicsScene with items for each layer of a core::Map.
// Scene units: 1 stud = 8 pixels (matches BlueBrickParts GIF sampling).
//
// Items are added directly to the scene (no QGraphicsItemGroup wrapper) so
// hit-testing / selection never gets intercepted by a parent group. We
// track per-layer items in a QHash<int, QList<QGraphicsItem*>> for
// visibility toggling.
// A painted area cell's colour: its RGB at the sheet's alpha, as vanilla
// BlueBrick and the web draw it (render-parity/areas.json).
QColor areaCellColor(const QColor& cell, int transparency);

class SceneBuilder {
public:
    static constexpr int kPixelsPerStud = 8;
    // A ruler's selectable item carries its line (QLineF, the offset line)
    // or circle (QRectF) in scene px, and its line thickness, for the
    // selection band.
    static constexpr int kRulerBandRole = 10;
    static constexpr int kRulerThicknessRole = 11;

    SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts);

    void build(const core::Map& map);
    void clear();

    // Venue label size in scene px; unset reads Preferences (venue/labelPx).
    // The Venue Designer sets it from the zoom so labels read the same at any scale.
    void setVenueLabelPx(std::optional<double> px) { venueLabelPx_ = px; }

    // Configure live drag-snap. `snapStepStuds` of 0 disables snapping.
    // Applied by the per-item ItemPositionChange override so the brick snaps
    // under the cursor during drag, not only on release.
    static void setLiveSnapStepStuds(double snapStepStuds);

    // Set to true for the span of programmatic setPos calls that should not
    // be re-snapped by the per-item grid-snap logic. MapView turns this on
    // while applying a group connection-snap shift so its shifted positions
    // survive the itemChange callback.
    static void setSuppressItemSnap(bool suppress);

    // Toggle the visibility of a layer (by index). Returns false if out of range.
    bool setLayerVisible(int layerIndex, bool visible);

    // Show or hide every anchored label (World, Brick, Group and Module
    // labels), for a saved view with its labels off.
    void setLabelsVisible(bool visible);

    // Each module's frame and name as drawn (View > Module names), in
    // scene pixels, by module id. The name usually sits outside the
    // frame. Empty when module names are off.
    const QList<QPair<QString, QRectF>>& moduleAnnotationRects() const { return moduleAnnotationRects_; }

    // Module names cut short to fit (the whole name shows on hover, as a
    // tooltip, and while the module is selected, MapView's pill).
    struct ShortenedModuleName {
        QString id;
        QString name;
        ModuleLabelLayout at;
        QColor fill;
    };
    const QList<ShortenedModuleName>& shortenedModuleNames() const { return shortenedModuleNames_; }

private:
    std::optional<double> venueLabelPx_;
    void addLayer(const core::Layer& layer, int layerIndex);
    void addVenue(const core::Map& map);
    void addAnchoredLabels(const core::Map& map);
    void addModuleLabels(const core::Map& map);
    void addElectricCircuits(const core::Map& map);

    QGraphicsScene& scene_;
    parts::PartsLibrary& parts_;
    QHash<int, QList<QGraphicsItem*>> itemsByLayer_;  // layer index -> direct scene items
    QHash<QString, QGraphicsItem*>    brickByGuid_;   // brick.guid -> QGraphicsItem*
    // brick.guid -> brick world centre in STUDS. Precomputed once per build
    // so ruler rendering (and any other cross-item feature) can resolve
    // attached-brick endpoints without walking the whole map.
    QHash<QString, QPointF>           brickCentreByGuid_;
    QList<QGraphicsItem*>             venueItems_;
    // Transient items for the Electric Circuits render overlay. Cleared +
    // rebuilt on every build().
    QList<QGraphicsItem*>             electricItems_;
    QList<QGraphicsItem*>             worldLabelItems_;
    QList<QGraphicsItem*>             moduleLabelItems_;
    QList<QPair<QString, QRectF>>     moduleAnnotationRects_;
    QList<ShortenedModuleName>        shortenedModuleNames_;
};

}
