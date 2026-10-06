// Custom painting overlays drawn on top of the QGraphicsScene:
//   * drawBackground  — grid lines (driven by core::LayerGrid metadata)
//   * drawForeground  — scale indicator bar (always visible, follows zoom)
//   * refreshSelectionOverlay — rebuild the SelectionOverlay polygons from
//     the current scene selection (rulers union multi-segment highlights)
//
// Split out of MapView.cpp so the main translation unit focuses on
// interaction (mouse / key / drag pipeline) rather than painting.

#include "MapView.h"

#include "../core/Layer.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/Map.h"
#include "../rendering/MapText.h"
#include "../rendering/SceneBuilder.h"
#include "MapViewInternal.h"
#include "SelectionOverlay.h"
#include "SelectionStyle.h"
#include "TouchMode.h"

#include <QGraphicsDropShadowEffect>

#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QSet>

#include <cmath>
#include <limits>

namespace bld::ui {

using detail::kBrickDataGuid;
using detail::isRulerItem;
using detail::studToPx;

void MapView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    if (!map_) return;

    // Optional sidecar background image — painted under the grid + bricks.
    // Cached on the MapView so we re-load only when the path changes.
    if (!map_->sidecar.backgroundImagePath.isEmpty()) {
        if (cachedBackgroundPath_ != map_->sidecar.backgroundImagePath) {
            cachedBackgroundImage_ = QPixmap(map_->sidecar.backgroundImagePath);
            cachedBackgroundPath_ = map_->sidecar.backgroundImagePath;
        }
        if (!cachedBackgroundImage_.isNull()) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            QRectF dst;
            if (!map_->sidecar.backgroundImageRectStuds.isNull()) {
                const auto& r = map_->sidecar.backgroundImageRectStuds;
                dst = QRectF(r.x() * pxPerStud, r.y() * pxPerStud,
                             r.width() * pxPerStud, r.height() * pxPerStud);
            } else {
                // Default: anchor top-left at scene origin, scale to image's
                // native size (1 px = 1 px). User can re-scale by setting
                // backgroundImageRectStuds via the dialog.
                dst = QRectF(0, 0, cachedBackgroundImage_.width(),
                             cachedBackgroundImage_.height());
            }
            const double op = std::clamp(map_->sidecar.backgroundImageOpacity, 0.0, 1.0);
            painter->save();
            painter->setOpacity(op);
            painter->drawPixmap(dst, cachedBackgroundImage_,
                                QRectF(cachedBackgroundImage_.rect()));
            painter->restore();
        }
    }

    // A saved view being looked at may turn the grid off.
    const bool gridAllowed = !viewFilter_ || viewFilter_->grid;
    for (const auto& layer : map_->layers()) {
        if (!gridAllowed) break;
        if (layer->kind() != core::LayerKind::Grid || !layer->visible) continue;
        const auto& g = static_cast<const core::LayerGrid&>(*layer);
        if (!g.displayGrid && !g.displaySubGrid && !g.displayCellIndex) continue;

        const double gridPx = g.gridSizeInStud * rendering::SceneBuilder::kPixelsPerStud;
        const double subPx  = gridPx / std::max(g.subDivisionNumber, 2);

        const double left   = std::floor(rect.left()   / subPx) * subPx;
        const double right  = rect.right();
        const double top    = std::floor(rect.top()    / subPx) * subPx;
        const double bottom = rect.bottom();

        if (g.displaySubGrid) {
            QPen subPen(g.subGridColor.color);
            subPen.setCosmetic(true);
            subPen.setWidthF(g.subGridThickness);
            painter->setPen(subPen);
            for (double x = left; x < right; x += subPx)
                painter->drawLine(QPointF(x, top), QPointF(x, bottom));
            for (double y = top; y < bottom; y += subPx)
                painter->drawLine(QPointF(left, y), QPointF(right, y));
        }
        if (g.displayGrid) {
            QPen gridPen(g.gridColor.color);
            gridPen.setCosmetic(true);
            gridPen.setWidthF(g.gridThickness);
            painter->setPen(gridPen);
            const double firstX = std::floor(rect.left() / gridPx) * gridPx;
            const double firstY = std::floor(rect.top()  / gridPx) * gridPx;
            for (double x = firstX; x < right; x += gridPx)
                painter->drawLine(QPointF(x, top), QPointF(x, bottom));
            for (double y = firstY; y < bottom; y += gridPx)
                painter->drawLine(QPointF(left, y), QPointF(right, y));
        }
        if (g.displayCellIndex) drawCellIndices(painter, rect, g);
        break;
    }
}


