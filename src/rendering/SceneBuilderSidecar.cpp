// Sidecar-driven render passes: venue footprint, world-anchored labels,
// and module-frame annotations. These all live in the .bbm.bld sidecar
// (fork-only data vanilla BlueBrick never sees) and are rendered as
// overlays beneath / above the per-layer items.
//
// Split out of SceneBuilder.cpp to keep that file focused on the
// per-layer pipeline; the sidecar overlays are self-contained and only
// touch SceneBuilder::{venueItems_, worldLabelItems_, moduleLabelItems_}.

#include "SceneBuilder.h"
#include "SceneBuilderInternal.h"
#include "MapText.h"
#include "ModuleLabels.h"
#include "VenueDraw.h"

#include "../core/AnchoredLabel.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"
#include "../core/Venue.h"

#include <QBrush>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QSettings>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bld::rendering {

using detail::kPx;
using detail::LayerSink;
using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::kBrickDataKind;

void SceneBuilder::addVenue(const core::Map& map) {
    if (!map.sidecar.venue || !map.sidecar.venue->enabled) return;
    const auto& v = *map.sidecar.venue;

    // Venue items live directly in the scene at a fixed low z so they
    // render beneath every layer. Tracked in venueItems_ for cleanup.
    LayerSink sink{ scene_, venueItems_, -100000.0, true };

    // Walkway buffer: translucent band on the INSIDE of every non-Wall
    // edge (Door + Open). Walls are solid barriers so bricks can butt
    // right up against them — no buffer needed. Drawn BEFORE the edges
    // so edge strokes render on top.
    const double walkPx = v.minWalkwayStuds * kPx;
    if (walkPx > 0.001) {
        for (const auto& edge : v.edges) {
            if (edge.kind == core::EdgeKind::Wall) continue;
            if (edge.polyline.size() < 2) continue;
            QPainterPath band;
            for (int i = 1; i < edge.polyline.size(); ++i) {
                const QPointF a = edge.polyline[i - 1] * kPx;
                const QPointF b = edge.polyline[i]     * kPx;
                const QPointF d = b - a;
                const double len = std::hypot(d.x(), d.y());
                if (len < 0.001) continue;
                const QPointF nIn(-d.y() / len, d.x() / len);  // left-hand normal
                const QPointF aIn = a + nIn * walkPx;
                const QPointF bIn = b + nIn * walkPx;
                QPolygonF quad; quad << a << b << bIn << aIn;
                band.addPolygon(quad);
            }
            auto* bandItem = new QGraphicsPathItem(band);
            bandItem->setPen(Qt::NoPen);
            bandItem->setBrush(QBrush(QColor(255, 170, 0, 60),
                                       Qt::BDiagPattern));
            sink.add(bandItem);
        }
    }

    for (const auto& edge : v.edges) {
        if (edge.polyline.size() < 2) continue;
        QPainterPath path;
        path.moveTo(edge.polyline[0] * kPx);
        for (int i = 1; i < edge.polyline.size(); ++i) {
            path.lineTo(edge.polyline[i] * kPx);
        }
        auto* item = new QGraphicsPathItem(path);
        QPen pen;
        pen.setCosmetic(true);
        // Beefier venue outline strokes — walls especially need to read
        // as a bold border against bricks / grid, not a hairline.
        switch (edge.kind) {
            case core::EdgeKind::Wall:
                pen.setColor(QColor(30, 30, 30));
                pen.setWidthF(7.0);
                break;
            case core::EdgeKind::Door:
                pen.setColor(QColor(0, 160, 0));
                pen.setStyle(Qt::DashLine);
                pen.setWidthF(5.0);
                break;
            case core::EdgeKind::Open:
                pen.setColor(QColor(0, 0, 200));
                pen.setStyle(Qt::DotLine);
                pen.setWidthF(4.0);
                break;
        }
        item->setPen(pen);
        if (edge.estimated) item->setOpacity(0.45); // not measured yet
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setData(kBrickDataLayerIndex, -1);
        item->setData(kBrickDataGuid,       QStringLiteral("venue"));
        item->setData(kBrickDataKind,       QStringLiteral("venue"));
        sink.add(item);

        // Measurement label on the OUTSIDE of each segment. Uses feet
        // by default; converts via 1 stud = 0.026248 ft.
        if (edge.polyline.size() >= 2) {
            const QPointF a = edge.polyline.first();
            const QPointF b = edge.polyline.last();
            const QPointF d = b - a;
            const double lenStuds = std::hypot(d.x(), d.y());
            if (lenStuds > 0.5) {
                const double lenFt = lenStuds * 0.026248;
                const double lenIn = lenFt * 12.0;
                QString txt;
                if (lenFt < 1.0) {
                    txt = QStringLiteral("%1\"").arg(lenIn, 0, 'f', 1);
                } else {
                    txt = QStringLiteral("%1 ft").arg(lenFt, 0, 'f', 2);
                }
                if (!edge.label.isEmpty()) {
                    txt = edge.label + QStringLiteral(" — ") + txt;
                }
                if (edge.estimated) txt = venuedraw::estimatedText(txt);

                // Right-hand normal (positive 90° rotation of the
                // segment direction). Users can reverse the polygon
                // winding if they want labels on the other side.
                const QPointF unit(d.x() / lenStuds, d.y() / lenStuds);
                const QPointF normal(-unit.y(), unit.x());
                constexpr double kLabelOffsetPx = 16.0;
                const QPointF midStuds = (a + b) * 0.5;
                const QPointF labelScenePos = midStuds * kPx
                                              + normal * kLabelOffsetPx;

                auto* lbl = new QGraphicsSimpleTextItem(txt);
                QFont f(mapFontFamily());
                f.setBold(true);
                // Font size is user-configurable via Preferences
                // (settings key venue/labelPx). Default 28 px stays
                // legible at map-scale zooms that show the whole venue.
                const int labelPx =
                    venueLabelPx_
                        ? std::max(1, static_cast<int>(std::lround(*venueLabelPx_)))
                        : std::max(10, QSettings().value(QStringLiteral("venue/labelPx"), 28).toInt());
                f.setPixelSize(labelPx);
                lbl->setFont(f);
                lbl->setBrush(QBrush(QColor(20, 20, 20)));
                double angleDeg = std::atan2(d.y(), d.x()) * 180.0 / M_PI;
                if (angleDeg > 90.0 || angleDeg < -90.0) angleDeg += 180.0;
                const QRectF tb = lbl->boundingRect();
                QTransform tr;
                tr.translate(labelScenePos.x(), labelScenePos.y());
                tr.rotate(angleDeg);
                tr.translate(-tb.width() / 2.0, -tb.height() / 2.0);
                lbl->setTransform(tr);

                const QRectF sceneBb = lbl->sceneBoundingRect();
                auto* bg = new QGraphicsRectItem(sceneBb.adjusted(-3, -1, 3, 1));
                bg->setPen(Qt::NoPen);
                bg->setBrush(QBrush(QColor(255, 255, 255, 220)));
                sink.add(bg);
                sink.add(lbl);
            }
        }
    }
    // Venue model v2 parts (VenueDraw.h): obstacles styled by kind,
    // power points, measurements and notes.
    const auto lines = [&](const QVector<QLineF>& segs, const QColor& color, double widthPx,
                           Qt::PenStyle style = Qt::SolidLine) {
        for (const QLineF& l : segs) {
            auto* li = new QGraphicsLineItem(QLineF(l.p1() * kPx, l.p2() * kPx));
            QPen pen(color);
            pen.setCosmetic(true);
            pen.setWidthF(widthPx);
            pen.setStyle(style);
            li->setPen(pen);
            sink.add(li);
        }
    };
    const double labelPx = venueLabelPx_
                               ? *venueLabelPx_
                               : std::max(10, QSettings().value(QStringLiteral("venue/labelPx"), 28).toInt());
    const auto text = [&](const QString& s, QPointF studs, double px, const QColor& color, bool italic,
                          double angle, bool centred) {
        auto* t = new QGraphicsSimpleTextItem(s);
        QFont f(mapFontFamily());
        f.setPixelSize(std::max(8, static_cast<int>(px)));
        f.setItalic(italic);
        t->setFont(f);
        t->setBrush(color);
        const QRectF tb = t->boundingRect();
        QTransform tr;
        tr.translate(studs.x() * kPx, studs.y() * kPx);
        tr.rotate(angle);
        if (centred) tr.translate(-tb.width() / 2.0, -tb.height() / 2.0);
        else tr.translate(0, -tb.height() / 2.0);
        t->setTransform(tr);
        sink.add(t);
    };

    for (const auto& ob : v.obstacles) {
        if (ob.polygon.size() < 3) continue;
        QPolygonF poly;
        for (const auto& p : ob.polygon) poly << p * kPx;
        auto* item = new QGraphicsPolygonItem(poly);
        const auto style = venuedraw::obstacleStyle(ob.kind);
        QPen pen(style.stroke);
        pen.setCosmetic(true);
        pen.setWidthF(style.strokeWidthPx);
        item->setPen(pen);
        if (ob.kind == core::ObstacleKind::Other)
            item->setBrush(QBrush(QColor(120, 120, 120, 100), Qt::BDiagPattern));
        else item->setBrush(style.fill ? QBrush(*style.fill) : QBrush(Qt::NoBrush));
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setData(kBrickDataLayerIndex, -1);
        item->setData(kBrickDataGuid,       QStringLiteral("venue"));
        item->setData(kBrickDataKind,       QStringLiteral("venue"));
        sink.add(item);
        if (ob.kind == core::ObstacleKind::Stairs) {
            const auto marks = venuedraw::stairMarks(ob.polygon, ob.upDegrees);
            lines(marks.treads, style.stroke, 1.0);
            lines(marks.arrow, QColor(30, 30, 30), 2.0);
        } else if (ob.kind == core::ObstacleKind::Elevator) {
            lines(venuedraw::elevatorCross(ob.polygon), style.stroke, 1.0);
        }
    }

    for (const auto& d : v.dimensions) {
        const auto g = venuedraw::dimensionGeometry(d.from, d.to);
        if (!g) continue;
        const QColor c = d.estimated ? venuedraw::estimateColor() : venuedraw::dimensionColor();
        lines({ g->line }, c, 1.5, d.estimated ? Qt::DashLine : Qt::SolidLine);
        lines({ g->ticks[0], g->ticks[1] }, c, 1.5);
        if (!d.label.isEmpty())
            text(d.estimated ? venuedraw::estimatedText(d.label) : d.label, g->label, labelPx * 0.8, c, false,
                 g->angleDeg, true);
    }

    for (const auto& p : v.power) {
        const double r = venuedraw::kPowerRadiusStuds * kPx;
        auto* dot = new QGraphicsEllipseItem(QRectF(p.pos * kPx - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        QPen pen(venuedraw::powerColor());
        pen.setCosmetic(true);
        pen.setWidthF(2.0);
        dot->setPen(pen);
        dot->setBrush(p.floor ? venuedraw::powerColor() : QColor(Qt::white));
        sink.add(dot);
        const QString t = venuedraw::powerText(p);
        if (!t.isEmpty())
            text(t, p.pos + QPointF(venuedraw::kPowerRadiusStuds * 1.4, 0), labelPx * 0.6,
                 venuedraw::powerColor(), false, 0, false);
    }

    for (const auto& n : v.notes) {
        text(n.estimated ? venuedraw::estimatedText(n.text) : n.text, n.pos, labelPx * 0.8,
             n.estimated ? venuedraw::estimateColor() : QColor(30, 30, 30), n.estimated, 0, false);
    }
}

void SceneBuilder::addAnchoredLabels(const core::Map& map) {
    if (map.sidecar.anchoredLabels.empty()) return;

    // World-anchored labels go directly to the scene; brick/group/module
    // anchors become children of their target so Qt transform inheritance
    // moves them for free.
    LayerSink sink{ scene_, worldLabelItems_, 100000.0, true };

    for (const auto& lbl : map.sidecar.anchoredLabels) {
        auto* t = new QGraphicsSimpleTextItem(lbl.text);
        QFont f(mapFontFamily(), static_cast<int>(lbl.font.sizePt));
        f.setBold(lbl.font.styleString.contains(QStringLiteral("Bold")));
        f.setItalic(lbl.font.styleString.contains(QStringLiteral("Italic")));
        t->setFont(f);
        t->setBrush(QBrush(lbl.color.color));
        t->setRotation(lbl.offsetRotation);
        t->setFlag(QGraphicsItem::ItemIsSelectable, true);
        t->setFlag(QGraphicsItem::ItemIsMovable,    true);
        t->setData(kBrickDataLayerIndex, -1);
        t->setData(kBrickDataGuid,       lbl.id);
        t->setData(kBrickDataKind,       QStringLiteral("label"));

        if (lbl.kind == core::AnchorKind::Brick) {
            auto it = brickByGuid_.constFind(lbl.targetId);
            if (it != brickByGuid_.constEnd()) {
                t->setParentItem(*it);
                t->setPos(lbl.offset * kPx);
                continue;
            }
            // fall through to world-positioned if anchor not found
        }
        t->setPos(lbl.offset * kPx);
        sink.add(t);
    }
}

void SceneBuilder::setLabelsVisible(bool visible) {
    // Brick labels are children of their brick, so walk the scene.
    for (QGraphicsItem* it : scene_.items())
        if (it->data(kBrickDataKind).toString() == QLatin1String("label")) it->setVisible(visible);
}

void SceneBuilder::addModuleLabels(const core::Map& map) {
    if (map.sidecar.modules.empty()) return;
    // View > Module Names: on by default, so a new module shows its name.
    QSettings vs;
    if (!vs.value(QStringLiteral("view/moduleNames"), true).toBool()) return;
    // Preferences > Appearance: frame thickness and name size.
    const double frameThickness =
        std::clamp(vs.value(QStringLiteral("view/moduleFrameThickness"), 5.0).toDouble(), 0.5, 40.0);
    const double labelPercent =
        std::clamp(vs.value(QStringLiteral("view/moduleLabelPercent"), 35.0).toDouble(), 5.0, 100.0);

    // Above every sheet, and not selectable: modules are picked in the
    // Modules panel.
    LayerSink sink{ scene_, moduleLabelItems_, 200000.0, true };

    // The web's look (ModuleOverlay.tsx): one light-blue dashed frame and
    // an outlined light-blue bold name per module.
    for (const auto& mod : map.sidecar.modules) {
        QRectF studs;
        // Pieces on hidden sheets don't frame or name their module.
        for (const auto& L : map.layers()) {
            if (!L || L->kind() != core::LayerKind::Brick || !L->visible) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                if (mod.memberIds.contains(b.guid)) studs = studs.united(b.displayArea);
        }
        if (studs.isEmpty()) continue;
        const QString name = mod.name.isEmpty() ? QStringLiteral("(module)") : mod.name;
        const LineWidthAt lineWidth = mapLineWidth(QStringLiteral("Bold"));
        const auto widthAt = [&lineWidth, &name](double fontPx) { return lineWidth(name, fontPx); };
        const ModuleLabelLayout at = moduleLabelLayout(studs, name, labelPercent, widthAt);

        auto* frame = new QGraphicsRectItem(at.frame);
        QPen framePen(kModuleFrameColor);
        framePen.setWidthF(frameThickness);
        framePen.setCosmetic(true);
        // Qt's dash is in pen widths; the web's is in screen px.
        framePen.setDashPattern({ kModuleFrameDash[0] / frameThickness, kModuleFrameDash[1] / frameThickness });
        framePen.setCapStyle(Qt::FlatCap);
        frame->setPen(framePen);
        frame->setBrush(Qt::NoBrush);
        frame->setData(kModuleAnnotationRole, QStringLiteral("frame"));
        sink.add(frame);

        // The name, centred in its box and vertically centred on the line
        // (Konva's "middle" baseline), the outline drawn under the fill.
        const QFont f = mapFont(QStringLiteral("Bold"), at.fontPx);
        const QPainterPath path =
            textPath(f, { { name, (at.width - lineWidth(name, at.fontPx)) / 2.0, 0.0 } }, 1.0);
        QTransform tr;
        tr.translate(at.textPos.x(), at.textPos.y());
        tr.rotate(at.rotation);
        auto* outline = new QGraphicsPathItem(path);
        QPen outlinePen(kModuleNameStroke);
        outlinePen.setWidthF(moduleNameStrokePx(at.fontPx));
        outlinePen.setJoinStyle(Qt::MiterJoin);
        outline->setPen(outlinePen);
        outline->setBrush(Qt::NoBrush);
        outline->setTransform(tr);
        outline->setData(kModuleAnnotationRole, QStringLiteral("outline"));
        sink.add(outline);
        auto* label = new QGraphicsPathItem(path);
        label->setPen(Qt::NoPen);
        label->setBrush(kModuleNameFill);
        label->setTransform(tr);
        label->setData(kModuleAnnotationRole, QStringLiteral("name"));
        sink.add(label);
        // The frame and the name, for fitting a view around them.
        moduleAnnotationRects_.append({ mod.id, at.bounds });
    }
}

}  // namespace bld::rendering
