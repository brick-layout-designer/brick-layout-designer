#include "VenueDesignerState.h"

#include <QRegularExpression>

#include <cmath>

namespace bld::edit::venue {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

std::optional<core::ObstacleKind> obstacleFor(Tool t) {
    switch (t) {
    case Tool::Column: return core::ObstacleKind::Column;
    case Tool::Stairs: return core::ObstacleKind::Stairs;
    case Tool::Elevator: return core::ObstacleKind::Elevator;
    case Tool::Counter: return core::ObstacleKind::Counter;
    default: return std::nullopt;
    }
}

bool isLineTool(Tool t) {
    return t == Tool::Wall || t == Tool::Railing || t == Tool::Measure;
}

DesignerState edited(DesignerState s, core::Venue next) {
    s.history = commit(std::move(s.history), std::move(next));
    s.message.clear();
    return s;
}

Snapped snapped(const DesignerState& s, QPointF at, double tol, bool free) {
    if (!s.snap) return { at, SnapKind::None };
    SnapOptions o;
    if (!s.draft.isEmpty() && isLineTool(s.tool)) o.from = s.draft.last();
    o.stepStuds = kDefaultStepStuds;
    o.angleStepDeg = free ? 0 : 45;
    o.tolStuds = tol;
    return snapPoint(s.venue(), at, o);
}

QPointF cornerFor(QPointF a, std::optional<QPointF> toward, double w, double d) {
    const double sx = toward && toward->x() < a.x() ? -1 : 1;
    const double sy = toward && toward->y() < a.y() ? -1 : 1;
    return { a.x() + sx * w, a.y() + sy * d };
}

QPointF snapDelta(QPointF d) {
    const auto r = [](double x) { return std::round(x / kDefaultStepStuds) * kDefaultStepStuds; };
    return { r(d.x()), r(d.y()) };
}

double snapT(const core::Venue& v, int edge, int seg, double t) {
    const auto& e = v.edges[edge];
    const double len = dist(e.polyline[seg], e.polyline[seg + 1]);
    if (len <= 0) return t;
    return std::clamp(std::round(t * len / kDefaultStepStuds) * kDefaultStepStuds / len, 0.0, 1.0);
}

std::optional<double> tOnCut(const DesignerState& s, QPointF p) {
    if (!s.cut || s.cut->edge >= s.venue().edges.size()) return std::nullopt;
    const auto& e = s.venue().edges[s.cut->edge];
    if (s.cut->seg + 1 >= e.polyline.size()) return std::nullopt;
    return segDist(p, e.polyline[s.cut->seg], e.polyline[s.cut->seg + 1]).t;
}

DesignerState finishTwoPoint(DesignerState s, QPointF a, QPointF b) {
    const core::Venue& v = s.venue();
    std::optional<core::Venue> next;
    std::optional<Selection> sel;
    if (s.tool == Tool::Room) {
        auto n = addRoom(v, a, b);
        if (n.edges.size() != v.edges.size()) next = std::move(n);
    } else if (const auto ob = obstacleFor(s.tool); ob || s.tool == Tool::Railing) {
        auto n = addObstacle(v, ob.value_or(core::ObstacleKind::Railing), a, b);
        if (n.obstacles.size() != v.obstacles.size()) {
            sel = Selection{ PartKind::Obstacle, static_cast<int>(n.obstacles.size()) - 1 };
            next = std::move(n);
        }
    } else if (s.tool == Tool::Measure) {
        auto n = addDimension(v, a, b, formatLength(dist(a, b), s.unit));
        if (n.dimensions.size() != v.dimensions.size()) {
            sel = Selection{ PartKind::Dimension, static_cast<int>(n.dimensions.size()) - 1 };
            next = std::move(n);
        }
    }
    s.draft.clear();
    s.typed.clear();
    s.selection = sel;
    return next ? edited(std::move(s), std::move(*next)) : s;
}

DesignerState finishCut(DesignerState s, double t1) {
    if (!s.cut) return s;
    const auto kind = s.tool == Tool::Door ? core::EdgeKind::Door : core::EdgeKind::Open;
    const auto c = *s.cut;
    auto next = cutOpening(s.venue(), c.edge, c.seg, c.t0, t1, kind,
                           kind == core::EdgeKind::Door ? QStringLiteral("door") : QStringLiteral("opening"));
    const bool changed =
        next.edges.size() != s.venue().edges.size() || next.edges[c.edge].kind != core::EdgeKind::Wall;
    s.cut.reset();
    s.typed.clear();
    if (!changed) return s;
    s.selection = Selection{ PartKind::Edge, c.edge + (std::min(c.t0, t1) > 0.0001 ? 1 : 0) };
    return edited(std::move(s), std::move(next));
}

} // namespace

