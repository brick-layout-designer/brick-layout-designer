#include "WebModelWriter.h"

#include "saveload/SidecarIO.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <QJsonArray>

#include <iterator>

namespace bld::sync {

namespace {

QJsonObject color(const core::ColorSpec& c) {
    if (c.isKnown()) return { { QStringLiteral("kind"), QStringLiteral("known") }, { QStringLiteral("name"), c.knownName } };
    // Lowercase, 8 digits: what the .bbm writer emits (XmlPrimitives writeColor).
    return { { QStringLiteral("kind"), QStringLiteral("argb") },
             { QStringLiteral("argb"), QStringLiteral("%1").arg(c.color.rgba(), 8, 16, QLatin1Char('0')) } };
}

QJsonObject font(const core::FontSpec& f) {
    return { { QStringLiteral("family"), f.familyName }, { QStringLiteral("size"), static_cast<double>(f.sizePt) },
             { QStringLiteral("style"), f.styleString } };
}

QJsonObject rect(const QRectF& r) {
    return { { QStringLiteral("x"), r.x() }, { QStringLiteral("y"), r.y() }, { QStringLiteral("width"), r.width() },
             { QStringLiteral("height"), r.height() } };
}

QJsonObject point(QPointF p) { return { { QStringLiteral("x"), p.x() }, { QStringLiteral("y"), p.y() } }; }

void item(QJsonObject& o, const core::LayerItem& it) {
    o.insert(QStringLiteral("id"), it.guid);
    o.insert(QStringLiteral("displayArea"), rect(it.displayArea));
    o.insert(QStringLiteral("myGroup"), it.myGroupId);
}

QJsonArray groups(const std::vector<core::Group>& gs) {
    QJsonArray out;
    for (const auto& g : gs)
        out.append(QJsonObject{ { QStringLiteral("id"), g.guid }, { QStringLiteral("partNumber"), g.partNumber },
                                { QStringLiteral("myGroup"), g.myGroupId } });
    return out;
}

void rulerBase(QJsonObject& o, const core::RulerItemBase& r) {
    item(o, r);
    o.insert(QStringLiteral("color"), color(r.color));
    o.insert(QStringLiteral("lineThickness"), static_cast<double>(r.lineThickness));
    o.insert(QStringLiteral("displayDistance"), r.displayDistance);
    o.insert(QStringLiteral("displayUnit"), r.displayUnit);
    o.insert(QStringLiteral("guidelineColor"), color(r.guidelineColor));
    o.insert(QStringLiteral("guidelineThickness"), static_cast<double>(r.guidelineThickness));
    QJsonArray dash;
    for (float d : r.guidelineDashPattern) dash.append(static_cast<double>(d));
    o.insert(QStringLiteral("guidelineDashPattern"), dash);
    o.insert(QStringLiteral("unit"), r.unit);
    o.insert(QStringLiteral("measureFont"), font(r.measureFont));
    o.insert(QStringLiteral("measureFontColor"), color(r.measureFontColor));
}

// What a part's JSON is made from, and nothing else: equal fields, equal JSON.
PartJsonMemo::Fields fieldsOf(const core::Brick& b) {
    PartJsonMemo::Fields f;
    f.id = b.guid;
    f.group = b.myGroupId;
    f.partNumber = b.partNumber;
    f.x = b.displayArea.x();
    f.y = b.displayArea.y();
    f.width = b.displayArea.width();
    f.height = b.displayArea.height();
    f.orientation = b.orientation;
    f.altitude = b.altitude;
    f.activeConnection = b.activeConnectionPointIndex;
    f.links.reserve(b.connections.size());
    for (const auto& c : b.connections) f.links.emplace_back(c.guid, c.linkedToId);
    return f;
}

QJsonObject partJson(const PartJsonMemo::Fields& f) {
    QJsonArray links;
    for (const auto& [id, linkedTo] : f.links)
        links.append(QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("linkedTo"), linkedTo } });
    return {
        { QStringLiteral("id"), f.id },
        { QStringLiteral("displayArea"), QJsonObject{ { QStringLiteral("x"), f.x }, { QStringLiteral("y"), f.y },
                                                      { QStringLiteral("width"), f.width },
                                                      { QStringLiteral("height"), f.height } } },
        { QStringLiteral("myGroup"), f.group },
        { QStringLiteral("partNumber"), f.partNumber },
        { QStringLiteral("orientation"), static_cast<double>(f.orientation) },
        { QStringLiteral("activeConnectionPointIndex"), f.activeConnection },
        { QStringLiteral("altitude"), static_cast<double>(f.altitude) },
        { QStringLiteral("connexions"), links },
    };
}

