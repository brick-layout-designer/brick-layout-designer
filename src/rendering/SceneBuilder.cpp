#include "SceneBuilder.h"
#include "MapText.h"
#include "UnknownPart.h"
#include "SceneBuilderInternal.h"

#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QSettings>

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

namespace bld::rendering {

// Pull the shared helpers (kPx, studToPx, LayerSink, kBrickData* keys)
// from detail:: so the per-layer free helpers below read naturally.
// SceneBuilderElectric.cpp and SceneBuilderSidecar.cpp do the same.
using detail::kPx;
using detail::studToPx;
using detail::LayerSink;
using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::kBrickDataKind;

namespace {

// Selection is now painted from MapView::drawForeground so every item kind
// (pixmap, rect, line, ellipse) gets a consistent, unmistakable highlight
// regardless of subclass-specific paint overrides. This helper stays only
// to hide connection-point dots on selection changes below.

// Palette matching BlueBrick's ConnectionType.Color-ish defaults: rail (cyan),
// road (orange), monorail (purple), and a fallback green. Type 0 means "no
// type" and never rendered.
QColor colorForConnectionType(const QString& /*type*/) {
    // User prefers all connection markers in a single bright red so they
    // stand out consistently against every brick color (green baseplates,
    // grey road, blue sky, etc.). Per-type color palettes can come back
    // later if users ask for them.
    return QColor(230, 40, 40);
}

// Toggles visibility of every connection-point marker child so they only
// appear while the parent brick is selected. Children are tagged via
// setData(kBrickDataKind, "connDot") by addBrickLayer so we pick them out
// without disturbing anchored-label children etc.
void setConnectionDotsVisible(QGraphicsItem* parent, bool visible) {
    for (QGraphicsItem* child : parent->childItems()) {
        if (child->data(kBrickDataKind).toString() == QStringLiteral("connDot")) {
            child->setVisible(visible);
        }
    }
}

// A part's picture: its connection dots show while it is picked. MapView
// moves and snaps the parts it drags (MapViewDrag.cpp).
class SnappingPixmap : public QGraphicsPixmapItem {
public:
    using QGraphicsPixmapItem::QGraphicsPixmapItem;
protected:
    QVariant itemChange(GraphicsItemChange c, const QVariant& v) override {
        if (c == ItemSelectedChange) {
            setConnectionDotsVisible(this, v.toBool());
        }
        return QGraphicsPixmapItem::itemChange(c, v);
    }
};

void addBrickLayer(const core::LayerBrick& L, LayerSink& sink, parts::PartsLibrary& lib,
                   int layerIndex, QHash<QString, QGraphicsItem*>& brickByGuid) {
    // Read scene-level render toggles once per layer; all bricks in the same
    // layer share these decisions. Stored under view/ in QSettings.
    QSettings settings;
    // PreferencesDialog writes the "always show connection points"
    // toggle to appearance/alwaysShowConnections — read the same key
    // so the checkbox actually does something.
    const bool alwaysShowConns = settings.value(
        QStringLiteral("appearance/alwaysShowConnections"), false).toBool();
    const bool displayHulls    = settings.value(QStringLiteral("view/brickHulls"), false).toBool()
                                 || L.hull.displayHulls;
    const bool displayElev     = settings.value(QStringLiteral("view/brickElevation"), false).toBool();
    // Part number -> library key actually used (after the prefix fallback
    // below). Memoised so a map with many copies of an unresolved part
    // scans the library once per distinct part number, not once per brick.
    QHash<QString, QString> resolvedKeys;
    for (const auto& brick : L.bricks) {
        // BlueBrick bakes the color suffix into the PartNumber string itself
        // (e.g. "3811.1" for a blue 32x32 baseplate, or just "TABLE96X190"
        // for an uncolored composite). Lookup is case-insensitive because
        // upstream stores the part number upper-cased in .bbm but the vendored
        // library uses mixed case on disk.
        auto resolved = resolvedKeys.constFind(brick.partNumber);
        if (resolved == resolvedKeys.constEnd()) {
            QString key = brick.partNumber;
            if (!lib.metadata(key)) {
                // Fallback: match by prefix so a .bbm referencing "3811" picks
                // up any color-specific variant.
                key.clear();
                const QString needle = brick.partNumber.toLower() + QLatin1Char('.');
                for (const QString& candidate : lib.keys()) {
                    if (candidate.toLower().startsWith(needle)) { key = candidate; break; }
                }
            }
            resolved = resolvedKeys.insert(brick.partNumber, key);
        }
        const QString& partKey = resolved.value();
        std::optional<parts::PartMetadata> meta;
        if (!partKey.isEmpty()) meta = lib.metadata(partKey);

        const QRectF areaPx(studToPx(brick.displayArea.x()),
                            studToPx(brick.displayArea.y()),
                            studToPx(brick.displayArea.width()),
                            studToPx(brick.displayArea.height()));
        const QPointF centerPx = areaPx.center();

        QGraphicsItem* item = nullptr;
        if (meta && !meta->gifFilePath.isEmpty()) {
            // Shared, decoded-once pixmap from the library cache — loading
            // meta->gifFilePath here re-read and re-decoded the GIF for
            // every brick on every scene rebuild.
            const QPixmap pm = lib.pixmap(partKey);
            if (!pm.isNull()) {
                auto* p = new SnappingPixmap(pm);
                p->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
                p->setTransformationMode(Qt::SmoothTransformation);
                // Use the full bounding rect as the clickable / selection
                // shape, not the pixmap alpha mask. BlueBrick parts are GIFs
                // with transparent backgrounds — the default MaskShape makes
                // clicks on transparent pixels miss entirely, so the user
                // can click on what looks like the brick and get no
                // selection because the cursor landed on a transparent
                // pixel of the sprite.
                // Imported models are big and full of empty space
                // (courtyards, the inside of a ring of track): clicks there
                // reach what is underneath.
                p->setShapeMode(lib.isImported(partKey) ? QGraphicsPixmapItem::MaskShape
                                                        : QGraphicsPixmapItem::BoundingRectShape);
                p->setOffset(-pm.width() / 2.0, -pm.height() / 2.0);
                p->setTransformOriginPoint(0, 0);
                // High-DPI imports: a part authored at e.g. 16 px/stud
                // has 2× the pixel dimensions of the same brick at 8.
                // The scene runs at 8 px/stud (kPixelsPerStud), so we
                // scale the pixmap item by (kPixelsPerStud / partPxPerStud)
                // to land at the correct stud footprint. Vanilla 8-px
                // parts get scale 1.0 (no-op).
                const int authoredPxPerStud = (meta->pxPerStud > 0)
                    ? meta->pxPerStud : SceneBuilder::kPixelsPerStud;
                if (authoredPxPerStud != SceneBuilder::kPixelsPerStud) {
                    const double s = static_cast<double>(SceneBuilder::kPixelsPerStud)
                                   / static_cast<double>(authoredPxPerStud);
                    p->setScale(s);
                }
                p->setRotation(brick.orientation);
                // The item sits at the displayArea centre (what drag and
                // snap move); parts with an XML hull draw their sprite
                // off it, like BlueBrick. transform() applies after the
                // rotation, so this is a scene-space shift that also
                // carries the connection dots and hull outline.
                const QPointF offset = lib.imageOffset(partKey, brick.orientation);
                if (!offset.isNull()) p->setTransform(QTransform::fromTranslate(offset.x() * kPx, offset.y() * kPx));
                p->setPos(centerPx);
                // Drawn in the sheet's order, like BlueBrick: Bring to Front
                // works whatever the altitude (an altitude edit sorts the
                // sheet instead, EditBrickCommand).
                item = p;
            }
        }
        if (!item) {
            // A part the library doesn't know: vanilla's red cross and part
            // number (UnknownPart.h), turned with the brick. The clear rect
            // takes the clicks and moves like a part.
            const UnknownPartLook look = unknownPartLook(brick.partNumber, brick.displayArea.width(),
                                                         brick.displayArea.height());
            auto* r =
                new QGraphicsRectItem(QRectF(-look.width / 2.0, -look.height / 2.0, look.width, look.height));
            r->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
            r->setPos(centerPx);
            r->setRotation(brick.orientation);
            r->setPen(Qt::NoPen);
            r->setBrush(Qt::NoBrush);
            addUnknownPartDrawing(r, brick.partNumber, look);
            item = r;
        }

        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemIsMovable,    true);
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        item->setData(kBrickDataLayerIndex, layerIndex);
        item->setData(kBrickDataGuid,       brick.guid);
        item->setData(kBrickDataKind,       QStringLiteral("brick"));
        item->setData(SceneBuilder::kBrickPartRole, partKey);

        // Connection-point markers: child ellipses in the brick's local coord
        // system so they transform with the brick for free. Hidden by default;
        // shown on selection via the ItemSelectedChange hook above (or
        // unconditionally if view/connectionPoints is on). Radius bumped to
        // 6px with a white ring + type-colored fill so they stand out
        // clearly on any brick color — user asked for larger/more obvious
        // markers.
        if (meta && !meta->connections.isEmpty()) {
            // Only render markers for FREE connections — ones whose
            // linkedToId is empty on the brick instance. A connection
            // that's already glued to another brick isn't a snap
            // target, so showing a marker there just clutters the view.
            //
            // The active connection (brick.activeConnectionPointIndex)
            // gets a bigger, brighter gold marker so the user can see at
            // a glance which end will lead the next snap.
            constexpr double kDotRadiusPx       = 10.0;
            constexpr double kActiveRadiusPx    = 13.0;
            const int nConns    = meta->connections.size();
            const int activeIdx = brick.activeConnectionPointIndex;
            for (int ci = 0; ci < nConns; ++ci) {
                const auto& c = meta->connections[ci];
                if (c.type.isEmpty()) continue;
                // Only render dots for STANDARD connection types
                // (0=none, 1=rail, 2=road, 3=monorail standard, 4=monorail
                // short curve — per the BlueBrickParts XML comment on
                // 2865.8.xml). Custom library-specific types like
                // "bricktracks_lever" tag internal non-user-facing joints
                // (switch stand to switch), and sets often leave them
                // geometrically unpaired. Rendering them as free red
                // dots just clutters the view with markers the user
                // can't act on — vanilla BlueBrick hides them.
                {
                    bool ok;
                    c.type.toInt(&ok);
                    if (!ok) continue;
                }
                const bool linked =
                    ci < static_cast<int>(brick.connections.size())
                    && !brick.connections[ci].linkedToId.isEmpty();
                if (linked) continue;
                const bool isActive = (ci == activeIdx);
                const double r = isActive ? kActiveRadiusPx : kDotRadiusPx;
                const QPointF localPx(c.position.x() * kPx, c.position.y() * kPx);
                auto* dot = new QGraphicsEllipseItem(
                    localPx.x() - r, localPx.y() - r, r * 2, r * 2, item);
                const QColor fill = isActive
                    ? QColor(255, 215, 0)              // gold for active
                    : colorForConnectionType(c.type);   // type color (red) for other free
                QPen pen(isActive ? QColor(30, 30, 30) : QColor(255, 255, 255));
                pen.setWidthF(isActive ? 3.0 : 2.5);
                pen.setCosmetic(true);
                dot->setPen(pen);
                dot->setBrush(QBrush(fill));
                dot->setZValue(isActive ? 1001 : 1000);
                dot->setData(kBrickDataKind, QStringLiteral("connDot"));
                dot->setVisible(alwaysShowConns);
            }
        }

        // Hull outline — vanilla's DisplayHulls renders the brick's selection
        // polygon (we approximate with the displayArea rect for now). Drawn
        // as a sibling polygon in scene coords so it can't be occluded by
        // the parent pixmap. Picks up the layer's configured hull color +
        // thickness; falls back to a sensible default if never configured.
        if (displayHulls) {
            // Prefer the part's alpha-derived convex hull (tight fit
            // around the sprite's opaque pixels) when available; fall
            // back to the axis-aligned bounding rect for parts with no
            // pixmap. Rendered in scene coords so it rotates with the
            // brick's transform just like any child item.
            QGraphicsItem* hull = nullptr;
            QPolygonF hullStuds = lib.hullPolygonStuds(brick.partNumber);
            if (!hullStuds.isEmpty()) {
                QPolygonF hullPx;
                hullPx.reserve(hullStuds.size());
                for (const QPointF& p : hullStuds) {
                    hullPx.append(QPointF(p.x() * kPx, p.y() * kPx));
                }
                auto* poly = new QGraphicsPolygonItem(hullPx);
                poly->setBrush(Qt::NoBrush);
                // Attach to the brick item so the polygon rotates along
                // with it (transform inheritance). Local coords are
                // centered on the pixmap, matching the hull polygon's
                // reference frame.
                poly->setParentItem(item);
                hull = poly;
            } else {
                // A child of the part too, so it moves with a drag.
                auto* rect = new QGraphicsRectItem(item->mapRectFromScene(areaPx));
                rect->setBrush(Qt::NoBrush);
                rect->setParentItem(item);
                rect->setAcceptedMouseButtons(Qt::NoButton);
                hull = rect;
            }
            QPen p(L.hull.color.color.isValid() ? L.hull.color.color : QColor(0, 0, 0));
            p.setWidthF(L.hull.thickness > 0 ? L.hull.thickness : 1);
            p.setCosmetic(true);
            if (auto* asPoly = qgraphicsitem_cast<QGraphicsPolygonItem*>(hull)) asPoly->setPen(p);
            else if (auto* asRect = qgraphicsitem_cast<QGraphicsRectItem*>(hull)) asRect->setPen(p);
            hull->setZValue(brick.altitude + 0.5);
        }

        // Altitude label centered on the brick — matches vanilla's
        // DisplayBrickElevation (LayerBrick.cs ~line 806).
        if (displayElev && std::abs(brick.altitude) > 0.001f) {
            auto* alt = new QGraphicsSimpleTextItem(QString::number(brick.altitude, 'f', 1));
            QFont f(mapFontFamily());
            f.setPixelSize(10);
            alt->setFont(f);
            alt->setBrush(QBrush(QColor(40, 40, 40)));
            const QRectF bb = alt->boundingRect();
            // A child of the part (centred on it), so it moves with a drag.
            alt->setParentItem(item);
            alt->setAcceptedMouseButtons(Qt::NoButton);
            alt->setPos(item->mapFromScene(centerPx) - QPointF(bb.width() / 2.0, bb.height() / 2.0));
            alt->setZValue(brick.altitude + 0.6);
        }

        brickByGuid.insert(brick.guid, item);
        sink.add(item);
    }
}

void addTextLayer(const core::LayerText& L, LayerSink& sink, int layerIndex) {
    for (const auto& cell : L.textCells) {
        // The web's text (MapText.h): the bundled font, fitted into the
        // display area BlueBrick measured, lines aligned as the cell says.
        const TextCellLayout at = textCellLayout(cell, mapLineWidth(cell.font.styleString));
        const QFont f = mapFont(cell.font.styleString, at.fontPx);
        auto* t = new QGraphicsPathItem(textPath(f, at.lines, kMapLineHeight));
        t->setPen(Qt::NoPen);
        t->setBrush(QBrush(cell.fontColor.color));
        QTransform tr;
        tr.translate(at.centre.x(), at.centre.y());
        tr.rotate(at.rotation);
        tr.translate(-at.width / 2.0, -at.height / 2.0);
        t->setTransform(tr);
        t->setFlag(QGraphicsItem::ItemIsSelectable, true);
        t->setData(kBrickDataLayerIndex, layerIndex);
        t->setData(kBrickDataGuid,       cell.guid);
        t->setData(kBrickDataKind,       QStringLiteral("text"));
        sink.add(t);
    }
}

}  // namespace

