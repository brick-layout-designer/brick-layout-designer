// Electric-circuits overlay, toggled by View > Electric Circuits (QSettings
// "view/electricCircuits"). BlueBrick's model (MapData/LayerBrick.cs draw,
// MapData/Tools/ElectricCircuitChecker.cs):
//
//  - A part's circuits join pairs of connections whose electricPlug values
//    are opposite (+1/-1...). Each is drawn as two rails 2.5 studs either
//    side of the circuit's path, 0.5 stud wide, at the layer's opacity.
//    The path follows the track (CircuitPath.h) instead of BlueBrick's
//    straight chord.
//  - Polarity is propagated through linked connections, per layer, in
//    brick order (ElectricCircuitChecker.check(layer), as after loading a
//    file). The first rail is OrangeRed, the second Cyan, swapped when the
//    circuit's first connection has negative polarity.
//  - A circuit cutter (862AC01/02) breaks the second rail in the middle,
//    with two orange bars.
//  - A short circuit (the same polarity met from both sides) gets an
//    orange mark at the connection.

#include "SceneBuilder.h"
#include "SceneBuilderInternal.h"
#include "CircuitPath.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QBrush>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QHash>
#include <QPainterPath>
#include <QPen>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

namespace bld::rendering {

using detail::LayerSink;

namespace {

struct Entry {
    const core::Brick* brick = nullptr;
    parts::PartMetadata meta;
    QPointF centre;               // sprite centre, studs
    std::vector<short> polarity;  // per connection
    std::vector<bool> shortcut;
};

// ElectricCircuitChecker.check(LayerBrick): one time stamp for the layer,
// bricks explored in order from each one not reached yet.
void checkPolarity(std::vector<Entry>& entries) {
    QHash<QString, std::pair<int, int>> owner;  // connection guid -> (entry, index)
    for (int e = 0; e < static_cast<int>(entries.size()); ++e) {
        const auto& conns = entries[e].brick->connections;
        for (int i = 0; i < static_cast<int>(conns.size()) && i < static_cast<int>(entries[e].polarity.size()); ++i)
            if (!conns[i].guid.isEmpty()) owner.insert(conns[i].guid, { e, i });
    }
    const auto link = [&](int e, int i) -> std::pair<int, int> {
        const auto& conns = entries[e].brick->connections;
        if (i >= static_cast<int>(conns.size()) || conns[i].linkedToId.isEmpty()) return { -1, -1 };
        return owner.value(conns[i].linkedToId, { -1, -1 });
    };
    const short stamp = 2;
    for (int startEntry = 0; startEntry < static_cast<int>(entries.size()); ++startEntry) {
        if (std::abs(entries[startEntry].polarity[0]) == stamp) continue;
        std::vector<std::pair<int, int>> shortcuts;
        std::deque<int> explore{ startEntry };
        const int first = entries[startEntry].meta.electricCircuits.front().index1;
        entries[startEntry].polarity[first] = stamp;
        if (const auto l = link(startEntry, first); l.first >= 0) {
            entries[l.first].polarity[l.second] = static_cast<short>(-stamp);
            explore.push_back(l.first);
        }
        while (!explore.empty()) {
            const int e = explore.front();
            explore.pop_front();
            bool reexplore = false;
            for (const auto& circuit : entries[e].meta.electricCircuits) {
                auto& pol = entries[e].polarity;
                int start = circuit.index1, end = circuit.index2;
                if (std::abs(pol[end]) == stamp) std::swap(start, end);
                if (std::abs(pol[start]) != stamp) {
                    reexplore = true;
                    continue;
                }
                if (pol[end] == pol[start]) {
                    shortcuts.push_back({ e, start });
                } else if (pol[end] != -pol[start]) {
                    pol[end] = static_cast<short>(-pol[start]);
                    if (reexplore) {
                        explore.push_front(e);
                        reexplore = false;
                    }
                    if (const auto l = link(e, end); l.first >= 0) {
                        short& other = entries[l.first].polarity[l.second];
                        if (other == pol[end]) {
                            shortcuts.push_back({ e, end });
                        } else if (other != -pol[end]) {
                            other = static_cast<short>(-pol[end]);
                            explore.push_back(l.first);
                        }
                    }
                }
            }
        }
        for (const auto& [e, i] : shortcuts) entries[e].shortcut[i] = true;
    }
}

QPainterPath polyline(const std::vector<QPointF>& pts, double px) {
    QPainterPath path;
    if (pts.empty()) return path;
    path.moveTo(pts.front() * px);
    for (size_t i = 1; i < pts.size(); ++i) path.lineTo(pts[i] * px);
    return path;
}

bool isCircuitCutter(const QString& part) {
    return part.compare(QLatin1String("862AC01.7"), Qt::CaseInsensitive) == 0
        || part.compare(QLatin1String("862AC02.7"), Qt::CaseInsensitive) == 0;
}

}  // namespace

void SceneBuilder::addElectricCircuits(const core::Map& map) {
    if (!QSettings().value(QStringLiteral("view/electricCircuits"), false).toBool()) return;
    const double px = kPixelsPerStud;
    LayerSink sink{ scene_, electricItems_, 5e5, true };

    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick || !layerPtr->visible) continue;
        const auto& layer = static_cast<const core::LayerBrick&>(*layerPtr);
        std::vector<Entry> entries;
        for (const auto& b : layer.bricks) {
            const auto meta = parts_.metadata(b.partNumber);
            if (!meta || meta->electricCircuits.isEmpty()) continue;
            Entry e;
            e.brick = &b;
            e.meta = *meta;
            e.centre = parts::placement::imageCentre(b, parts_);
            e.polarity.assign(meta->connections.size(), 0);
            e.shortcut.assign(meta->connections.size(), false);
            entries.push_back(std::move(e));
        }
        if (entries.empty()) continue;
        checkPolarity(entries);

