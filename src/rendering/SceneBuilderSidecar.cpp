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
#include "VenueDraw.h"

#include "../core/AnchoredLabel.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"
#include "../core/Venue.h"

#include <QBrush>
#include <QFont>
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
                QFont f(QStringLiteral("Sans"));
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
        QFont f(QStringLiteral("Sans"));
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
        QFont f(lbl.font.familyName, static_cast<int>(lbl.font.sizePt));
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

void SceneBuilder::addModuleLabels(const core::Map& map) {
    if (map.sidecar.modules.empty()) return;
    // User-controlled via View > Module Names. Default = on so the user
    // sees annotations the first time they import / create a module.
    QSettings vs;
    if (!vs.value(QStringLiteral("view/moduleNames"), true).toBool()) return;
    // Configurable frame thickness + label size (Preferences > Appearance).
    const double frameThickness = std::clamp(
        vs.value(QStringLiteral("view/moduleFrameThickness"), 5.0).toDouble(),
        0.5, 40.0);
    const double tickThickness  = std::max(1.0, frameThickness * 1.25);
    // Module label size as a percentage of the module's long axis.
    // Default 35 % — users can push higher for bolder annotation or
    // lower for modest labels.
    const double labelPercent = std::clamp(
        vs.value(QStringLiteral("view/moduleLabelPercent"), 35.0).toDouble(),
        5.0, 100.0);

    // Module annotations sit above every layer so they're always
    // visible. Tagged kind="moduleAnnotation" so they don't intercept
    // selection — the user interacts with modules via the Modules panel.
    LayerSink sink{ scene_, moduleLabelItems_, 200000.0, true };

    static const QColor kPalette[] = {
        QColor(230,  90,  40), QColor( 30, 150, 220), QColor( 60, 180,  90),
        QColor(180,  90, 200), QColor(220, 160,  30), QColor( 90, 180, 200),
        QColor(220,  80, 140),
    };

    // First pass: compute every module's frame rect. We need them up
    // front so each label can avoid overlapping any other module's
    // frame when it gets placed outside.
    struct ModuleEntry { int idx; const core::Module* mod; QRectF framePx; QColor color; };
    std::vector<ModuleEntry> modules;
    {
        int modIdx = 0;
        for (const auto& mod : map.sidecar.modules) {
            QRectF bbStuds;
            for (const auto& L : map.layers()) {
                if (!L || L->kind() != core::LayerKind::Brick) continue;
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                    if (mod.memberIds.contains(b.guid)) bbStuds = bbStuds.united(b.displayArea);
                }
            }
            if (bbStuds.isEmpty()) { ++modIdx; continue; }
            constexpr double kInsetStuds = 1.0;
            const QRectF framePx(
                (bbStuds.x()      - kInsetStuds) * kPx,
                (bbStuds.y()      - kInsetStuds) * kPx,
                (bbStuds.width()  + 2 * kInsetStuds) * kPx,
                (bbStuds.height() + 2 * kInsetStuds) * kPx);
            const QColor color = kPalette[modIdx % (sizeof(kPalette) / sizeof(kPalette[0]))];
            modules.push_back({ modIdx, &mod, framePx, color });
            ++modIdx;
        }
    }

    // Track every label rect we've committed so subsequent labels can
    // avoid landing on top of them (not just on other modules' frames).
    std::vector<QRectF> placedLabels;

    for (const auto& me : modules) {
        const core::Module& mod = *me.mod;
        const QRectF framePx = me.framePx;
        const QColor color   = me.color;

        // Double-stroke: solid dark outer + dashed colored inner. Keeps
        // the frame visible against both light sky-blue map background
        // and dark brick colors.
        auto* frameBack = new QGraphicsRectItem(framePx);
        QPen backPen(QColor(20, 20, 20));
        backPen.setWidthF(frameThickness + 3.0);
        backPen.setCosmetic(true);
        frameBack->setPen(backPen);
        frameBack->setBrush(Qt::NoBrush);
        sink.add(frameBack);

        auto* frame = new QGraphicsRectItem(framePx);
        QPen framePen(color);
        framePen.setWidthF(frameThickness);
        framePen.setCosmetic(true);
        framePen.setStyle(Qt::DashLine);
        frame->setPen(framePen);
        frame->setBrush(Qt::NoBrush);
        sink.add(frame);

        constexpr double kTickPx = 12.0;
        auto addCornerTicks = [&](QPointF corner, QPointF dx, QPointF dy) {
            auto* h = new QGraphicsLineItem(corner.x(), corner.y(),
                                              corner.x() + dx.x(), corner.y() + dx.y());
            auto* v = new QGraphicsLineItem(corner.x(), corner.y(),
                                              corner.x() + dy.x(), corner.y() + dy.y());
            QPen p(color);
            p.setWidthF(tickThickness); p.setCosmetic(true);
            h->setPen(p); v->setPen(p);
            sink.add(h); sink.add(v);
        };
        addCornerTicks(framePx.topLeft(),     QPointF( kTickPx, 0), QPointF(0,  kTickPx));
        addCornerTicks(framePx.topRight(),    QPointF(-kTickPx, 0), QPointF(0,  kTickPx));
        addCornerTicks(framePx.bottomLeft(),  QPointF( kTickPx, 0), QPointF(0, -kTickPx));
        addCornerTicks(framePx.bottomRight(), QPointF(-kTickPx, 0), QPointF(0, -kTickPx));

        // Module name. Default: place outside the enclosed area on
        // whichever side has clear space. Vertical labels face the
        // inside (top of text points toward the module) so the name
        // reads naturally when you look at the module. Falls back to
        // centred inside only when every outside slot collides.
        const QString name = mod.name.isEmpty() ? QStringLiteral("(unnamed module)") : mod.name;
        const bool portrait = framePx.height() > framePx.width() * 1.1;

        auto* label = new QGraphicsSimpleTextItem(name);
        QFont lf(QStringLiteral("Sans"));
        lf.setBold(true);
        constexpr int kProbePx = 100;
        lf.setPixelSize(kProbePx);
        label->setFont(lf);
        label->setBrush(QBrush(QColor(10, 10, 10)));
        const QRectF probe = label->boundingRect();

        auto othersOverlap = [&modules, &me, &placedLabels](const QRectF& r) {
            for (const auto& other : modules) {
                if (other.mod == me.mod) continue;
                if (r.intersects(other.framePx)) return true;
            }
            for (const QRectF& placed : placedLabels) {
                if (r.intersects(placed)) return true;
            }
            return false;
        };

        enum class Side { Top, Bottom, Left, Right, Inside };
        struct Candidate { Side side; QPointF centre; QRectF labelBox; int fontPx; };

        // labelPercent% of the module's long axis, clamped to a sane
        // readable range. User-tunable via Preferences > Appearance.
        const double longAxis = portrait ? framePx.height() : framePx.width();
        const int outsidePx = static_cast<int>(
            std::clamp(longAxis * (labelPercent / 100.0), 16.0, 400.0));

        lf.setPixelSize(outsidePx);
        label->setFont(lf);
        const QRectF outsideText = label->boundingRect();

        auto boxAround = [](QPointF centre, double w, double h) {
            return QRectF(centre.x() - w / 2.0 - 8.0,
                          centre.y() - h / 2.0 - 4.0,
                          w + 16.0, h + 8.0);
        };

        const double padOut = 16.0;
        QPointF topC   (framePx.center().x(), framePx.top()    - outsideText.height() / 2.0 - padOut);
        QPointF botC   (framePx.center().x(), framePx.bottom() + outsideText.height() / 2.0 + padOut);
        QPointF leftC  (framePx.left()  - outsideText.height() / 2.0 - padOut, framePx.center().y());
        QPointF rightC (framePx.right() + outsideText.height() / 2.0 + padOut, framePx.center().y());

        // Try short-axis sides first (labels look best parallel to the
        // long axis). Within each pair, pick the non-colliding side;
        // if both collide, try long-axis sides; if every outside slot
        // collides, fall back to inside.
        std::vector<Candidate> tries;
        const double hW = outsideText.width(), hH = outsideText.height();
        if (portrait) {
            tries.push_back({ Side::Left,   leftC,   boxAround(leftC,   hH, hW), outsidePx });
            tries.push_back({ Side::Right,  rightC,  boxAround(rightC,  hH, hW), outsidePx });
            tries.push_back({ Side::Top,    topC,    boxAround(topC,    hW, hH), outsidePx });
            tries.push_back({ Side::Bottom, botC,    boxAround(botC,    hW, hH), outsidePx });
        } else {
            tries.push_back({ Side::Top,    topC,    boxAround(topC,    hW, hH), outsidePx });
            tries.push_back({ Side::Bottom, botC,    boxAround(botC,    hW, hH), outsidePx });
            tries.push_back({ Side::Left,   leftC,   boxAround(leftC,   hH, hW), outsidePx });
            tries.push_back({ Side::Right,  rightC,  boxAround(rightC,  hH, hW), outsidePx });
        }

        Side chosenSide = Side::Inside;
        QPointF centerPx;
        int finalPx = outsidePx;
        for (const auto& c : tries) {
            if (!othersOverlap(c.labelBox)) {
                chosenSide = c.side;
                centerPx   = c.centre;
                finalPx    = c.fontPx;
                break;
            }
        }
        if (chosenSide == Side::Inside) {
            // Dense neighbourhood — fall back to centred inside,
            // scaled to fill the interior so the name is still big.
            const double interiorW = (portrait ? framePx.height() : framePx.width())  * 0.92;
            const double interiorH = (portrait ? framePx.width()  : framePx.height()) * 0.55;
            const double scaleW = (probe.width()  > 0) ? interiorW / probe.width()  : 1.0;
            const double scaleH = (probe.height() > 0) ? interiorH / probe.height() : 1.0;
            finalPx = std::max(24, static_cast<int>(kProbePx * std::min(scaleW, scaleH)));
            centerPx = framePx.center();
        }

        lf.setPixelSize(finalPx);
        label->setFont(lf);
        const QRectF lbb = label->boundingRect();

        // Vertical sides (Left/Right + portrait Inside) rotate so the
        // top of the text faces the INTERIOR of the module.
        double rotation = 0.0;
        const bool needsRotation = chosenSide == Side::Left
                                    || chosenSide == Side::Right
                                    || (chosenSide == Side::Inside && portrait);
        if (needsRotation) {
            if (chosenSide == Side::Right) rotation =  90.0;   // top → left  (inside)
            else                            rotation = -90.0;  // top → right (inside) — default
        }

        QTransform tr;
        tr.translate(centerPx.x(), centerPx.y());
        if (rotation != 0.0) tr.rotate(rotation);
        tr.translate(-lbb.width() / 2.0, -lbb.height() / 2.0);
        label->setTransform(tr);

        // Solid white fill + thick coloured border + subtle drop-
        // shadow-like offset rect for separation from busy-colored
        // bricks behind.
        const QRectF lsbr = label->sceneBoundingRect();
        const QRectF bgRect = lsbr.adjusted(-8, -4, 8, 4);
        auto* shadow = new QGraphicsRectItem(bgRect.translated(2, 2));
        shadow->setPen(Qt::NoPen);
        shadow->setBrush(QBrush(QColor(0, 0, 0, 80)));
        sink.add(shadow);
        auto* bg = new QGraphicsRectItem(bgRect);
        QPen bgPen(color.darker(130));
        bgPen.setWidthF(2.0);
        bgPen.setCosmetic(true);
        bg->setPen(bgPen);
        bg->setBrush(QBrush(QColor(255, 255, 255)));
        sink.add(bg);
        sink.add(label);
        placedLabels.push_back(bgRect);
    }
}

}  // namespace bld::rendering
