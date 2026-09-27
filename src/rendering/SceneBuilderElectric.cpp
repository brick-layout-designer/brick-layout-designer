// Electric-circuits render overlay. Toggled via View > Electric Circuits
// (QSettings key "view/electricCircuits"). Matches BlueBrick's visual model:
//
//  - Each part with electricPlug pairs (+1/-1) defines one or more in-part
//    "circuits". We draw those as two parallel lines between the pair's
//    connection points: one OrangeRed (rail +1 side) and one Cyan (rail -1
//    side), each offset 0.5 studs perpendicularly.
//
//  - Polarity is propagated across connected bricks via BFS so the red/blue
//    assignment stays consistent across an entire wired run of track.
//
//  - Short circuits (same polarity appears on both ends of a circuit after BFS)
//    are flagged with an orange diamond at the offending connection point.
//
//  - Line width scales with kPixelsPerStud (0.5 studs wide, same as BlueBrick).

#include "SceneBuilder.h"
#include "SceneBuilderInternal.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QBrush>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QPen>
#include <QSettings>

#include <cmath>
#include <unordered_map>

namespace bld::rendering {

using detail::LayerSink;

namespace {

// World-space position (pixels) of a connection point on a placed brick.
QPointF connWorldPx(const core::Brick& b,
                    const parts::PartConnectionPoint& c,
                    double px) {
    const double r = b.orientation * M_PI / 180.0;
    const double cs = std::cos(r), sn = std::sin(r);
    const QPointF centre = b.displayArea.center();
    return QPointF(
        (centre.x() + c.position.x() * cs - c.position.y() * sn) * px,
        (centre.y() + c.position.x() * sn + c.position.y() * cs) * px);
}

// Per-connection polarity state used during BFS.
// +timestamp = rail +1, -timestamp = rail -1, 0 = unvisited.
struct ConnState {
    short polarity = 0;
    bool  hasShortcut = false;
};

}  // namespace

void SceneBuilder::addElectricCircuits(const core::Map& map) {
    QSettings s;
    if (!s.value(QStringLiteral("view/electricCircuits"), false).toBool()) return;

    // -------------------------------------------------------------------
    // 1. Collect every (brick, circuit) pair that has at least one
    //    in-part circuit. Build a lookup: brickGuid -> brick pointer and
    //    per-connection state.
    // -------------------------------------------------------------------
    struct BrickEntry {
        const core::Brick*          brick   = nullptr;
        const parts::PartMetadata*  meta    = nullptr;
        // Polarity per connection index. Sized to meta->connections.size().
        QVector<ConnState>          state;
    };

    QHash<QString, BrickEntry> entries;

    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& bl = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& b : bl.bricks) {
            if (b.guid.isEmpty()) continue;
            auto meta = parts_.metadata(b.partNumber);
            if (!meta || meta->electricCircuits.isEmpty()) continue;
            BrickEntry e;
            e.brick = &b;
            e.meta  = new parts::PartMetadata(*meta);   // owned copy for BFS lifetime
            e.state.resize(meta->connections.size());
            entries.insert(b.guid, std::move(e));
        }
    }
    if (entries.isEmpty()) return;

    // <LinkedTo> names the partner's connection: map each connection id to
    // (brick, index) so polarity flows into the right end of the partner.
    struct ConnRef { QString brickGuid; int index = -1; };
    QHash<QString, ConnRef> connOwner;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const auto& conns = it->brick->connections;
        for (int i = 0; i < static_cast<int>(conns.size()); ++i) {
            if (!conns[i].guid.isEmpty()) connOwner.insert(conns[i].guid, { it.key(), i });
        }
    }
    const auto partnerOf = [&](const core::Brick& b, int idx) -> ConnRef {
        if (idx < 0 || idx >= static_cast<int>(b.connections.size())) return {};
        const ConnRef r = connOwner.value(b.connections[idx].linkedToId);
        const auto e = entries.constFind(r.brickGuid);
        if (e == entries.constEnd() || r.index < 0 || r.index >= e->state.size()) return {};
        return r;
    };

    // -------------------------------------------------------------------
    // 2. BFS to assign consistent polarity across connected bricks,
    //    matching ElectricCircuitChecker.cs logic.
    // -------------------------------------------------------------------
    short stamp = 1;

    auto propagate = [&](const QString& startGuid) {
        BrickEntry* startEntry = entries.find(startGuid) != entries.end()
                                 ? &entries[startGuid] : nullptr;
        if (!startEntry) return;

        ++stamp;
        if (stamp == std::numeric_limits<short>::max()) stamp = 1;

        QList<QString> toExplore;
        toExplore.append(startGuid);

        // Seed the first circuit's first connection with +stamp.
        const int seed1 = startEntry->meta->electricCircuits[0].index1;
        startEntry->state[seed1].polarity = stamp;

        // If it's already linked to another electric brick, seed the partner.
        if (const ConnRef p = partnerOf(*startEntry->brick, seed1); p.index >= 0) {
            entries[p.brickGuid].state[p.index].polarity = (short)(-stamp);
            toExplore.append(p.brickGuid);
        }

        while (!toExplore.isEmpty()) {
            const QString guid = toExplore.takeFirst();
            BrickEntry* entry = &entries[guid];
            bool needReexplore = false;

            for (const auto& circuit : entry->meta->electricCircuits) {
                ConnState& s1 = entry->state[circuit.index1];
                ConnState& s2 = entry->state[circuit.index2];

                // Ensure s1 carries the incoming electricity; swap if needed.
                ConnState* start = &s1;
                ConnState* end   = &s2;
                int startIdx = circuit.index1;
                int endIdx   = circuit.index2;
                if (std::abs(s2.polarity) == stamp) {
                    std::swap(start, end);
                    std::swap(startIdx, endIdx);
                }

                if (std::abs(start->polarity) != stamp) {
                    needReexplore = true;
                    continue;
                }

                // Short circuit: end already set to same polarity.
                if (end->polarity == start->polarity) {
                    start->hasShortcut = true;
                    continue;
                }

                // Transfer polarity to end if not yet visited.
                if (end->polarity != -(start->polarity)) {
                    end->polarity = (short)(-(start->polarity));

                    if (needReexplore) {
                        toExplore.prepend(guid);
                        needReexplore = false;
                    }

                    // Propagate to linked neighbor brick.
                    if (const ConnRef p = partnerOf(*entry->brick, endIdx); p.index >= 0) {
                        ConnState& nState = entries[p.brickGuid].state[p.index];
                        if (nState.polarity == end->polarity) {
                            end->hasShortcut = true;
                        } else if (nState.polarity != -(end->polarity)) {
                            nState.polarity = (short)(-(end->polarity));
                            toExplore.append(p.brickGuid);
                        }
                    }
                }
            }
        }
    };

    // Run BFS from every electric brick not yet stamped.
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        BrickEntry& e = it.value();
        if (!e.meta->electricCircuits.isEmpty()) {
            const int i0 = e.meta->electricCircuits[0].index1;
            if (std::abs(e.state[i0].polarity) != stamp) {
                propagate(it.key());
            }
        }
    }

    // -------------------------------------------------------------------
    // 3. Render: one line per electric connection joint, drawn from this
    //    connection point to the linked neighbor's. Colour is fixed by
    //    electricPlug: +1 = OrangeRed, -1 = Cyan. This draws each
    //    physical rail as a continuous coloured line across the track run.
    // -------------------------------------------------------------------
    const double px = kPixelsPerStud;

    static const QColor kRed      = QColor(255,  69,   0, 200); // OrangeRed
    static const QColor kBlue     = QColor(  0, 255, 255, 200); // Cyan
    static const QColor kShortcut = QColor(255, 165,   0, 200); // Orange

    // Constant screen-pixel sizes, cosmetic pens (don't scale with zoom).
    constexpr double strokeWidth = 3.0;  // screen px
    constexpr double shortcutWidth  = 8.0;   // screen px
    constexpr double shortcutStroke = 2.0;   // screen px

    auto makePen = [](QColor col, double w) {
        QPen p(col);
        p.setWidthF(w);
        p.setCosmetic(true);
        return p;
    };

    // World-scaled perpendicular offset matching BlueBrick's ELECTRIC_WIDTH
    // (2.5 studs). This offsets the two lines to the actual rail positions.
    // Stroke is cosmetic (fixed screen px) so lines don't get fat when zoomed in.
    const double halfOffset = 2.0 * px;

    LayerSink sink{ scene_, electricItems_, 5e5, true };

    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const BrickEntry& e = it.value();
        for (const auto& circuit : e.meta->electricCircuits) {
            const int i1 = circuit.index1;
            const int i2 = circuit.index2;
            // Normalise: posIdx = +1 plug, negIdx = -1 plug.
            const int posIdx = (e.meta->connections[i1].electricPlug > 0) ? i1 : i2;
            const int negIdx = (posIdx == i1) ? i2 : i1;

            const QPointF pPos = connWorldPx(*e.brick, e.meta->connections[posIdx], px);
            const QPointF pNeg = connWorldPx(*e.brick, e.meta->connections[negIdx], px);

            const double len = std::hypot(pNeg.x() - pPos.x(), pNeg.y() - pPos.y());
            if (len < 0.5) continue;

            // Perpendicular offset from the circuit centreline.
            // Direction is from +1 toward -1 so +norm is always on the same
            // physical side across all parts.
            const QPointF dir((pNeg.x() - pPos.x()) / len, (pNeg.y() - pPos.y()) / len);
            const QPointF norm(-dir.y() * halfOffset, dir.x() * halfOffset);

            // Red line offset to the +1 side, cyan to the -1 side.
            auto* lineRed = new QGraphicsLineItem(QLineF(pPos + norm, pNeg + norm));
            lineRed->setPen(makePen(kRed, strokeWidth));
            lineRed->setZValue(500);
            sink.add(lineRed);

            auto* lineCyan = new QGraphicsLineItem(QLineF(pPos - norm, pNeg - norm));
            lineCyan->setPen(makePen(kBlue, strokeWidth));
            lineCyan->setZValue(500);
            sink.add(lineCyan);
        }
    }

    // Shortcut sign: a perpendicular line segment centred on the bad
    // connection point, matching BlueBrick's DrawLines diamond approach
    // but simplified to a cross tick perpendicular to the circuit.
    // (BlueBrick draws a diamond; we keep that.)
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const BrickEntry& e = it.value();
        for (const auto& circuit : e.meta->electricCircuits) {
            for (int idx : { circuit.index1, circuit.index2 }) {
                if (!e.state[idx].hasShortcut) continue;
                const QPointF c = connWorldPx(*e.brick, e.meta->connections[idx], px);
                QPolygonF diamond;
                diamond << QPointF(c.x() - shortcutWidth, c.y())
                        << QPointF(c.x(), c.y() - shortcutWidth)
                        << QPointF(c.x(), c.y() + shortcutWidth)
                        << QPointF(c.x() + shortcutWidth, c.y());
                auto* poly = new QGraphicsPolygonItem(diamond);
                poly->setPen(makePen(kShortcut, shortcutStroke));
                poly->setBrush(Qt::NoBrush);
                poly->setZValue(502);
                sink.add(poly);
            }
        }
    }

    // Clean up owned metadata copies.
    for (auto& e : entries) delete e.meta;
}

}  // namespace bld::rendering