QColor areaCellColor(const QColor& cell, int transparency) {
    QColor c = cell;
    c.setAlpha((255 * std::clamp(transparency, 0, 100)) / 100);
    return c;
}

namespace {

void addAreaLayer(const core::LayerArea& L, LayerSink& sink) {
    const double sizePx = studToPx(L.areaCellSizeInStud);
    // Vanilla BlueBrick draws a cell in its RGB at the sheet's alpha,
    // (255 * transparency) / 100 in whole numbers, replacing the cell's own
    // alpha (LayerArea.cs paintCell / AlphaValue); the web does the same.
    for (const auto& cell : L.cells) {
        auto* r = new QGraphicsRectItem(cell.x * sizePx, cell.y * sizePx, sizePx, sizePx);
        r->setPen(Qt::NoPen);
        const QColor c = areaCellColor(cell.color, L.transparency);
        r->setBrush(QBrush(c));
        sink.add(r);
    }
}

QString formatDistance(double studs, int unit) {
    // Upstream Tools/Distance.cs Unit enum:
    //   0 STUD, 1 LDU, 2 STRAIGHT_TRACK, 3 MODULE, 4 METER, 5 FEET.
    // 1 stud = 20 LDU = 1/16 track = 1/96 AFOL module = 8 mm = 0.026248 ft.
    switch (unit) {
        case 1:  return QStringLiteral("%1 LDU").arg(studs * 20.0, 0, 'f', 0);
        case 2:  return QStringLiteral("%1 tracks").arg(studs / 16.0, 0, 'f', 2);
        case 3:  return QStringLiteral("%1 mod").arg(studs / 96.0, 0, 'f', 2);
        case 4:  return QStringLiteral("%1 m").arg(studs * 0.008, 0, 'f', 3);
        case 5:  return QStringLiteral("%1 ft").arg(studs * 0.026248, 0, 'f', 2);
        default: return QStringLiteral("%1 studs").arg(studs, 0, 'f', 2);
    }
}

// Add an ephemeral text label (measurement readout) to the ruler group at
// the given scene-pixel position, rotated to `rotationDeg`, in the chosen
// font + color.
void addRulerLabel(LayerSink& sink, const QString& text,
                   QPointF scenePosPx, double rotationDeg,
                   const core::FontSpec& fontSpec, const core::ColorSpec& colorSpec) {
    if (text.isEmpty()) return;
    auto* t = new QGraphicsSimpleTextItem(text);
    QFont f(mapFontFamily());
    f.setBold(fontSpec.styleString.contains(QStringLiteral("Bold")));
    f.setItalic(fontSpec.styleString.contains(QStringLiteral("Italic")));
    f.setPixelSize(std::max(6, static_cast<int>(fontSpec.sizePt * 1.6)));
    t->setFont(f);
    t->setBrush(QBrush(colorSpec.color));
    const QRectF bb = t->boundingRect();
    QTransform tr;
    tr.translate(scenePosPx.x(), scenePosPx.y());
    tr.rotate(rotationDeg);
    tr.translate(-bb.width() / 2.0, -bb.height() - 4.0);  // sit just above the line
    t->setTransform(tr);
    sink.add(t);
}

void addRulerLayer(const core::LayerRuler& L, LayerSink& sink, int layerIndex,
                   const QHash<QString, QPointF>& brickCentreByGuid) {
    // Helper: if a ruler endpoint is attached to a brick, return that
    // brick's current world centre in stud coords; otherwise fall back
    // to the ruler's stored point. This is what makes attached rulers
    // track their bricks when the brick moves — we re-read the centre
    // every build() instead of trusting the stored point.
    auto resolveAnchor = [&](const QString& brickId, QPointF fallbackStud) -> QPointF {
        if (brickId.isEmpty()) return fallbackStud;
        auto it = brickCentreByGuid.constFind(brickId);
        if (it == brickCentreByGuid.constEnd()) return fallbackStud;
        return it.value();
    };

    for (const auto& any : L.rulers) {
        if (any.kind == core::RulerKind::Linear) {
            const auto& r = any.linear;
            // Resolve each endpoint: attached → brick centre, unattached → stored point.
            const QPointF p1Stud = resolveAnchor(r.attachedBrick1Id, r.point1);
            const QPointF p2Stud = resolveAnchor(r.attachedBrick2Id, r.point2);
            const QPointF anchor1(studToPx(p1Stud.x()), studToPx(p1Stud.y()));
            const QPointF anchor2(studToPx(p2Stud.x()), studToPx(p2Stud.y()));

            // The MAIN drawn line is between the *offsetted* points when
            // AllowOffset is on — vanilla moves the visible measure line
            // away from the anchors. When AllowOffset is off, offsetP == anchor.
            QPointF offsetP1 = anchor1, offsetP2 = anchor2;
            const QPointF dir = anchor2 - anchor1;
            const double  len = std::hypot(dir.x(), dir.y());
            // Perpendicular-offset vector: vanilla BlueBrick computes this
            // as (Uy, -Ux) where U is the unit vector along the ruler line
            // (LayerRulerItem.cs updateDisplayData line 877). Our previous
            // (-Uy, Ux) was opposite sign, so an OffsetDistance of -48 on a
            // left-to-right ruler moved the line upward instead of downward
            // and the red Nikki-area ruler ended up above the map.
            QPointF nrm;
            if (len > 0.001) nrm = QPointF(dir.y() / len, -dir.x() / len);
            const bool needOffset = r.allowOffset && std::abs(r.offsetDistance) > 0.001f && len > 0.001;
            if (needOffset) {
                const double off = studToPx(r.offsetDistance);
                offsetP1 += nrm * off;
                offsetP2 += nrm * off;
            }

            QPen mainPen(r.color.color);
            mainPen.setWidthF(r.lineThickness);
            mainPen.setCapStyle(Qt::FlatCap);

            // Measurement label + segmented main line (split around the text)
            // when displayDistance is on; otherwise one solid line.
            QGraphicsLineItem* selectableLine = nullptr;
            if (r.displayDistance) {
                // Distance uses the RESOLVED endpoints so attached rulers
                // show the current distance between their bricks, not the
                // frozen-at-creation distance between stored points.
                const QPointF delta = p2Stud - p1Stud;
                const double distStuds = std::hypot(delta.x(), delta.y());
                const QString text = r.displayUnit
                    ? formatDistance(distStuds, r.unit)
                    : QString::number(distStuds, 'f', 2);
                const QPointF midPx = (offsetP1 + offsetP2) / 2.0;

                // Keep the label readable: if the ruler's line angle would
                // rotate the text upside-down (90° < |angle| ≤ 180°), flip
                // by 180° so it stays right-reading. Upstream uses the same
                // "always readable" convention.
                double angleDeg = std::atan2(offsetP2.y() - offsetP1.y(),
                                              offsetP2.x() - offsetP1.x()) * 180.0 / M_PI;
                if (angleDeg > 90.0 || angleDeg < -90.0) angleDeg += 180.0;

                // Build label sized proportionally to the ruler's length.
                // Scaling to length (not to zoom) keeps the text legible
                // without ballooning at high zoom. Targets ~6% of the
                // ruler's pixel length for readout height, capped so a
                // huge ruler (like a 64-stud baseplate measurement)
                // doesn't get a heading-sized label.
                QFont f(mapFontFamily());
                f.setBold(r.measureFont.styleString.contains(QStringLiteral("Bold")));
                f.setItalic(r.measureFont.styleString.contains(QStringLiteral("Italic")));
                const double targetLabelPx = std::clamp(len * 0.06, 9.0, 36.0);
                f.setPixelSize(std::max(8, static_cast<int>(targetLabelPx)));
                QFontMetricsF fm(f);
                const double halfText = fm.horizontalAdvance(text) / 2.0 + 6.0;

                // Unit vector along the ruler line (in offset-line coords).
                QPointF unit(dir.x() / len, dir.y() / len);
                const QPointF mid1 = midPx - unit * halfText;
                const QPointF mid2 = midPx + unit * halfText;

                auto* seg1 = new QGraphicsLineItem(offsetP1.x(), offsetP1.y(), mid1.x(), mid1.y());
                auto* seg2 = new QGraphicsLineItem(offsetP2.x(), offsetP2.y(), mid2.x(), mid2.y());
                seg1->setPen(mainPen);
                seg2->setPen(mainPen);
                sink.add(seg1);
                sink.add(seg2);
                // Both segments AND the label tag with the same ruler guid
                // so clicking any piece selects the logical ruler — and
                // so refreshSelectionOverlay can union all pieces into one
                // highlight rather than outlining each segment separately.
                seg2->setFlag(QGraphicsItem::ItemIsSelectable, true);
                seg2->setFlag(QGraphicsItem::ItemIsMovable,    true);
                seg2->setData(kBrickDataLayerIndex, layerIndex);
                seg2->setData(kBrickDataGuid,       r.guid);
                seg2->setData(kBrickDataKind,       QStringLiteral("ruler"));
                selectableLine = seg1;

                // Label centred on midPx, rotated to match the (possibly
                // flipped) line angle so text always reads left-to-right.
                auto* t = new QGraphicsSimpleTextItem(text);
                t->setFont(f);
                t->setBrush(QBrush(r.measureFontColor.color));
                const QRectF bb = t->boundingRect();
                QTransform tr;
                tr.translate(midPx.x(), midPx.y());
                tr.rotate(angleDeg);
                tr.translate(-bb.width() / 2.0, -bb.height() / 2.0);
                t->setTransform(tr);
                t->setFlag(QGraphicsItem::ItemIsSelectable, true);
                t->setFlag(QGraphicsItem::ItemIsMovable,    true);
                t->setData(kBrickDataLayerIndex, layerIndex);
                t->setData(kBrickDataGuid,       r.guid);
                t->setData(kBrickDataKind,       QStringLiteral("ruler"));
                sink.add(t);
            } else {
                // Single solid line from offsetP1 to offsetP2.
                auto* line = new QGraphicsLineItem(offsetP1.x(), offsetP1.y(),
                                                    offsetP2.x(), offsetP2.y());
                line->setPen(mainPen);
                sink.add(line);
                selectableLine = line;
            }

            // Tag the first main-line segment as the ruler's selectable
            // proxy so right-click / double-click → Properties works.
            if (selectableLine) {
                selectableLine->setFlag(QGraphicsItem::ItemIsSelectable, true);
                selectableLine->setFlag(QGraphicsItem::ItemIsMovable,    true);
                selectableLine->setData(kBrickDataLayerIndex, layerIndex);
                selectableLine->setData(kBrickDataGuid,       r.guid);
                selectableLine->setData(kBrickDataKind,       QStringLiteral("ruler"));
                selectableLine->setData(SceneBuilder::kRulerBandRole, QLineF(offsetP1, offsetP2));
                selectableLine->setData(SceneBuilder::kRulerThicknessRole, static_cast<double>(r.lineThickness));
            }

            // Vanilla BlueBrick draws perpendicular tick caps on each end
            // of a ruler when the distance isn't shown AND the ruler is
            // shorter than 4 studs — without the caps, a very short
            // ruler reads as "just a short line" with no indication of
            // where it begins and ends. The caps are 4 studs long,
            // centred on each endpoint, perpendicular to the ruler
            // direction. Matches the `needToDrawArrowForSmallDistance`
            // branch of LayerRulerItem.cs::draw.
            const double lenStuds = std::hypot(p2Stud.x() - p1Stud.x(),
                                                p2Stud.y() - p1Stud.y());
            const bool drawEndCaps = !r.displayDistance && lenStuds < 4.0 && len > 0.001;
            if (drawEndCaps) {
                // Perpendicular unit vector (same sign as vanilla's
                // offsetNormalizedVector). Caps extend 4 studs in each
                // direction = 8 studs total width.
                const QPointF perp(dir.y() / len, -dir.x() / len);
                const double capPx = studToPx(4.0);
                QPen capPen(r.guidelineColor.color);
                capPen.setWidthF(std::max(0.5f, r.guidelineThickness));
                capPen.setCosmetic(true);
                QList<qreal> dashes;
                for (float d : r.guidelineDashPattern) if (d > 0) dashes.append(d);
                if (dashes.size() >= 2) capPen.setDashPattern(dashes);
                auto addCap = [&](QPointF p){
                    const QPointF a = p + perp * capPx;
                    const QPointF b = p - perp * capPx;
                    auto* cap = new QGraphicsLineItem(a.x(), a.y(), b.x(), b.y());
                    cap->setPen(capPen);
                    cap->setData(kBrickDataLayerIndex, layerIndex);
                    cap->setData(kBrickDataGuid,       r.guid);
                    cap->setData(kBrickDataKind,       QStringLiteral("ruler"));
                    sink.add(cap);
                };
                addCap(offsetP1);
                addCap(offsetP2);
            }

            // Dashed guidelines from the anchor points to the offset line's
            // matching endpoints — only when allowOffset actually moves the
            // line away from the anchors. Matches vanilla's penForGuideline.
            if (needOffset) {
                QPen gpen(r.guidelineColor.color);
                gpen.setWidthF(std::max(0.5f, r.guidelineThickness));
                gpen.setCosmetic(true);
                QList<qreal> dashes;
                for (float d : r.guidelineDashPattern) if (d > 0) dashes.append(d);
                if (dashes.size() >= 2) gpen.setDashPattern(dashes);
                else                     gpen.setStyle(Qt::DashLine);

                auto* g1 = new QGraphicsLineItem(anchor1.x(), anchor1.y(), offsetP1.x(), offsetP1.y());
                auto* g2 = new QGraphicsLineItem(anchor2.x(), anchor2.y(), offsetP2.x(), offsetP2.y());
                g1->setPen(gpen);
                g2->setPen(gpen);
                sink.add(g1);
                sink.add(g2);

                // Small filled circles at the actual anchor points so the
                // user can see where the ruler is anchored when it's been
                // offset away from the visible measure line. Colored to
                // match whatever the ruler is attached to: orange = free,
                // green = attached to a brick.
                constexpr double kAnchorRadiusPx = 4.0;
                auto addAnchorMarker = [&](QPointF p, bool attached) {
                    auto* dot = new QGraphicsEllipseItem(
                        p.x() - kAnchorRadiusPx, p.y() - kAnchorRadiusPx,
                        kAnchorRadiusPx * 2, kAnchorRadiusPx * 2);
                    QPen pen(QColor(40, 40, 40));
                    pen.setWidthF(1.5);
                    pen.setCosmetic(true);
                    dot->setPen(pen);
                    dot->setBrush(QBrush(attached ? QColor(30, 180, 60)
                                                   : QColor(240, 140, 30)));
                    dot->setData(kBrickDataLayerIndex, layerIndex);
                    dot->setData(kBrickDataGuid,       r.guid);
                    dot->setData(kBrickDataKind,       QStringLiteral("ruler"));
                    sink.add(dot);
                };
                addAnchorMarker(anchor1, !r.attachedBrick1Id.isEmpty());
                addAnchorMarker(anchor2, !r.attachedBrick2Id.isEmpty());
            }
        } else {
            const auto& r = any.circular;
            const double rPx = studToPx(r.radius);
            // Resolve centre: attached → brick centre; unattached → stored point.
            const QPointF cStud = resolveAnchor(r.attachedBrickId, r.center);
            const QPointF cPx(studToPx(cStud.x()), studToPx(cStud.y()));

            auto* el = new QGraphicsEllipseItem(
                cPx.x() - rPx, cPx.y() - rPx, 2 * rPx, 2 * rPx);
            QPen pen(r.color.color);
            pen.setWidthF(r.lineThickness);
            el->setPen(pen);
            el->setBrush(Qt::NoBrush);
            el->setFlag(QGraphicsItem::ItemIsSelectable, true);
            el->setFlag(QGraphicsItem::ItemIsMovable,    true);
            el->setData(kBrickDataLayerIndex, layerIndex);
            el->setData(kBrickDataGuid,       r.guid);
            el->setData(kBrickDataKind,       QStringLiteral("ruler"));
            el->setData(SceneBuilder::kRulerBandRole, el->rect());
            el->setData(SceneBuilder::kRulerThicknessRole, static_cast<double>(r.lineThickness));
            sink.add(el);

            // Radius label to the right of centre.
            if (r.displayDistance) {
                const QString text = r.displayUnit
                    ? formatDistance(r.radius, r.unit)
                    : QString::number(r.radius, 'f', 2);
                addRulerLabel(sink, text, QPointF(cPx.x() + rPx, cPx.y()), 0.0,
                              r.measureFont, r.measureFontColor);
            }
        }
    }
}

}

