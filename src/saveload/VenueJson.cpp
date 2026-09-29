#include "VenueJson.h"

#include <QJsonArray>

#include <array>
#include <cmath>
#include <initializer_list>

namespace bld::saveload {

namespace {

QJsonObject point(QPointF p) {
    return { { QStringLiteral("x"), p.x() }, { QStringLiteral("y"), p.y() } };
}
QPointF pointOf(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return { o.value(QLatin1String("x")).toDouble(), o.value(QLatin1String("y")).toDouble() };
}
QJsonArray points(const QVector<QPointF>& ps) {
    QJsonArray a;
    for (const auto& p : ps) a.append(point(p));
    return a;
}
QVector<QPointF> pointsOf(const QJsonValue& v) {
    QVector<QPointF> out;
    for (const auto& p : v.toArray()) out.append(pointOf(p));
    return out;
}

// The keys of `o` not in `known`.
QJsonObject extrasOf(const QJsonObject& o, std::initializer_list<const char*> known) {
    QJsonObject out = o;
    for (const char* k : known) out.remove(QLatin1String(k));
    return out;
}
// `o` with `extras` added under it (known keys win).
QJsonObject withExtras(QJsonObject o, const QJsonObject& extras) {
    for (auto it = extras.begin(); it != extras.end(); ++it)
        if (!o.contains(it.key())) o.insert(it.key(), it.value());
    return o;
}

constexpr std::array<const char*, 6> kObstacleKinds{
    "", "column", "stairs", "elevator", "counter", "railing"
};

double finiteOr(const QJsonValue& v, double dflt) {
    const double d = v.toDouble(dflt);
    return std::isfinite(d) ? d : dflt;
}

} // namespace

QJsonObject venueToJson(const core::Venue& v) {
    QJsonObject o{
        { QStringLiteral("name"), v.name },
        { QStringLiteral("enabled"), v.enabled },
        { QStringLiteral("minWalkwayStuds"), v.minWalkwayStuds },
        { QStringLiteral("bounds"), QJsonObject{ { QStringLiteral("x"), v.layoutBoundsStuds.x() },
                                                 { QStringLiteral("y"), v.layoutBoundsStuds.y() },
                                                 { QStringLiteral("w"), v.layoutBoundsStuds.width() },
                                                 { QStringLiteral("h"), v.layoutBoundsStuds.height() } } },
    };
    QJsonArray edges;
    for (const auto& e : v.edges) {
        QJsonObject eo{ { QStringLiteral("kind"), static_cast<int>(e.kind) },
                        { QStringLiteral("doorWidthStuds"), e.doorWidthStuds },
                        { QStringLiteral("label"), e.label },
                        { QStringLiteral("poly"), points(e.polyline) } };
        if (e.estimated) eo.insert(QStringLiteral("estimated"), true);
        edges.append(withExtras(eo, e.extras));
    }
    o.insert(QStringLiteral("edges"), edges);
    QJsonArray obstacles;
    for (const auto& ob : v.obstacles) {
        QJsonObject oo{ { QStringLiteral("label"), ob.label },
                        { QStringLiteral("poly"), points(ob.polygon) } };
        if (ob.kind != core::ObstacleKind::Other)
            oo.insert(QStringLiteral("kind"), QLatin1String(kObstacleKinds[static_cast<size_t>(ob.kind)]));
        if (ob.upDegrees) oo.insert(QStringLiteral("upDegrees"), *ob.upDegrees);
        obstacles.append(withExtras(oo, ob.extras));
    }
    o.insert(QStringLiteral("obstacles"), obstacles);
    if (!v.power.isEmpty()) {
        QJsonArray a;
        for (const auto& p : v.power) {
            QJsonObject po{ { QStringLiteral("x"), p.pos.x() },
                            { QStringLiteral("y"), p.pos.y() },
                            { QStringLiteral("kind"),
                              p.floor ? QStringLiteral("floor") : QStringLiteral("wall") } };
            if (!p.label.isEmpty()) po.insert(QStringLiteral("label"), p.label);
            if (p.amps > 0) po.insert(QStringLiteral("amps"), p.amps);
            if (p.volts > 0) po.insert(QStringLiteral("volts"), p.volts);
            a.append(withExtras(po, p.extras));
        }
        o.insert(QStringLiteral("power"), a);
    }
    if (!v.notes.isEmpty()) {
        QJsonArray a;
        for (const auto& n : v.notes) {
            QJsonObject no{ { QStringLiteral("x"), n.pos.x() },
                            { QStringLiteral("y"), n.pos.y() },
                            { QStringLiteral("text"), n.text } };
            if (n.estimated) no.insert(QStringLiteral("estimated"), true);
            a.append(withExtras(no, n.extras));
        }
        o.insert(QStringLiteral("notes"), a);
    }
    if (!v.dimensions.isEmpty()) {
        QJsonArray a;
        for (const auto& d : v.dimensions) {
            QJsonObject dobj{ { QStringLiteral("from"), point(d.from) },
                              { QStringLiteral("to"), point(d.to) } };
            if (!d.label.isEmpty()) dobj.insert(QStringLiteral("label"), d.label);
            if (d.estimated) dobj.insert(QStringLiteral("estimated"), true);
            a.append(withExtras(dobj, d.extras));
        }
        o.insert(QStringLiteral("dimensions"), a);
    }
    return withExtras(o, v.extras);
}

core::Venue venueFromJson(const QJsonObject& o) {
    core::Venue v;
    v.name = o.value(QLatin1String("name")).toString();
    v.enabled = o.value(QLatin1String("enabled")).toBool(true);
    v.minWalkwayStuds = finiteOr(o.value(QLatin1String("minWalkwayStuds")), v.minWalkwayStuds);
    const QJsonObject b = o.value(QLatin1String("bounds")).toObject();
    v.layoutBoundsStuds =
        QRectF(finiteOr(b.value(QLatin1String("x")), 0), finiteOr(b.value(QLatin1String("y")), 0),
               finiteOr(b.value(QLatin1String("w")), 0), finiteOr(b.value(QLatin1String("h")), 0));
    for (const auto& ev : o.value(QLatin1String("edges")).toArray()) {
        const QJsonObject eo = ev.toObject();
        core::VenueEdge e;
        const int k = eo.value(QLatin1String("kind")).toInt();
        e.kind = k == 1 ? core::EdgeKind::Door : k == 2 ? core::EdgeKind::Open : core::EdgeKind::Wall;
        e.doorWidthStuds = finiteOr(eo.value(QLatin1String("doorWidthStuds")), 0);
        e.label = eo.value(QLatin1String("label")).toString();
        e.polyline = pointsOf(eo.value(QLatin1String("poly")));
        e.estimated = eo.value(QLatin1String("estimated")).toBool(false);
        e.extras = extrasOf(eo, { "kind", "doorWidthStuds", "label", "poly", "estimated" });
        v.edges.append(e);
    }
    for (const auto& ov : o.value(QLatin1String("obstacles")).toArray()) {
        const QJsonObject oo = ov.toObject();
        core::VenueObstacle ob;
        ob.label = oo.value(QLatin1String("label")).toString();
        ob.polygon = pointsOf(oo.value(QLatin1String("poly")));
        const QString kind = oo.value(QLatin1String("kind")).toString();
        for (size_t i = 1; i < kObstacleKinds.size(); ++i)
            if (kind == QLatin1String(kObstacleKinds[i])) ob.kind = static_cast<core::ObstacleKind>(i);
        const QJsonValue up = oo.value(QLatin1String("upDegrees"));
        if (up.isDouble() && std::isfinite(up.toDouble())) ob.upDegrees = up.toDouble();
        ob.extras = extrasOf(oo, { "label", "poly", "kind", "upDegrees" });
        v.obstacles.append(ob);
    }
    for (const auto& pv : o.value(QLatin1String("power")).toArray()) {
        const QJsonObject po = pv.toObject();
        core::VenuePower p;
        p.pos = pointOf(po);
        p.floor = po.value(QLatin1String("kind")).toString() == QLatin1String("floor");
        p.label = po.value(QLatin1String("label")).toString();
        p.amps = std::max(0.0, finiteOr(po.value(QLatin1String("amps")), 0));
        p.volts = std::max(0.0, finiteOr(po.value(QLatin1String("volts")), 0));
        p.extras = extrasOf(po, { "x", "y", "kind", "label", "amps", "volts" });
        v.power.append(p);
    }
    for (const auto& nv : o.value(QLatin1String("notes")).toArray()) {
        const QJsonObject no = nv.toObject();
        core::VenueNote n;
        n.pos = pointOf(no);
        n.text = no.value(QLatin1String("text")).toString();
        n.estimated = no.value(QLatin1String("estimated")).toBool(false);
        n.extras = extrasOf(no, { "x", "y", "text", "estimated" });
        v.notes.append(n);
    }
    for (const auto& dv : o.value(QLatin1String("dimensions")).toArray()) {
        const QJsonObject dobj = dv.toObject();
        core::VenueDimension d;
        d.from = pointOf(dobj.value(QLatin1String("from")));
        d.to = pointOf(dobj.value(QLatin1String("to")));
        d.label = dobj.value(QLatin1String("label")).toString();
        d.estimated = dobj.value(QLatin1String("estimated")).toBool(false);
        d.extras = extrasOf(dobj, { "from", "to", "label", "estimated" });
        v.dimensions.append(d);
    }
    v.extras = extrasOf(o, { "schema", "name", "enabled", "minWalkwayStuds", "bounds", "edges", "obstacles",
                             "power", "notes", "dimensions" });
    return v;
}

} // namespace bld::saveload