const std::array<ToolInfo, 13>& tools() {
    static const std::array<ToolInfo, 13> kTools{ {
        { Tool::Select, "Select", 'v', "Click to select, drag to move, drag a corner to reshape" },
        { Tool::Wall, "Wall", 'w',
          "Click corner to corner; type a length and press Enter; Enter or Esc to finish" },
        { Tool::Room, "Room", 'r', "Click two opposite corners, or click one and type width x depth" },
        { Tool::Door, "Door", 'd',
          "Click where the door starts on a wall, then where it ends (or type its width)" },
        { Tool::Opening, "Opening", 'o',
          "Click where the opening starts on a wall, then where it ends (or type its width)" },
        { Tool::Column, "Column", 'c', "Click two opposite corners, or click one and type width x depth" },
        { Tool::Stairs, "Stairs", 's', "Click two opposite corners; set the way up in the inspector" },
        { Tool::Elevator, "Elevator", 'e', "Click two opposite corners" },
        { Tool::Counter, "Counter", 'k', "Click two opposite corners" },
        { Tool::Railing, "Railing", 'l', "Click where it starts and where it ends (or type its length)" },
        { Tool::Power, "Power", 'p', "Click on a wall for a wall outlet, anywhere else for a floor outlet" },
        { Tool::Note, "Note", 'n', "Click where the note goes, then type it in the inspector" },
        { Tool::Measure, "Measure", 'm', "Click two points to add a measurement (or type its length)" },
    } };
    return kTools;
}

DesignerState initialState(core::Venue v) {
    DesignerState s;
    s.history.present = std::move(v);
    return s;
}

std::optional<QVector<double>> parseSize(const QString& text, LengthUnit unit) {
    QStringList parts = text.split(
        QRegularExpression(QStringLiteral(R"(\s*[x×]\s*)"), QRegularExpression::CaseInsensitiveOption));
    parts.removeIf([](const QString& p) { return p.trimmed().isEmpty(); });
    if (parts.isEmpty() || parts.size() > 2) return std::nullopt;
    QVector<double> out;
    for (const auto& p : parts) {
        const auto v = parseLength(p, unit);
        if (!v || *v <= 0) return std::nullopt;
        out.append(*v);
    }
    return out;
}