SceneBuilder::SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts)
    : scene_(scene), parts_(parts) {}

void SceneBuilder::clear() {
    // Items added since the event loop last ran wait in the scene's
    // "unpolished" list, and removing each one scans that list: clearing
    // right after a build (two rebuilds in one event-loop turn) went
    // quadratic, ~250 ms for 10 000 items. Polish them first (a queued
    // call on the scene) so removal stays linear.
    QCoreApplication::sendPostedEvents(&scene_, QEvent::MetaCall);
    for (auto& list : itemsByLayer_) {
        for (auto* it : list) { scene_.removeItem(it); delete it; }
    }
    itemsByLayer_.clear();
    brickByGuid_.clear();
    for (auto* it : venueItems_)        { scene_.removeItem(it); delete it; }
    for (auto* it : worldLabelItems_)   { scene_.removeItem(it); delete it; }
    for (auto* it : moduleLabelItems_)  { scene_.removeItem(it); delete it; }
    for (auto* it : electricItems_)     { scene_.removeItem(it); delete it; }
    venueItems_.clear();
    worldLabelItems_.clear();
    moduleLabelItems_.clear();
    moduleAnnotationRects_.clear();
    shortenedModuleNames_.clear();
    electricItems_.clear();
}

void SceneBuilder::build(const core::Map& map) {
    clear();
    // Precompute every brick's world centre in studs so attached rulers,
    // anchored labels, and any future cross-item feature can resolve the
    // partner position by guid without a linear scan.
    brickCentreByGuid_.clear();
    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& bl = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& b : bl.bricks) {
            // Rulers attach to the brick's pivot (its sprite centre).
            brickCentreByGuid_.insert(b.guid, parts::placement::imageCentre(b, parts_));
        }
    }
    addVenue(map);  // z = -100 so it sits beneath every layer
    for (size_t i = 0; i < map.layers().size(); ++i) {
        addLayer(*map.layers()[i], static_cast<int>(i));
    }
    addAnchoredLabels(map);
    addModuleLabels(map);
    addElectricCircuits(map);
}