QJsonObject layer(const core::Layer& l, PartJsonMemo* memo) {
    QJsonObject o{
        { QStringLiteral("id"), l.guid },
        { QStringLiteral("name"), l.name },
        { QStringLiteral("visible"), l.visible },
        { QStringLiteral("transparency"), l.transparency },
        { QStringLiteral("hullProperties"),
          QJsonObject{ { QStringLiteral("isVisible"), l.hull.displayHulls }, { QStringLiteral("hullColor"), color(l.hull.color) },
                       { QStringLiteral("hullThickness"), l.hull.thickness } } },
    };
    switch (l.kind()) {
    case core::LayerKind::Grid: {
        const auto& g = static_cast<const core::LayerGrid&>(l);
        o.insert(QStringLiteral("type"), QStringLiteral("grid"));
        o.insert(QStringLiteral("gridColor"), color(g.gridColor));
        o.insert(QStringLiteral("gridThickness"), static_cast<double>(g.gridThickness));
        o.insert(QStringLiteral("subGridColor"), color(g.subGridColor));
        o.insert(QStringLiteral("subGridThickness"), static_cast<double>(g.subGridThickness));
        o.insert(QStringLiteral("gridSizeInStud"), g.gridSizeInStud);
        o.insert(QStringLiteral("subDivisionNumber"), g.subDivisionNumber);
        o.insert(QStringLiteral("displayGrid"), g.displayGrid);
        o.insert(QStringLiteral("displaySubGrid"), g.displaySubGrid);
        o.insert(QStringLiteral("displayCellIndex"), g.displayCellIndex);
        o.insert(QStringLiteral("cellIndexFont"), font(g.cellIndexFont));
        o.insert(QStringLiteral("cellIndexColor"), color(g.cellIndexColor));
        o.insert(QStringLiteral("cellIndexColumnType"), QString::number(static_cast<int>(g.cellIndexColumnType)));
        o.insert(QStringLiteral("cellIndexRowType"), QString::number(static_cast<int>(g.cellIndexRowType)));
        o.insert(QStringLiteral("cellIndexCorner"), QJsonObject{ { QStringLiteral("x"), g.cellIndexCorner.x() }, { QStringLiteral("y"), g.cellIndexCorner.y() } });
        break;
    }
    case core::LayerKind::Brick: {
        const auto& b = static_cast<const core::LayerBrick&>(l);
        o.insert(QStringLiteral("type"), QStringLiteral("brick"));
        o.insert(QStringLiteral("displayBrickElevation"), b.displayBrickElevation);
        QJsonArray bricks;
        for (const auto& brick : b.bricks) {
            PartJsonMemo::Fields f = fieldsOf(brick);
            bricks.append(memo ? memo->json(std::move(f)) : partJson(f));
        }
        o.insert(QStringLiteral("bricks"), bricks);
        o.insert(QStringLiteral("groups"), groups(b.groups));
        break;
    }
    case core::LayerKind::Text: {
        const auto& t = static_cast<const core::LayerText&>(l);
        o.insert(QStringLiteral("type"), QStringLiteral("text"));
        QJsonArray cells;
        for (const auto& c : t.textCells) {
            QJsonObject co;
            item(co, c);
            co.insert(QStringLiteral("text"), c.text);
            co.insert(QStringLiteral("orientation"), static_cast<double>(c.orientation));
            co.insert(QStringLiteral("fontColor"), color(c.fontColor));
            co.insert(QStringLiteral("font"), font(c.font));
            co.insert(QStringLiteral("textAlignment"), c.alignment == core::TextAlignment::Near ? QStringLiteral("Near")
                                                     : c.alignment == core::TextAlignment::Far  ? QStringLiteral("Far")
                                                                                                : QStringLiteral("Center"));
            cells.append(co);
        }
        o.insert(QStringLiteral("textCells"), cells);
        o.insert(QStringLiteral("groups"), groups(t.groups));
        break;
    }
    case core::LayerKind::Area: {
        const auto& a = static_cast<const core::LayerArea&>(l);
        o.insert(QStringLiteral("type"), QStringLiteral("area"));
        o.insert(QStringLiteral("areaCellSize"), a.areaCellSizeInStud);
        QJsonArray cells;
        for (const auto& c : a.cells) {
            // Uppercase hex, as the .bbm stores it (LayerIO.cpp writeLayerArea).
            cells.append(QJsonObject{ { QStringLiteral("x"), c.x }, { QStringLiteral("y"), c.y },
                                      { QStringLiteral("color"), QStringLiteral("%1").arg(c.color.rgba(), 0, 16).toUpper() } });
        }
        o.insert(QStringLiteral("areas"), cells);
        break;
    }
    case core::LayerKind::Ruler: {
        const auto& r = static_cast<const core::LayerRuler&>(l);
        o.insert(QStringLiteral("type"), QStringLiteral("ruler"));
        QJsonArray rulers;
        for (const auto& any : r.rulers) {
            QJsonObject ro;
            if (any.kind == core::RulerKind::Circular) {
                rulerBase(ro, any.circular);
                ro.insert(QStringLiteral("kind"), QStringLiteral("circular"));
                ro.insert(QStringLiteral("center"), point(any.circular.center));
                ro.insert(QStringLiteral("radius"), static_cast<double>(any.circular.radius));
                ro.insert(QStringLiteral("attachedBrickId"), any.circular.attachedBrickId);
            } else {
                rulerBase(ro, any.linear);
                ro.insert(QStringLiteral("kind"), QStringLiteral("linear"));
                ro.insert(QStringLiteral("point1"), point(any.linear.point1));
                ro.insert(QStringLiteral("point2"), point(any.linear.point2));
                ro.insert(QStringLiteral("attachedBrick1Id"), any.linear.attachedBrick1Id);
                ro.insert(QStringLiteral("attachedBrick2Id"), any.linear.attachedBrick2Id);
                ro.insert(QStringLiteral("offsetDistance"), static_cast<double>(any.linear.offsetDistance));
                ro.insert(QStringLiteral("allowOffset"), any.linear.allowOffset);
            }
            rulers.append(ro);
        }
        o.insert(QStringLiteral("rulerItems"), rulers);
        o.insert(QStringLiteral("groups"), groups(r.groups));
        break;
    }
    default:
        break;
    }
    return o;
}

}  // namespace

