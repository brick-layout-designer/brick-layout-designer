#include "WebModel.h"

#include "saveload/SidecarIO.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <QJsonArray>

namespace bld::sync {

namespace {

using core::ColorSpec;

QString str(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toString(); }
double num(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toDouble(); }
float flt(const QJsonObject& o, const char* k) { return static_cast<float>(num(o, k)); }
int integer(const QJsonObject& o, const char* k) { return static_cast<int>(num(o, k)); }
bool boolean(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toBool(); }
QJsonObject obj(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toObject(); }
QJsonArray arr(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toArray(); }

// { kind: 'known', name } | { kind: 'argb', argb: 'ffd3d3d3' }, resolved like
// the .bbm reader (saveload/XmlPrimitives.cpp readColor).
ColorSpec color(const QJsonObject& c) {
    if (str(c, "kind") == QLatin1String("known")) {
        const QString name = str(c, "name");
        const QColor resolved(name);
        return ColorSpec::fromKnown(resolved.isValid() ? resolved : QColor(Qt::black), name);
    }
    bool ok = false;
    const auto argb = static_cast<quint32>(str(c, "argb").toLongLong(&ok, 16));
    return ok ? ColorSpec::fromArgb(QColor::fromRgba(argb)) : ColorSpec{};
}

core::FontSpec font(const QJsonObject& f) {
    core::FontSpec out;
    out.familyName = str(f, "family");
    out.sizePt = flt(f, "size");
    out.styleString = str(f, "style");
    return out;
}

QRectF rect(const QJsonObject& r) { return { num(r, "x"), num(r, "y"), num(r, "width"), num(r, "height") }; }
QPointF point(const QJsonObject& p) { return { num(p, "x"), num(p, "y") }; }

void item(core::LayerItem& it, const QJsonObject& o) {
    it.guid = str(o, "id");
    it.displayArea = rect(obj(o, "displayArea"));
    it.myGroupId = str(o, "myGroup");
}

std::vector<core::Group> groups(const QJsonObject& layer) {
    std::vector<core::Group> out;
    for (const auto& v : arr(layer, "groups")) {
        const QJsonObject g = v.toObject();
        core::Group group;
        group.guid = str(g, "id");
        group.partNumber = str(g, "partNumber");
        group.myGroupId = str(g, "myGroup");
        out.push_back(std::move(group));
    }
    return out;
}

void rulerBase(core::RulerItemBase& r, const QJsonObject& o) {
    item(r, o);
    r.color = color(obj(o, "color"));
    r.lineThickness = flt(o, "lineThickness");
    r.displayDistance = boolean(o, "displayDistance");
    r.displayUnit = boolean(o, "displayUnit");
    r.guidelineColor = color(obj(o, "guidelineColor"));
    r.guidelineThickness = flt(o, "guidelineThickness");
    for (const auto& d : arr(o, "guidelineDashPattern")) r.guidelineDashPattern.push_back(static_cast<float>(d.toDouble()));
    r.unit = integer(o, "unit");
    r.measureFont = font(obj(o, "measureFont"));
    r.measureFontColor = color(obj(o, "measureFontColor"));
}

std::unique_ptr<core::Layer> layer(const QJsonObject& o, QString* error) {
    const QString type = str(o, "type");
    std::unique_ptr<core::Layer> out;
    if (type == QLatin1String("grid")) {
        auto g = std::make_unique<core::LayerGrid>();
        g->gridColor = color(obj(o, "gridColor"));
        g->gridThickness = flt(o, "gridThickness");
        g->subGridColor = color(obj(o, "subGridColor"));
        g->subGridThickness = flt(o, "subGridThickness");
        g->gridSizeInStud = integer(o, "gridSizeInStud");
        g->subDivisionNumber = integer(o, "subDivisionNumber");
        g->displayGrid = boolean(o, "displayGrid");
        g->displaySubGrid = boolean(o, "displaySubGrid");
        g->displayCellIndex = boolean(o, "displayCellIndex");
        g->cellIndexFont = font(obj(o, "cellIndexFont"));
        g->cellIndexColor = color(obj(o, "cellIndexColor"));
        // Stored as the .bbm's integer text ("0" letters, "1" numbers).
        g->cellIndexColumnType = static_cast<core::CellIndexType>(str(o, "cellIndexColumnType").toInt());
        g->cellIndexRowType = static_cast<core::CellIndexType>(str(o, "cellIndexRowType").toInt());
        const QJsonObject corner = obj(o, "cellIndexCorner");
        g->cellIndexCorner = QPoint(integer(corner, "x"), integer(corner, "y"));
        out = std::move(g);
    } else if (type == QLatin1String("brick")) {
        auto l = std::make_unique<core::LayerBrick>();
        l->displayBrickElevation = boolean(o, "displayBrickElevation");
        for (const auto& v : arr(o, "bricks")) {
            const QJsonObject b = v.toObject();
            core::Brick brick;
            item(brick, b);
            brick.partNumber = str(b, "partNumber");
            brick.orientation = flt(b, "orientation");
            brick.activeConnectionPointIndex = integer(b, "activeConnectionPointIndex");
            brick.altitude = flt(b, "altitude");
            for (const auto& c : arr(b, "connexions")) {
                const QJsonObject cp = c.toObject();
                brick.connections.push_back({ str(cp, "id"), str(cp, "linkedTo") });
            }
            l->bricks.push_back(std::move(brick));
        }
        l->groups = groups(o);
        out = std::move(l);
    } else if (type == QLatin1String("text")) {
        auto l = std::make_unique<core::LayerText>();
        for (const auto& v : arr(o, "textCells")) {
            const QJsonObject t = v.toObject();
            core::TextCell cell;
            item(cell, t);
            // The cell keeps the doc's id (minted by the web when the .bbm had
            // none): it's what edits are matched by. .bbm files never carry it.
            cell.text = str(t, "text");
            cell.orientation = flt(t, "orientation");
            cell.fontColor = color(obj(t, "fontColor"));
            cell.font = font(obj(t, "font"));
            const QString a = str(t, "textAlignment");
            cell.alignment = a == QLatin1String("Near") ? core::TextAlignment::Near
                           : a == QLatin1String("Far")  ? core::TextAlignment::Far
                                                        : core::TextAlignment::Center;
            l->textCells.push_back(std::move(cell));
        }
        l->groups = groups(o);
        out = std::move(l);
    } else if (type == QLatin1String("area")) {
        auto l = std::make_unique<core::LayerArea>();
        l->areaCellSizeInStud = integer(o, "areaCellSize");
        for (const auto& v : arr(o, "areas")) {
            const QJsonObject a = v.toObject();
            core::AreaCell cell;
            cell.x = integer(a, "x");
            cell.y = integer(a, "y");
            // The .bbm's hex ARGB text, kept as is (LayerIO.cpp).
            cell.color = QColor::fromRgba(static_cast<quint32>(str(a, "color").toLongLong(nullptr, 16)));
            l->cells.push_back(cell);
        }
        out = std::move(l);
    } else if (type == QLatin1String("ruler")) {
        auto l = std::make_unique<core::LayerRuler>();
        for (const auto& v : arr(o, "rulerItems")) {
            const QJsonObject r = v.toObject();
            core::LayerRuler::AnyRuler any;
            if (str(r, "kind") == QLatin1String("circular")) {
                any.kind = core::RulerKind::Circular;
                rulerBase(any.circular, r);
                any.circular.center = point(obj(r, "center"));
                any.circular.radius = flt(r, "radius");
                any.circular.attachedBrickId = str(r, "attachedBrickId");
            } else {
                any.kind = core::RulerKind::Linear;
                rulerBase(any.linear, r);
                any.linear.point1 = point(obj(r, "point1"));
                any.linear.point2 = point(obj(r, "point2"));
                any.linear.attachedBrick1Id = str(r, "attachedBrick1Id");
                any.linear.attachedBrick2Id = str(r, "attachedBrick2Id");
                any.linear.offsetDistance = flt(r, "offsetDistance");
                any.linear.allowOffset = boolean(r, "allowOffset");
            }
            l->rulers.push_back(std::move(any));
        }
        l->groups = groups(o);
        out = std::move(l);
    } else {
        if (error) *error = QStringLiteral("unknown layer type \"%1\"").arg(type);
        return nullptr;
    }
    out->guid = str(o, "id");
    out->name = str(o, "name");
    out->visible = boolean(o, "visible");
    out->transparency = integer(o, "transparency");
    const QJsonObject hull = obj(o, "hullProperties");
    out->hull.displayHulls = boolean(hull, "isVisible");
    out->hull.color = color(obj(hull, "hullColor"));
    out->hull.thickness = integer(hull, "hullThickness");
    return out;
}

}  // namespace

std::unique_ptr<core::Map> mapFromDocJson(const QJsonObject& doc, QString* error) {
    const QJsonObject meta = obj(doc, "meta");
    if (!meta.contains(QLatin1String("version"))) {
        if (error) *error = QStringLiteral("the document has no layout header");
        return nullptr;
    }
    auto map = std::make_unique<core::Map>();
    map->dataVersion = integer(meta, "version");
    map->nbItems = integer(meta, "nbItems");
    map->backgroundColor = color(obj(meta, "backgroundColor"));
    map->author = str(meta, "author");
    map->lug = str(meta, "lug");
    map->event = str(meta, "event");
    const QJsonObject date = obj(meta, "date");
    map->date = QDate(integer(date, "year"), integer(date, "month"), integer(date, "day"));
    map->comment = str(meta, "comment");
    const QJsonObject ex = obj(meta, "exportInfo");
    map->exportInfo.exportPath = str(ex, "exportPath");
    map->exportInfo.fileTypeIndex = integer(ex, "exportFileType");
    map->exportInfo.area = rect(obj(ex, "exportArea"));
    map->exportInfo.scale = num(ex, "exportScale");
    map->exportInfo.watermark = boolean(ex, "exportWatermark");
    map->exportInfo.electricCircuit = boolean(ex, "exportElectricCircuit");
    map->exportInfo.connectionPoints = boolean(ex, "exportConnectionPoints");
    map->selectedLayerIndex = integer(meta, "selectedLayerIndex");
    // Anchored labels, modules and the venue: the web keeps the sidecar in
    // meta.cache.
    saveload::sidecarFromJson(obj(meta, "cache"), map->sidecar);

    const QJsonObject data = obj(doc, "layerData");
    for (const auto& id : arr(doc, "layers")) {
        const QJsonValue l = data.value(id.toString());
        if (!l.isObject()) continue;  // the web skips ids without data too
        auto out = layer(l.toObject(), error);
        if (!out) return nullptr;
        map->layers().push_back(std::move(out));
    }
    return map;
}

}  // namespace bld::sync