// LayerGrid.draw's cell indices: column labels along the origin cell's row
// and row labels down its column, counting from the origin (which stays
// blank).
void MapView::drawCellIndices(QPainter* painter, const QRectF& rect, const core::LayerGrid& g) {
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    const double cellPx = std::max(1, g.gridSizeInStud) * px;
    QFont f(rendering::mapFontFamily());
    f.setPixelSize(std::max(1, static_cast<int>(std::lround(g.cellIndexFont.sizePt * 4.0 / 3.0 * px))));
    f.setBold(g.cellIndexFont.styleString.contains(QStringLiteral("Bold")));
    f.setItalic(g.cellIndexFont.styleString.contains(QStringLiteral("Italic")));
    painter->save();
    painter->setFont(f);
    painter->setPen(g.cellIndexColor.color);
    const QPoint c = g.cellIndexCorner;
    const auto label = [&](int cx, int cy, const QString& text) {
        if (text.isEmpty()) return;
        painter->drawText(QRectF(cx * cellPx, cy * cellPx, cellPx, cellPx), Qt::AlignCenter, text);
    };
    const int firstCol = std::max(c.x(), static_cast<int>(std::floor(rect.left() / cellPx)));
    const int lastCol  = static_cast<int>(std::ceil(rect.right() / cellPx));
    if ((c.y() + 1) * cellPx > rect.top() && c.y() * cellPx < rect.bottom())
        for (int x = firstCol; x <= lastCol; ++x)
            label(x, c.y(), core::LayerGrid::cellIndexLabel(x - c.x(), g.cellIndexColumnType == core::CellIndexType::Letters));
    const int firstRow = std::max(c.y(), static_cast<int>(std::floor(rect.top() / cellPx)));
    const int lastRow  = static_cast<int>(std::ceil(rect.bottom() / cellPx));
    if ((c.x() + 1) * cellPx > rect.left() && c.x() * cellPx < rect.right())
        for (int y = firstRow; y <= lastRow; ++y)
            label(c.x(), y, core::LayerGrid::cellIndexLabel(y - c.y(), g.cellIndexRowType == core::CellIndexType::Letters));
    painter->restore();
}