void SceneBuilder::refreshModuleLabels(const core::Map& map) {
    for (auto* it : moduleLabelItems_) {
        scene_.removeItem(it);
        delete it;
    }
    moduleLabelItems_.clear();
    moduleAnnotationRects_.clear();
    shortenedModuleNames_.clear();
    addModuleLabels(map);
}

void SceneBuilder::rebuildFollowers(const core::Map& posed, const QSet<QString>& moving,
                                    const QSet<int>& keepRulerLayers) {
    for (auto* it : moduleLabelItems_) {
        scene_.removeItem(it);
        delete it;
    }
    for (auto* it : electricItems_) {
        scene_.removeItem(it);
        delete it;
    }
    moduleLabelItems_.clear();
    moduleAnnotationRects_.clear();
    shortenedModuleNames_.clear();
    electricItems_.clear();
    addModuleLabels(posed, true);
    addElectricCircuits(posed);

    // Rulers fixed to a moving part: their ends follow its pivot.
    for (const auto& layerPtr : posed.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layerPtr).bricks)
            if (moving.contains(b.guid))
                brickCentreByGuid_.insert(b.guid, parts::placement::imageCentre(b, parts_));
    }
    for (int i = 0; i < static_cast<int>(posed.layers().size()); ++i) {
        const auto& layerPtr = posed.layers()[static_cast<size_t>(i)];
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Ruler || keepRulerLayers.contains(i)) continue;
        bool fixed = false;
        for (const auto& any : static_cast<const core::LayerRuler&>(*layerPtr).rulers) {
            if (any.kind == core::RulerKind::Linear)
                fixed = fixed || moving.contains(any.linear.attachedBrick1Id)
                        || moving.contains(any.linear.attachedBrick2Id);
            else fixed = fixed || moving.contains(any.circular.attachedBrickId);
        }
        if (!fixed) continue;
        auto& list = itemsByLayer_[i];
        const bool shown = list.isEmpty() || list.front()->isVisible();
        for (auto* it : list) {
            scene_.removeItem(it);
            delete it;
        }
        list.clear();
        addLayer(*layerPtr, i);
        for (auto* it : itemsByLayer_[i]) it->setVisible(shown);
    }
}