DesignerState reduce(DesignerState s, const Action& action) {
    return std::visit(
        Overloaded{
            [&](const act::SetTool& a) {
                s.tool = a.tool;
                s.draft.clear();
                s.cut.reset();
                s.typed.clear();
                s.drag.reset();
                s.calibration.clear();
                s.message.clear();
                if (a.tool != Tool::Select) s.selection.reset();
                return s;
            },
            [&](const act::Move& a) {
                if (s.drag) {
                    auto& d = *s.drag;
                    core::Venue moved;
                    if (!d.vertex)
                        moved = movePart(d.base, d.sel, s.snap ? snapDelta(a.at - d.from) : a.at - d.from);
                    else {
                        QPointF target = a.at;
                        if (s.snap)
                            target =
                                snapPoint(d.base, a.at, { std::nullopt, kDefaultStepStuds, 0, a.tol, true })
                                    .pt;
                        if (d.sel.kind == PartKind::Edge)
                            moved = moveCorner(d.base, d.base.edges[d.sel.index].polyline[*d.vertex], target);
                        else moved = moveVertex(d.base, d.sel, *d.vertex, target);
                    }
                    s.history.present = std::move(moved);
                    d.moved = true;
                    s.pointer = a.at;
                    return s;
                }
                const auto sp = snapped(s, a.at, a.tol, a.free);
                s.pointer = sp.pt;
                s.snapKind = sp.kind;
                return s;
            },
            [&](const act::Down& a) {
                const auto sp = snapped(s, a.at, a.tol, a.free);
                const QPointF p = sp.pt;
                const core::Venue& v = s.venue();
                switch (s.tool) {
                case Tool::Select: {
                    const auto hit = hitTest(v, a.at, a.tol);
                    if (!hit) {
                        s.selection.reset();
                        return s;
                    }
                    s.selection = hit->sel;
                    s.drag = DesignerState::Drag{ hit->sel, hit->vertex, a.at, v, false };
                    return s;
                }
                case Tool::Wall: {
                    if (s.draft.isEmpty()) {
                        s.draft = { p };
                        s.typed.clear();
                        return s;
                    }
                    const QPointF last = s.draft.last();
                    if (dist(last, p) < 0.001) return s;
                    const bool closed = s.draft.size() > 1 && dist(p, s.draft.first()) < 0.001;
                    auto next = addEdge(v, { last, p });
                    if (closed) s.draft.clear();
                    else s.draft.append(p);
                    s.typed.clear();
                    return edited(std::move(s), std::move(next));
                }
                case Tool::Room:
                case Tool::Column:
                case Tool::Stairs:
                case Tool::Elevator:
                case Tool::Counter:
                case Tool::Railing:
                case Tool::Measure:
                    if (s.draft.isEmpty()) {
                        s.draft = { p };
                        s.typed.clear();
                        return s;
                    }
                    {
                        const QPointF first = s.draft.first();
                        return finishTwoPoint(std::move(s), first, p);
                    }
                case Tool::Door:
                case Tool::Opening: {
                    if (!s.cut) {
                        const auto hit = hitTest(v, a.at, a.tol);
                        if (!hit || hit->sel.kind != PartKind::Edge || !hit->seg
                            || v.edges[hit->sel.index].kind != core::EdgeKind::Wall) {
                            s.message = QStringLiteral("Click on a wall");
                            return s;
                        }
                        s.cut = DesignerState::Cut{ hit->sel.index, *hit->seg,
                                                    snapT(v, hit->sel.index, *hit->seg, hit->t) };
                        s.typed.clear();
                        s.message.clear();
                        return s;
                    }
                    const auto t = tOnCut(s, a.at);
                    if (!t) return s;
                    const double t1 = snapT(v, s.cut->edge, s.cut->seg, *t);
                    return finishCut(std::move(s), t1);
                }
                case Tool::Power: {
                    const bool onWall = sp.kind == SnapKind::Wall || sp.kind == SnapKind::Corner;
                    auto next = addPower(v, p, !onWall);
                    s.selection = Selection{ PartKind::Power, static_cast<int>(next.power.size()) - 1 };
                    return edited(std::move(s), std::move(next));
                }
                case Tool::Note: {
                    auto next = addNote(v, p, QStringLiteral("Note"));
                    s.selection = Selection{ PartKind::Note, static_cast<int>(next.notes.size()) - 1 };
                    return edited(std::move(s), std::move(next));
                }
                case Tool::Calibrate:
                    s.calibration.append(a.at);
                    if (s.calibration.size() > 2) s.calibration.remove(0, s.calibration.size() - 2);
                    return s;
                }
                return s;
            },
            [&](const act::Up&) {
                if (!s.drag) return s;
                core::Venue done = s.history.present;
                const bool moved = s.drag->moved;
                s.history.present = s.drag->base;
                s.drag.reset();
                return moved ? edited(std::move(s), std::move(done)) : s;
            },
            [&](const act::Type& a) {
                s.typed = a.text;
                s.message.clear();
                return s;
            },
            [&](const act::Enter&) {
                const QString typed = s.typed.trimmed();
                const core::Venue& v = s.venue();
                if (s.tool == Tool::Wall) {
                    if (typed.isEmpty()) {
                        s.draft.clear();
                        s.typed.clear();
                        return s;
                    }
                    const auto len = parseLength(typed, s.unit);
                    if (!len || *len <= 0 || s.draft.isEmpty()) {
                        s.message = QStringLiteral("Not a length: %1").arg(typed);
                        return s;
                    }
                    const QPointF last = s.draft.last();
                    const QPointF end =
                        pointAtLength(last, s.pointer.value_or(last + QPointF(1, 0)), *len, 45);
                    auto next = addEdge(v, { last, end });
                    s.draft.append(end);
                    s.typed.clear();
                    return edited(std::move(s), std::move(next));
                }
                if ((s.tool == Tool::Door || s.tool == Tool::Opening) && s.cut) {
                    const auto len = parseLength(typed, s.unit);
                    const auto& e = v.edges[s.cut->edge];
                    const double segLen = dist(e.polyline[s.cut->seg], e.polyline[s.cut->seg + 1]);
                    if (!len || *len <= 0 || segLen <= 0) {
                        s.message = QStringLiteral("Not a length: %1").arg(typed);
                        return s;
                    }
                    const auto tp = s.pointer ? tOnCut(s, *s.pointer) : std::nullopt;
                    const double dir = tp && *tp < s.cut->t0 ? -1 : 1;
                    const double t1 = std::clamp(s.cut->t0 + dir * *len / segLen, 0.0, 1.0);
                    return finishCut(std::move(s), t1);
                }
                if (s.draft.size() == 1 && !typed.isEmpty()) {
                    const auto size = parseSize(typed, s.unit);
                    if (!size) {
                        s.message = QStringLiteral("Not a size: %1").arg(typed);
                        return s;
                    }
                    const QPointF a0 = s.draft.first();
                    QPointF b0;
                    if (s.tool == Tool::Railing || s.tool == Tool::Measure)
                        b0 = pointAtLength(a0, s.pointer.value_or(a0 + QPointF(1, 0)), (*size)[0], 45);
                    else
                        b0 = cornerFor(a0, s.pointer, (*size)[0], size->size() > 1 ? (*size)[1] : (*size)[0]);
                    return finishTwoPoint(std::move(s), a0, b0);
                }
                return s;
            },
            [&](const act::Escape&) {
                if (s.drag) {
                    s.history.present = s.drag->base;
                    s.drag.reset();
                    return s;
                }
                if (!s.draft.isEmpty() || s.cut || !s.typed.isEmpty() || !s.calibration.isEmpty()) {
                    s.draft.clear();
                    s.cut.reset();
                    s.typed.clear();
                    s.calibration.clear();
                    s.message.clear();
                    return s;
                }
                if (s.tool != Tool::Select) return reduce(std::move(s), act::SetTool{ Tool::Select });
                s.selection.reset();
                return s;
            },
            [&](const act::Delete&) {
                if (!s.selection) return s;
                auto next = deletePart(s.venue(), *s.selection);
                s.selection.reset();
                return edited(std::move(s), std::move(next));
            },
            [&](const act::Duplicate&) {
                if (!s.selection) return s;
                auto [next, copy] = duplicatePart(s.venue(), *s.selection);
                s.selection = copy;
                return edited(std::move(s), std::move(next));
            },
            [&](const act::Undo&) {
                s.history = undo(std::move(s.history));
                s.selection.reset();
                s.draft.clear();
                s.cut.reset();
                return s;
            },
            [&](const act::Redo&) {
                s.history = redo(std::move(s.history));
                s.selection.reset();
                s.draft.clear();
                s.cut.reset();
                return s;
            },
            [&](const act::Select& a) {
                s.selection = a.selection;
                return s;
            },
            [&](const act::Edit& a) { return edited(std::move(s), a.venue); },
            [&](const act::Unit& a) {
                s.unit = a.unit;
                return s;
            },
            [&](const act::Snap& a) {
                s.snap = a.on;
                return s;
            },
            [&](const act::Show& a) {
                s.show[static_cast<size_t>(a.layer)] = a.on;
                return s;
            },
        },
        action);
}

Preview preview(const DesignerState& s) {
    Preview out;
    if (!s.pointer) return out;
    const QPointF p = *s.pointer;
    if (s.cut) {
        const auto& e = s.venue().edges[s.cut->edge];
        const auto t = tOnCut(s, p);
        if (!t) return out;
        const QPointF a = e.polyline[s.cut->seg], b = e.polyline[s.cut->seg + 1];
        const QLineF l(a + (b - a) * s.cut->t0, a + (b - a) * *t);
        out.cut = l;
        out.length = l.length();
        return out;
    }
    if (s.draft.isEmpty()) return out;
    const QPointF a = s.draft.last();
    if (isLineTool(s.tool)) {
        out.line = QLineF(a, p);
        out.length = dist(a, p);
    } else {
        out.rect = QRectF(a, p).normalized();
    }
    return out;
}

} // namespace bld::edit::venue