void MapView::drawForeground(QPainter* painter, const QRectF& rect) {
    // Edit module: the rest of the layout dimmed, the module outlined.
    paintModuleEdit(painter);
    QGraphicsView::drawForeground(painter, rect);
    paintLongPressRing(painter);
    paintBendHandles(painter);

    // Endpoint handles for any selected single linear ruler. drawn in
    // scene coords so they pin to the actual endpoints regardless of
    // pan/zoom. Hit-testing in mousePressEvent uses the same world->
    // viewport math to detect a click on a handle.
    if (map_) {
        const auto sel = scene()->selectedItems();
        QSet<QString> selRulerGuids;
        for (QGraphicsItem* it : sel) {
            if (!it) continue;
            if (it->data(2).toString() == QStringLiteral("ruler"))
                selRulerGuids.insert(it->data(1).toString());
        }
        if (selRulerGuids.size() == 1) {
            const QString g = *selRulerGuids.constBegin();
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            for (const auto& L : map_->layers()) {
                if (!L || L->kind() != core::LayerKind::Ruler) continue;
                const auto& RL = static_cast<const core::LayerRuler&>(*L);
                for (const auto& any : RL.rulers) {
                    if (any.kind != core::RulerKind::Linear) continue;
                    if (any.linear.guid != g) continue;
                    const QPointF p1Scene(any.linear.point1.x() * pxPerStud,
                                           any.linear.point1.y() * pxPerStud);
                    const QPointF p2Scene(any.linear.point2.x() * pxPerStud,
                                           any.linear.point2.y() * pxPerStud);
                    painter->save();
                    // Round gold handles, as on the web (SelectionStyle.h).
                    QPen pen(selection::kHandleStroke);
                    pen.setCosmetic(true); pen.setWidthF(selection::kHandleStrokeWidth);
                    painter->setPen(pen);
                    painter->setBrush(selection::kHandleFill);
                    painter->setRenderHint(QPainter::Antialiasing, true);
                    // Bigger under a finger: at least half a touch target across.
                    const double r = TouchMode::instance().active()
                        ? handleRadiusScenePx(TouchMode::kMinTarget / 2.0) : selection::kHandleRadius;
                    auto drawHandle = [painter, r](QPointF c) { painter->drawEllipse(c, r, r); };
                    drawHandle(p1Scene);
                    drawHandle(p2Scene);
                    painter->restore();
                    break;
                }
            }
        }
    }

    // Scale-indicator bar pinned to the lower-left viewport corner.
    // Picks a round stud value that renders somewhere near 100-160 px
    // wide so the label stays legible across zoom levels.
    const double pxPerStudScene = studToPx();
    const double vpPxPerScene = transform().m11();
    const double vpPxPerStud = pxPerStudScene * vpPxPerScene;
    if (vpPxPerStud <= 0.0) return;

    static constexpr double kNiceSteps[] = {
        1, 2, 5, 10, 16, 20, 32, 48, 64, 96, 128, 192, 256,
        384, 512, 768, 1024, 1536, 2048, 3072, 4096
    };
    double bestStuds = 32.0;
    double bestScore = std::numeric_limits<double>::max();
    for (double s : kNiceSteps) {
        const double pxWide = s * vpPxPerStud;
        if (pxWide < 40.0 || pxWide > 320.0) continue;
        const double dev = std::abs(pxWide - 120.0);
        if (dev < bestScore) { bestScore = dev; bestStuds = s; }
    }
    const double barPx = bestStuds * vpPxPerStud;
    // 1 stud = 8 mm per BlueBrick convention — show a human-friendly
    // secondary label in mm / m depending on magnitude.
    const double mm = bestStuds * 8.0;
    QString primary  = QStringLiteral("%1 studs").arg(bestStuds, 0, 'f', 0);
    QString secondary;
    if (mm >= 1000) secondary = QStringLiteral("%1 m").arg(mm / 1000.0, 0, 'f', 2);
    else            secondary = QStringLiteral("%1 mm").arg(mm, 0, 'f', 0);

    painter->save();
    painter->resetTransform();
    const QRectF pillRect(8, viewport()->height() - 44,
                          barPx + 60, 36);
    painter->setBrush(QColor(255, 255, 255, 200));
    painter->setPen(QPen(QColor(0, 0, 0, 120), 1));
    painter->drawRoundedRect(pillRect, 6, 6);
    QPen barPen(QColor(20, 20, 20));
    barPen.setWidthF(2.0);
    barPen.setCosmetic(true);
    painter->setPen(barPen);
    const double barY = viewport()->height() - 24;
    const double barX0 = 20;
    const double barX1 = 20 + barPx;
    painter->drawLine(QPointF(barX0, barY), QPointF(barX1, barY));
    painter->drawLine(QPointF(barX0, barY - 4), QPointF(barX0, barY + 4));
    painter->drawLine(QPointF(barX1, barY - 4), QPointF(barX1, barY + 4));
    QFont f = painter->font();
    f.setPixelSize(11);
    painter->setFont(f);
    painter->drawText(QPointF(barX0, barY - 8), primary);
    painter->drawText(QPointF(barX0, barY + 14), secondary);
    painter->restore();
    (void)rect;
}