        const int alpha = std::clamp(255 * layer.transparency / 100, 0, 255);
        const auto pen = [&](QColor c, double widthStuds) {
            c.setAlpha(alpha);
            QPen p(c, widthStuds * px);
            p.setCapStyle(Qt::FlatCap);
            p.setJoinStyle(Qt::RoundJoin);
            return p;
        };
        const QPen red = pen(QColor(255, 69, 0), kCircuitPenWidth);       // OrangeRed
        const QPen blue = pen(QColor(0, 255, 255), kCircuitPenWidth);     // Cyan
        const QPen orange = pen(QColor(255, 165, 0), kCircuitCutterPen);  // Orange
        const auto add = [&](const QPainterPath& path, const QPen& p, double z) {
            auto* item = new QGraphicsPathItem(path);
            item->setPen(p);
            item->setBrush(Qt::NoBrush);
            item->setZValue(z);
            sink.add(item);
        };

        for (const Entry& e : entries) {
            const double orientation = e.brick->orientation;
            const auto world = [&](int i) {
                return e.centre + parts::placement::rotated(e.meta.connections[i].position, orientation);
            };
            for (const auto& circuit : e.meta.electricCircuits) {
                const auto path = circuitPath(world(circuit.index1), orientation + e.meta.connections[circuit.index1].angleDegrees,
                                              world(circuit.index2), orientation + e.meta.connections[circuit.index2].angleDegrees);
                if (path.size() < 2) continue;
                const bool swapped = e.polarity[circuit.index1] < 0;
                add(polyline(offsetPath(path, kCircuitRailOffset), px), swapped ? blue : red, 500);
                const QPen& second = swapped ? red : blue;
                const double length = path.back().s;
                if (isCircuitCutter(e.brick->partNumber) && length > 2.0 * kCircuitCutterGap) {
                    const double g = kCircuitCutterGap;
                    add(polyline(offsetPathBetween(path, -kCircuitRailOffset, 0.0, g), px), second, 500);
                    add(polyline(offsetPathBetween(path, -kCircuitRailOffset, length - g, length), px), second, 500);
                    for (double s : { g, length - g }) {
                        const CircuitPathPoint at = pointAt(path, s);
                        // From the centreline across the cut rail (BlueBrick: middle ± normal).
                        add(polyline({ at.p, at.p - at.normal * (2.0 * kCircuitRailOffset) }, px), orange, 501);
                    }
                } else {
                    add(polyline(offsetPath(path, -kCircuitRailOffset), px), second, 500);
                }
            }
        }

        // The shortcut sign: BlueBrick's four points, at a shorted connection
        // of each circuit (its first connection if both are).
        const double w = kCircuitShortcutSize;
        for (const Entry& e : entries) {
            for (const auto& circuit : e.meta.electricCircuits) {
                int index = -1;
                if (e.shortcut[circuit.index1]) index = circuit.index1;
                else if (e.shortcut[circuit.index2]) index = circuit.index2;
                if (index < 0) continue;
                const QPointF c = e.centre + parts::placement::rotated(e.meta.connections[index].position, e.brick->orientation);
                add(polyline({ c + QPointF(-w, 0), c + QPointF(0, -w), c + QPointF(w, 0), c + QPointF(0, w), c + QPointF(-w, 0) }, px),
                    orange, 502);
            }
        }
    }
}

}  // namespace bld::rendering