bool PartJsonMemo::changed(const QString& id) const {
    const auto it = entries_.constFind(id);
    return it == entries_.constEnd() || it->seen != call_ || it->changed || it->repeated;
}

void PartJsonMemo::startCall() { ++call_; }

QJsonObject PartJsonMemo::json(Fields&& fields) {
    auto it = entries_.find(fields.id);
    if (it == entries_.end()) {
        const QString id = fields.id;
        Entry fresh;
        fresh.seen = call_;
        fresh.json = partJson(fields);
        fresh.fields = std::move(fields);
        return entries_.insert(id, std::move(fresh))->json;
    }
    Entry& e = *it;
    if (e.seen == call_) {  // a second part with this id
        e.repeated = true;
        e.changed = true;
        e.fields = std::move(fields);
        e.json = partJson(e.fields);
        return e.json;
    }
    const bool wasRepeated = e.repeated || e.seen + 1 != call_;  // (or not seen in the call before)
    e.seen = call_;
    e.repeated = false;
    e.changed = wasRepeated || !(e.fields == fields);
    if (e.changed) {
        e.fields = std::move(fields);
        e.json = partJson(e.fields);
    }
    return e.json;
}

void PartJsonMemo::endCall(qsizetype parts) {
    // Parts gone since: forgotten once they outnumber the ones there.
    if (entries_.size() <= 2 * parts + 64) return;
    for (auto it = entries_.begin(); it != entries_.end();)
        it = it->seen == call_ ? std::next(it) : entries_.erase(it);
}