void MapView::refreshSelectionOverlay() {
    if (!selectionOverlay_) return;
    QList<QPolygonF> polys;

    // A ruler can render as multiple scene items (two line segments + a
    // rotated text label when displayDistance is on). Tagging every piece
    // with the same guid lets us union their scene bounding rects into a
    // single highlight instead of drawing one box per segment.
    // As on the web (SelectionStyle.h): a ruler gets a band along its
    // line, text and labels glow (selection effects, set as the selection
    // changes), and everything else gets the outline 1 px outside it.
    QList<SelectionOverlay::RulerBand> bands;
    QSet<QString> rulerGuidsSeen;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!it || it == selectionOverlay_) continue;
        const QString kind = it->data(detail::kBrickDataKind).toString();
        if (kind == QLatin1String("text") || kind == QLatin1String("label")) continue;
        if (isRulerItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            if (!guid.isEmpty() && rulerGuidsSeen.contains(guid)) continue;
            rulerGuidsSeen.insert(guid);
            for (QGraphicsItem* any : scene()->items()) {
                if (!isRulerItem(any) || any->data(kBrickDataGuid).toString() != guid) continue;
                const QVariant geo = any->data(rendering::SceneBuilder::kRulerBandRole);
                if (!geo.isValid()) continue;
                SelectionOverlay::RulerBand band;
                band.width = selection::rulerHaloWidth(any->data(rendering::SceneBuilder::kRulerThicknessRole).toDouble());
                if (geo.typeId() == QMetaType::QLineF) {
                    band.line = geo.toLineF();
                } else {
                    const QRectF r = geo.toRectF();
                    band.circle = true;
                    band.centre = r.center();
                    band.radius = r.width() / 2;
                }
                bands.append(band);
                break;
            }
            continue;
        }
        const QRectF local = it->boundingRect();
        const bool localThin = (local.width() < 1.0 || local.height() < 1.0);
        QPolygonF poly;
        if (!localThin && !local.isEmpty()) {
            // The item's own rect, 1 scene px out (it may be scaled), turned with it.
            const double pad = selection::kPartPadPx / std::max(1e-9, it->scale());
            poly = it->mapToScene(local.adjusted(-pad, -pad, pad, pad));
        } else {
            // Thin items (lines, zero-height rect, etc.): scene-space
            // AABB inflated in whichever dimension is near zero.
            QRectF sbr = it->sceneBoundingRect();
            if (sbr.width()  < 2.0) sbr.adjust(-3.0, 0.0, 3.0, 0.0);
            if (sbr.height() < 2.0) sbr.adjust(0.0, -3.0, 0.0, 3.0);
            if (sbr.isEmpty()) sbr = QRectF(it->mapToScene(QPointF()) - QPointF(6, 6),
                                             QSizeF(12, 12));
            poly = QPolygonF(sbr);
        }
        polys.append(poly);
    }
    // Selected text and labels glow gold; the rest lose their glow.
    for (QGraphicsItem* it : scene()->items()) {
        const QString kind = it->data(detail::kBrickDataKind).toString();
        if (kind != QLatin1String("text") && kind != QLatin1String("label")) continue;
        const bool glow = it->isSelected();
        if (glow == (it->graphicsEffect() != nullptr)) continue;
        if (!glow) {
            it->setGraphicsEffect(nullptr);
            continue;
        }
        auto* effect = new QGraphicsDropShadowEffect();
        effect->setColor(selection::kTextGlow);
        effect->setBlurRadius(selection::kTextGlowBlur);
        effect->setOffset(0, 0);
        it->setGraphicsEffect(effect);
    }
    auto* ov = static_cast<SelectionOverlay*>(selectionOverlay_);
    // A selected module whose name was cut short shows its whole name.
    QList<SelectionOverlay::FullName> fullNames;
    if (map_ && !builder_->shortenedModuleNames().isEmpty()) {
        QSet<QString> selected;
        for (QGraphicsItem* it : scene()->selectedItems())
            if (detail::isBrickItem(it)) selected.insert(it->data(detail::kBrickDataGuid).toString());
        const rendering::NameWidthAt width = rendering::mapLineWidth(QStringLiteral("Bold"));
        for (const auto& s : builder_->shortenedModuleNames()) {
            const core::Module* mod = nullptr;
            for (const auto& m : map_->sidecar.modules)
                if (m.id == s.id) mod = &m;
            if (!mod || mod->memberIds.isEmpty()) continue;
            bool all = true;
            for (const QString& id : mod->memberIds) all = all && selected.contains(id);
            if (!all) continue;
            const auto pill = rendering::moduleFullNamePill(s.at, s.name, width);
            QTransform tr;
            tr.translate(s.at.textPos.x(), s.at.textPos.y());
            tr.rotate(s.at.rotation);
            fullNames.append({ tr, pill.box, pill.textTopLeft, s.name,
                               rendering::mapFont(QStringLiteral("Bold"), s.at.fontPx), s.fill });
        }
    }
    ov->setFullNames(std::move(fullNames));
    ov->setSnapState(liveSnapActive_, liveSnapPointScene_, liveSnapMovingScene_,
                     std::hypot(transform().m11(), transform().m12()));
    ov->setOutlines(std::move(polys));
    ov->setRulerBands(std::move(bands));
}

}  // namespace bld::ui