void SceneBuilder::addLayer(const core::Layer& L, int layerIndex) {
    // Each per-layer item goes DIRECTLY into the scene — no parent group —
    // so hit-testing / selection / mouse events aren't intercepted by any
    // container. Per-layer ordering is preserved via baseZ in LayerSink.
    auto& list = itemsByLayer_[layerIndex];
    LayerSink sink{ scene_, list, static_cast<double>(layerIndex) * 1000.0, L.visible };
    const double opacity = std::clamp(L.transparency, 0, 100) / 100.0;

    switch (L.kind()) {
        case core::LayerKind::Grid:
            // Grid lines are painted by MapView::drawBackground, not as scene
            // items. Nothing to add here.
            break;
        case core::LayerKind::Brick:
            addBrickLayer(static_cast<const core::LayerBrick&>(L), sink, parts_, layerIndex, brickByGuid_);
            break;
        case core::LayerKind::Text:
            addTextLayer(static_cast<const core::LayerText&>(L), sink, layerIndex);
            break;
        case core::LayerKind::Area:
            addAreaLayer(static_cast<const core::LayerArea&>(L), sink);
            break;
        case core::LayerKind::Ruler:
            addRulerLayer(static_cast<const core::LayerRuler&>(L), sink, layerIndex,
                          brickCentreByGuid_);
            break;
        case core::LayerKind::AnchoredText:
            break;
    }

    // Apply per-layer transparency by scaling each item's opacity (a
    // painted area already has it in its colors).
    if (opacity < 1.0 && L.kind() != core::LayerKind::Area) {
        for (auto* it : list) it->setOpacity(opacity);
    }
}

bool SceneBuilder::setLayerVisible(int layerIndex, bool visible) {
    auto it = itemsByLayer_.find(layerIndex);
    if (it == itemsByLayer_.end()) return false;
    for (auto* item : *it) item->setVisible(visible);
    return true;
}

}