QJsonObject docJsonFromMap(const core::Map& map, PartJsonMemo* memo) {
    if (memo) memo->startCall();
    const auto& ex = map.exportInfo;
    const QJsonObject meta{
        { QStringLiteral("version"), map.dataVersion },
        { QStringLiteral("nbItems"), map.nbItems },
        { QStringLiteral("backgroundColor"), color(map.backgroundColor) },
        { QStringLiteral("author"), map.author },
        { QStringLiteral("lug"), map.lug },
        { QStringLiteral("event"), map.event },
        { QStringLiteral("date"), QJsonObject{ { QStringLiteral("day"), map.date.day() }, { QStringLiteral("month"), map.date.month() },
                                               { QStringLiteral("year"), map.date.year() } } },
        { QStringLiteral("comment"), map.comment },
        { QStringLiteral("exportInfo"),
          QJsonObject{ { QStringLiteral("exportPath"), ex.exportPath }, { QStringLiteral("exportFileType"), ex.fileTypeIndex },
                       { QStringLiteral("exportArea"), rect(ex.area) }, { QStringLiteral("exportScale"), ex.scale },
                       { QStringLiteral("exportWatermark"), ex.watermark }, { QStringLiteral("exportElectricCircuit"), ex.electricCircuit },
                       { QStringLiteral("exportConnectionPoints"), ex.connectionPoints } } },
        { QStringLiteral("selectedLayerIndex"), map.selectedLayerIndex },
    };
    QJsonArray order;
    QJsonObject data;
    for (const auto& l : map.layers()) {
        if (l->kind() == core::LayerKind::AnchoredText) continue;  // sidecar data, not a doc layer
        order.append(l->guid);
        data.insert(l->guid, layer(*l, memo));
    }
    if (memo) {
        qsizetype parts = 0;
        for (const auto& l : map.layers())
            if (l->kind() == core::LayerKind::Brick) parts += static_cast<qsizetype>(static_cast<const core::LayerBrick&>(*l).bricks.size());
        memo->endCall(parts);
    }
    return { { QStringLiteral("meta"), meta }, { QStringLiteral("layers"), order }, { QStringLiteral("layerData"), data } };
}

QJsonObject mergeSidecarCache(const QJsonObject& current, const core::Sidecar& sidecar) {
    const QJsonObject mine = saveload::sidecarToJson(sidecar);
    QJsonObject out = current;
    for (const char* key : { "anchoredLabels", "modules", "views", "sets" }) {
        const QString k = QString::fromLatin1(key);
        const QJsonArray list = mine.value(k).toArray();
        if (!list.isEmpty()) out.insert(k, list);
        else if (!out.value(k).toArray().isEmpty()) out.insert(k, QJsonArray{});
    }
    if (mine.contains(QLatin1String("venue")))
        out.insert(QStringLiteral("venue"), mine.value(QLatin1String("venue")));
    else out.remove(QStringLiteral("venue"));
    return out;
}

}  // namespace bld::sync
