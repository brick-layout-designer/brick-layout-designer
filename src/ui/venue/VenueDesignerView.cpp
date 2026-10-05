#include "VenueDesignerView.h"
#include "rendering/VenueLabels.h"

#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <cmath>

namespace bld::ui {

namespace ev = edit::venue;

namespace {
constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;
constexpr double kFt = 12 * ev::kStudsPerInch;
constexpr double kHitPx = 8.0;
const QColor kSelect(47, 111, 237);

// The venue with hidden layers taken out, for drawing.
core::Venue visibleVenue(core::Venue v, const std::array<bool, 6>& show) {
    const bool est = show[static_cast<size_t>(ev::Layer::Estimates)];
    v.enabled = true;
    if (!est) v.edges.removeIf([](const core::VenueEdge& e) { return e.estimated; });
    if (!show[static_cast<size_t>(ev::Layer::Obstacles)]) v.obstacles.clear();
    if (!show[static_cast<size_t>(ev::Layer::Power)]) v.power.clear();
    if (!show[static_cast<size_t>(ev::Layer::Notes)]) v.notes.clear();
    else if (!est) v.notes.removeIf([](const core::VenueNote& n) { return n.estimated; });
    if (!show[static_cast<size_t>(ev::Layer::Dimensions)]) v.dimensions.clear();
    else if (!est) v.dimensions.removeIf([](const core::VenueDimension& d) { return d.estimated; });
    return v;
}

QPen cosmetic(const QColor& c, double w, Qt::PenStyle style = Qt::SolidLine) {
    QPen p(c, w, style);
    p.setCosmetic(true);
    return p;
}
} // namespace

VenueDesignerView::VenueDesignerView(QWidget* parent)
    : QGraphicsView(parent), parts_(std::make_unique<parts::PartsLibrary>()),
      builder_(std::make_unique<rendering::SceneBuilder>(scene_, *parts_)) {
    setScene(&scene_);
    scene_.setSceneRect(-5e6, -5e6, 1e7, 1e7);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(Qt::white);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setFocusPolicy(Qt::NoFocus);
}

VenueDesignerView::~VenueDesignerView() {
    builder_.reset();
}

double VenueDesignerView::pixelsPerStud() const {
    return transform().m11() * kPx;
}
double VenueDesignerView::tolStuds() const {
    return kHitPx / pixelsPerStud();
}

QPointF VenueDesignerView::toStuds(QPoint viewPos) const {
    return mapToScene(viewPos) / kPx;
}

void VenueDesignerView::setState(const ev::DesignerState& s, const QImage& plan, QRectF planRect,
                                 double planOpacity) {
    last_ = { s, plan, planRect, planOpacity };
    core::Map map;
    map.sidecar.venue = visibleVenue(s.venue(), s.show);
    // Labels about 13 px on screen at any zoom, as on the web; the selected
    // wall's label whole, and every label clear of the selection handles.
    builder_->setVenueLabelPx(13.0 / transform().m11());
    {
        std::optional<int> edge;
        QVector<QPointF> grips;
        if (s.selection) {
            const auto sel = *s.selection;
            const core::Venue& sv = s.venue();
            if (sel.kind == ev::PartKind::Edge && sel.index < sv.edges.size()) {
                edge = static_cast<int>(sel.index);
                grips = sv.edges[sel.index].polyline;
            } else if (sel.kind == ev::PartKind::Obstacle && sel.index < sv.obstacles.size()) {
                grips = sv.obstacles[sel.index].polygon;
            } else if (sel.kind == ev::PartKind::Dimension && sel.index < sv.dimensions.size()) {
                grips = { sv.dimensions[sel.index].from, sv.dimensions[sel.index].to };
            }
        }
        builder_->setVenueSelection(edge, grips, 7.0 / transform().m11());
    }
    builder_->build(map); // clears its own items, including earlier overlays below

    // Everything else is ours: remove what the last call added.
    for (QGraphicsItem* it : scene_.items())
        if (it->data(0).toString() == QLatin1String("designer")) {
            scene_.removeItem(it);
            delete it;
        }
    const auto own = [this](QGraphicsItem* it, double z) {
        it->setData(0, QStringLiteral("designer"));
        it->setZValue(z);
        scene_.addItem(it);
    };
    const auto line = [&](QLineF l, const QColor& c, double w, Qt::PenStyle st = Qt::SolidLine) {
        auto* li = new QGraphicsLineItem(QLineF(l.p1() * kPx, l.p2() * kPx));
        li->setPen(cosmetic(c, w, st));
        own(li, 1e6);
    };

    if (!plan.isNull() && s.show[static_cast<size_t>(ev::Layer::Plan)]) {
        auto* pm = new QGraphicsPixmapItem(QPixmap::fromImage(plan));
        pm->setTransformationMode(Qt::SmoothTransformation);
        pm->setPos(planRect.topLeft() * kPx);
        pm->setScale(planRect.width() * kPx / plan.width());
        pm->setOpacity(planOpacity);
        own(pm, -1e7);
    }

    const core::Venue& v = s.venue();
    // The grid fades a little under the room: its walls end to end.
    {
        QPolygonF room;
        for (const auto& e : map.sidecar.venue->edges)
            for (const QPointF& p : e.polyline) room << p * kPx;
        if (room.size() >= 3) {
            auto* fade = new QGraphicsPolygonItem(room);
            fade->setPen(Qt::NoPen);
            fade->setBrush(rendering::kVenueGridFade);
            own(fade, -2e5);
        }
    }
    QVector<QPointF> handles;
    if (s.selection) {
        const auto sel = *s.selection;
        switch (sel.kind) {
        case ev::PartKind::Edge:
            if (sel.index < v.edges.size()) {
                const auto& poly = v.edges[sel.index].polyline;
                for (int i = 1; i < poly.size(); ++i) line({ poly[i - 1], poly[i] }, kSelect, 3);
                handles = poly;
            }
            break;
        case ev::PartKind::Obstacle:
            if (sel.index < v.obstacles.size()) {
                const auto& poly = v.obstacles[sel.index].polygon;
                for (int i = 0; i < poly.size(); ++i)
                    line({ poly[i], poly[(i + 1) % poly.size()] }, kSelect, 2);
                handles = poly;
            }
            break;
        case ev::PartKind::Dimension:
            if (sel.index < v.dimensions.size())
                handles = { v.dimensions[sel.index].from, v.dimensions[sel.index].to };
            break;
        case ev::PartKind::Power:
        case ev::PartKind::Note: {
            const QPointF c = sel.kind == ev::PartKind::Power
                                  ? (sel.index < v.power.size() ? v.power[sel.index].pos : QPointF())
                                  : (sel.index < v.notes.size() ? v.notes[sel.index].pos : QPointF());
            const double r = 10.0 / pixelsPerStud() * kPx;
            auto* ring = new QGraphicsEllipseItem(QRectF(c * kPx - QPointF(r, r), QSizeF(2 * r, 2 * r)));
            ring->setPen(cosmetic(kSelect, 2));
            own(ring, 1e6);
            break;
        }
        }
    }
    const double hs = 5.0 / transform().m11();
    for (const auto& h : handles) {
        auto* r = new QGraphicsRectItem(QRectF(h * kPx - QPointF(hs, hs), QSizeF(2 * hs, 2 * hs)));
        r->setPen(cosmetic(kSelect, 1.5));
        r->setBrush(Qt::white);
        own(r, 1e6 + 1);
    }

    if (s.tool == ev::Tool::Wall && s.draft.size() > 1)
        for (int i = 1; i < s.draft.size(); ++i) line({ s.draft[i - 1], s.draft[i] }, kSelect, 2);
    const auto pv = ev::preview(s);
    QString bubble;
    if (pv.line) {
        line(*pv.line, kSelect, 2, Qt::DashLine);
        bubble = QStringLiteral("%1 · %2").arg(ev::formatLength(pv.length, s.unit),
                                               ev::formatAngle(-pv.line->angle()));
    } else if (pv.rect) {
        auto* r = new QGraphicsRectItem(QRectF(pv.rect->topLeft() * kPx, pv.rect->size() * kPx));
        r->setPen(cosmetic(kSelect, 2, Qt::DashLine));
        own(r, 1e6);
        bubble = QStringLiteral("%1 × %2").arg(ev::formatLength(pv.rect->width(), s.unit),
                                               ev::formatLength(pv.rect->height(), s.unit));
    } else if (pv.cut) {
        line(*pv.cut, s.tool == ev::Tool::Door ? QColor(0, 160, 0) : QColor(0, 0, 200), 6);
        bubble = ev::formatLength(pv.length, s.unit);
    }
    for (const auto& c : s.calibration) {
        const double r = 6.0 / transform().m11();
        auto* dot = new QGraphicsEllipseItem(QRectF(c * kPx - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        dot->setBrush(QColor(217, 72, 15));
        dot->setPen(Qt::NoPen);
        own(dot, 1e6);
    }
    if (s.calibration.size() == 2) line({ s.calibration[0], s.calibration[1] }, QColor(217, 72, 15), 2);
    if (s.pointer && s.tool != ev::Tool::Select) {
        const QColor c = s.snapKind == ev::SnapKind::Corner ? QColor(217, 72, 15)
                         : s.snapKind == ev::SnapKind::Wall ? QColor(43, 138, 62)
                                                            : kSelect;
        const double r = 4.0 / transform().m11();
        auto* dot = new QGraphicsEllipseItem(QRectF(*s.pointer * kPx - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        dot->setPen(cosmetic(c, 2));
        own(dot, 1e6 + 2);
    }
    if (!bubble.isEmpty() && s.pointer) {
        if (!s.typed.isEmpty()) bubble += QStringLiteral(" · typing %1").arg(s.typed);
        auto* t = new QGraphicsSimpleTextItem(bubble);
        t->setBrush(Qt::white);
        t->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        auto* bg = new QGraphicsRectItem(t->boundingRect().adjusted(-8, -4, 8, 4));
        bg->setBrush(kSelect);
        bg->setPen(Qt::NoPen);
        bg->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        const QPointF at = *s.pointer * kPx + QPointF(14, -30) / transform().m11();
        bg->setPos(at);
        t->setPos(at);
        own(bg, 1e6 + 3);
        own(t, 1e6 + 4);
    }

    QVector<QPointF> pts;
    for (const auto& e : v.edges) pts += e.polyline;
    for (const auto& o : v.obstacles) pts += o.polygon;
    venueBounds_ = pts.isEmpty() ? QRectF(0, 0, 60 * kFt, 60 * kFt) : ev::bounds(pts);
    if (!fitted_ && width() > 0) fit();
}

void VenueDesignerView::zoomBy(double factor) {
    const double k = std::clamp(transform().m11() * factor, 0.002, 50.0) / transform().m11();
    scale(k, k);
    redraw();
}

void VenueDesignerView::fit() {
    fitted_ = true;
    const QRectF r(venueBounds_.topLeft() * kPx, venueBounds_.size() * kPx);
    // The room with 40 studs round it, plus about 50 px on each side for
    // the wall labels outside it (they keep their size on screen).
    const QRectF want = r.adjusted(-40 * kPx, -40 * kPx, 40 * kPx, 40 * kPx);
    const double vw = std::max(1, viewport()->width() - 100), vh = std::max(1, viewport()->height() - 100);
    const double k = std::min(vw / std::max(1e-9, want.width()), vh / std::max(1e-9, want.height()));
    setTransform(QTransform::fromScale(k, k));
    centerOn(want.center());
    redraw();
}

// Labels and handles are sized for the zoom: draw again after it changes.
void VenueDesignerView::redraw() {
    if (!last_ || redrawing_) return;
    redrawing_ = true;
    const auto l = *last_;
    setState(l.state, l.plan, l.planRect, l.planOpacity);
    redrawing_ = false;
}

void VenueDesignerView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panFrom_ = e->position().toPoint();
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (planMoving_) {
        planFrom_ = toStuds(e->position().toPoint());
        return;
    }
    emit pressed(toStuds(e->position().toPoint()), e->modifiers() & Qt::ShiftModifier);
}

void VenueDesignerView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    if (panFrom_) {
        const QPointF d = mapToScene(p) - mapToScene(*panFrom_);
        translate(d.x(), d.y());
        panFrom_ = p;
        return;
    }
    const QPointF w = toStuds(p);
    emit cursorMoved(w);
    if (planFrom_) {
        emit planDragged(w - *planFrom_);
        planFrom_ = w;
        return;
    }
    emit moved(w, e->modifiers() & Qt::ShiftModifier);
}

void VenueDesignerView::mouseReleaseEvent(QMouseEvent*) {
    if (panFrom_) panFrom_.reset();
    else if (planFrom_) planFrom_.reset();
    else emit released();
}

void VenueDesignerView::wheelEvent(QWheelEvent* e) {
    const QPointF before = mapToScene(e->position().toPoint());
    zoomBy(std::exp(e->angleDelta().y() * 0.0015));
    const QPointF after = mapToScene(e->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
}

void VenueDesignerView::leaveEvent(QEvent* e) {
    emit cursorMoved(std::nullopt);
    QGraphicsView::leaveEvent(e);
}

// Feet grid: 1 ft lines when at least 10 px apart, 10 ft lines always.
void VenueDesignerView::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, Qt::white);
    const double ftPx = kFt * kPx;
    const double step = pixelsPerStud() * kFt >= 10 ? ftPx : 10 * ftPx;
    if (rect.width() / step > 400) return;
    for (int major = 0; major < 2; ++major) {
        painter->setPen(cosmetic(major ? QColor(213, 218, 225) : QColor(238, 240, 243), 1));
        const long x0 = std::lround(std::floor(rect.left() / step)),
                   x1 = std::lround(std::ceil(rect.right() / step));
        for (long i = x0; i <= x1; ++i) {
            const double x = i * step;
            if ((std::lround(x / ftPx) % 10 == 0) != static_cast<bool>(major)) continue;
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        const long y0 = std::lround(std::floor(rect.top() / step)),
                   y1 = std::lround(std::ceil(rect.bottom() / step));
        for (long i = y0; i <= y1; ++i) {
            const double y = i * step;
            if ((std::lround(y / ftPx) % 10 == 0) != static_cast<bool>(major)) continue;
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
    }
}

} // namespace bld::ui
