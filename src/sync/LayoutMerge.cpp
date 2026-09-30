#include "LayoutMerge.h"

#include "WebModel.h"
#include "WebModelWriter.h"

#include "../core/Map.h"
#include "../saveload/SidecarIO.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QUuid>

#include <cmath>

namespace bld::sync::merge {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("bld::sync::merge", s); }

const QStringList kItemLists{ QStringLiteral("bricks"), QStringLiteral("groups"), QStringLiteral("textCells"),
                              QStringLiteral("areas"), QStringLiteral("rulerItems") };
const QStringList kHeader{ QStringLiteral("author"), QStringLiteral("lug"), QStringLiteral("event"),
                           QStringLiteral("date"), QStringLiteral("comment"), QStringLiteral("backgroundColor") };

struct Item {
    QString kind, layerId;
    QJsonValue value;
    QString layerName;
};
using Items = QMap<QString, Item>;

QString rulerHash(QJsonObject ruler) {
    ruler.remove(QStringLiteral("id"));
    // QJsonObject keeps its keys sorted, so equal rulers give equal bytes.
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(ruler).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex()
            .left(16));
}

QString textKey(const QString& layer, const QJsonObject& cell, int index) {
    const QString id = cell.value(QLatin1String("id")).toString();
    return QStringLiteral("text:%1:%2").arg(layer, id.isEmpty() ? QStringLiteral("#%1").arg(index) : id);
}

Items itemsOf(const Snapshot& s) {
    Items out;
    const QJsonObject meta = s.doc.value(QLatin1String("meta")).toObject();
    QJsonObject header;
    for (const auto& k : kHeader) header.insert(k, meta.value(k));
    out.insert(QStringLiteral("map"), { QStringLiteral("map"), {}, header });
    const QJsonObject data = s.doc.value(QLatin1String("layerData")).toObject();
    for (const auto& lv : s.doc.value(QLatin1String("layers")).toArray()) {
        const QString lid = lv.toString();
        QJsonObject props = data.value(lid).toObject();
        const QJsonObject layer = props;
        const QString name = layer.value(QLatin1String("name")).toString();
        for (const auto& k : kItemLists) props.remove(k);
        out.insert(QStringLiteral("layer:") + lid, { QStringLiteral("layer"), lid, props, name });
        for (const auto& g : layer.value(QLatin1String("groups")).toArray())
            out.insert(QStringLiteral("group:%1:%2").arg(lid, g.toObject().value(QLatin1String("id")).toString()),
                       { QStringLiteral("group"), lid, g, name });
        for (const auto& b : layer.value(QLatin1String("bricks")).toArray()) {
            QJsonObject brick = b.toObject();
            brick.remove(QStringLiteral("connexions"));  // links follow positions: noise
            out.insert(QStringLiteral("brick:%1:%2").arg(lid, brick.value(QLatin1String("id")).toString()),
                       { QStringLiteral("brick"), lid, brick, name });
        }
        const QJsonArray cells = layer.value(QLatin1String("textCells")).toArray();
        for (int i = 0; i < cells.size(); ++i)
            out.insert(textKey(lid, cells[i].toObject(), i), { QStringLiteral("text"), lid, cells[i], name });
        for (const auto& a : layer.value(QLatin1String("areas")).toArray()) {
            const QJsonObject cell = a.toObject();
            out.insert(QStringLiteral("area:%1:%2,%3")
                           .arg(lid)
                           .arg(cell.value(QLatin1String("x")).toInt())
                           .arg(cell.value(QLatin1String("y")).toInt()),
                       { QStringLiteral("area"), lid, cell.value(QLatin1String("color")), name });
        }
        for (const auto& r : layer.value(QLatin1String("rulerItems")).toArray()) {
            QJsonObject ruler = r.toObject();
            ruler.remove(QStringLiteral("id"));
            out.insert(QStringLiteral("ruler:%1:%2").arg(lid, rulerHash(ruler)), { QStringLiteral("ruler"), lid, ruler, name });
        }
    }
    for (const auto& l : s.sidecar.value(QLatin1String("anchoredLabels")).toArray())
        out.insert(QStringLiteral("label:") + l.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("label"), {}, l });
    for (const auto& m : s.sidecar.value(QLatin1String("modules")).toArray())
        out.insert(QStringLiteral("module:") + m.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("module"), {}, m });
    if (s.sidecar.contains(QLatin1String("venue")))
        out.insert(QStringLiteral("venue"), { QStringLiteral("venue"), {}, s.sidecar.value(QLatin1String("venue")) });
    if (s.sidecar.contains(QLatin1String("backgroundImage")))
        out.insert(QStringLiteral("background"),
                   { QStringLiteral("background"), {}, s.sidecar.value(QLatin1String("backgroundImage")) });
    return out;
}

Side sideChange(const Item* base, const Item* side) {
    if (!base && !side) return Side::Unchanged;
    if (!base) return Side::Added;
    if (!side) return Side::Deleted;
    return base->value == side->value ? Side::Unchanged : Side::Edited;
}

int indexOf(const QJsonArray& list, const QString& value) {
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toString() == value) return i;
    return -1;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// The item list of a layer that holds `kind`.
QString listOf(const QString& kind) {
    if (kind == QLatin1String("brick")) return QStringLiteral("bricks");
    if (kind == QLatin1String("group")) return QStringLiteral("groups");
    if (kind == QLatin1String("text")) return QStringLiteral("textCells");
    if (kind == QLatin1String("area")) return QStringLiteral("areas");
    return QStringLiteral("rulerItems");
}

// The index in `list` of the item `key` names, or -1.
int findItem(const QJsonArray& list, const QString& kind, const QString& key) {
    const QString last = key.section(QLatin1Char(':'), 2);
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject o = list[i].toObject();
        if (kind == QLatin1String("area")) {
            if (QStringLiteral("%1,%2").arg(o.value(QLatin1String("x")).toInt()).arg(o.value(QLatin1String("y")).toInt()) == last)
                return i;
        } else if (kind == QLatin1String("ruler")) {
            if (rulerHash(o) == last) return i;
        } else if (kind == QLatin1String("text") && last.startsWith(QLatin1Char('#'))) {
            if (i == last.mid(1).toInt() && o.value(QLatin1String("id")).toString().isEmpty()) return i;
        } else if (o.value(QLatin1String("id")).toString() == last) {
            return i;
        }
    }
    return -1;
}

class Merger {
public:
    Merger(const Snapshot& mine, const Snapshot& server) : mine_(mine), doc_(server.doc), sidecar_(server.sidecar) {}

    void take(const ItemChange& c, Choice choice) {
        if (choice == Choice::Server) return;
        const bool copy = choice == Choice::Both && c.allowsBoth();
        if (c.kind == QLatin1String("map")) return takeHeader(c);
        if (c.kind == QLatin1String("layer")) return takeLayer(c);
        if (c.kind == QLatin1String("label") || c.kind == QLatin1String("module"))
            return takeSidecarItem(c, c.kind == QLatin1String("label") ? QStringLiteral("anchoredLabels")
                                                                       : QStringLiteral("modules"),
                                   copy);
        if (c.kind == QLatin1String("venue") || c.kind == QLatin1String("background")) {
            const QString k = c.kind == QLatin1String("venue") ? QStringLiteral("venue") : QStringLiteral("backgroundImage");
            if (c.mineValue.isUndefined()) sidecar_.remove(k);
            else sidecar_.insert(k, mine_.sidecar.value(k));
            return;
        }
        takeLayerItem(c, copy);
    }

    Snapshot result() const { return { doc_, sidecar_ }; }

private:
    QJsonObject data() const { return doc_.value(QLatin1String("layerData")).toObject(); }
    void setData(const QJsonObject& d) { doc_.insert(QStringLiteral("layerData"), d); }

    void takeHeader(const ItemChange& c) {
        QJsonObject meta = doc_.value(QLatin1String("meta")).toObject();
        const QJsonObject mine = c.mineValue.toObject();
        for (const auto& k : kHeader) meta.insert(k, mine.value(k));
        doc_.insert(QStringLiteral("meta"), meta);
    }

    // Makes sure the layer is there, taking mine's properties when it isn't.
    void ensureLayer(const QString& lid) {
        QJsonObject d = data();
        if (d.contains(lid)) return;
        QJsonObject layer = mine_.doc.value(QLatin1String("layerData")).toObject().value(lid).toObject();
        for (const auto& k : kItemLists)
            if (layer.contains(k)) layer.insert(k, QJsonArray{});
        d.insert(lid, layer);
        setData(d);
        // Where mine has it: after the nearest layer before it that the server has.
        QJsonArray order = doc_.value(QLatin1String("layers")).toArray();
        const QJsonArray mineOrder = mine_.doc.value(QLatin1String("layers")).toArray();
        int at = 0;
        for (int i = indexOf(mineOrder, lid) - 1; i >= 0; --i) {
            const int prev = indexOf(order, mineOrder[i].toString());
            if (prev >= 0) {
                at = prev + 1;
                break;
            }
        }
        order.insert(at, lid);
        doc_.insert(QStringLiteral("layers"), order);
    }

    void takeLayer(const ItemChange& c) {
        if (c.mineValue.isUndefined()) {
            QJsonObject d = data();
            d.remove(c.layerId);
            setData(d);
            QJsonArray order = doc_.value(QLatin1String("layers")).toArray();
            for (int i = order.size() - 1; i >= 0; --i)
                if (order[i].toString() == c.layerId) order.removeAt(i);
            doc_.insert(QStringLiteral("layers"), order);
            return;
        }
        ensureLayer(c.layerId);
        QJsonObject d = data();
        QJsonObject layer = d.value(c.layerId).toObject();
        const QJsonObject props = c.mineValue.toObject();
        for (auto it = props.begin(); it != props.end(); ++it) layer.insert(it.key(), it.value());
        d.insert(c.layerId, layer);
        setData(d);
    }

    // Mine's whole item (with what the compare leaves out, such as links).
    QJsonValue mineItem(const ItemChange& c) const {
        const QJsonArray list = mine_.doc.value(QLatin1String("layerData"))
                                    .toObject()
                                    .value(c.layerId)
                                    .toObject()
                                    .value(listOf(c.kind))
                                    .toArray();
        const int i = findItem(list, c.kind, c.key);
        return i >= 0 ? list[i] : QJsonValue();
    }

    void takeLayerItem(const ItemChange& c, bool copy) {
        const QString listName = listOf(c.kind);
        if (c.mineValue.isUndefined()) {
            QJsonObject d = data();
            QJsonObject layer = d.value(c.layerId).toObject();
            QJsonArray list = layer.value(listName).toArray();
            const int i = findItem(list, c.kind, c.key);
            if (i >= 0) list.removeAt(i);
            layer.insert(listName, list);
            if (d.contains(c.layerId)) d.insert(c.layerId, layer);
            setData(d);
            return;
        }
        ensureLayer(c.layerId);
        QJsonObject d = data();
        QJsonObject layer = d.value(c.layerId).toObject();
        QJsonArray list = layer.value(listName).toArray();
        QJsonObject item = mineItem(c).toObject();
        const int i = findItem(list, c.kind, c.key);
        if (copy) {
            item.insert(QStringLiteral("id"), newId());
            item.insert(QStringLiteral("myGroup"), QString());
            if (item.contains(QLatin1String("connexions"))) {
                QJsonArray links;
                for (const auto& l : item.value(QLatin1String("connexions")).toArray())
                    links.append(QJsonObject{ { QStringLiteral("id"), newId() }, { QStringLiteral("linkedTo"), QString() } });
                item.insert(QStringLiteral("connexions"), links);
            }
            list.append(item);
        } else if (i >= 0) {
            list.replace(i, item);
        } else {
            list.append(item);
        }
        layer.insert(listName, list);
        d.insert(c.layerId, layer);
        setData(d);
    }

    void takeSidecarItem(const ItemChange& c, const QString& listName, bool copy) {
        QJsonArray list = sidecar_.value(listName).toArray();
        const QString id = c.key.section(QLatin1Char(':'), 1);
        int at = -1;
        for (int i = 0; i < list.size(); ++i)
            if (list[i].toObject().value(QLatin1String("id")).toString() == id) at = i;
        if (c.mineValue.isUndefined()) {
            if (at >= 0) list.removeAt(at);
        } else if (copy) {
            QJsonObject item = c.mineValue.toObject();
            item.insert(QStringLiteral("id"), newId());
            list.append(item);
        } else if (at >= 0) {
            list.replace(at, c.mineValue);
        } else {
            list.append(c.mineValue);
        }
        sidecar_.insert(listName, list);
    }

    const Snapshot& mine_;
    QJsonObject doc_;
    QJsonObject sidecar_;
};

QString verb(Side s) {
    switch (s) {
    case Side::Added: return tr("added it");
    case Side::Edited: return tr("changed it");
    case Side::Deleted: return tr("deleted it");
    case Side::Unchanged: break;
    }
    return tr("left it as it was");
}

}  // namespace

bool ItemChange::allowsBoth() const {
    static const QSet<QString> copyable{ QStringLiteral("brick"), QStringLiteral("text"), QStringLiteral("label"),
                                         QStringLiteral("module") };
    return copyable.contains(kind) && !mineValue.isUndefined() && !serverValue.isUndefined();
}

Snapshot snapshotOf(const core::Map& map) {
    QJsonObject sidecar = saveload::sidecarToJson(map.sidecar);
    sidecar.remove(QStringLiteral("bbmHashSha256"));
    sidecar.remove(QStringLiteral("schemaVersion"));
    return { docJsonFromMap(map), sidecar };
}

std::unique_ptr<core::Map> mapOf(const Snapshot& snapshot, QString* error) {
    auto map = mapFromDocJson(snapshot.doc, error);
    if (map) saveload::sidecarFromJson(snapshot.sidecar, map->sidecar);
    return map;
}

QJsonObject toJson(const Snapshot& snapshot) {
    return { { QStringLiteral("doc"), snapshot.doc }, { QStringLiteral("sidecar"), snapshot.sidecar } };
}

Snapshot snapshotFromJson(const QJsonObject& o) {
    return { o.value(QLatin1String("doc")).toObject(), o.value(QLatin1String("sidecar")).toObject() };
}

QList<ItemChange> compareLayouts(const Snapshot& base, const Snapshot& mine, const Snapshot& server) {
    const Items b = itemsOf(base), m = itemsOf(mine), s = itemsOf(server);
    QSet<QString> keys;
    for (const auto* items : { &b, &m, &s })
        for (auto it = items->begin(); it != items->end(); ++it) keys.insert(it.key());
    QStringList sorted(keys.begin(), keys.end());
    sorted.sort();
    QList<ItemChange> out;
    for (const auto& key : sorted) {
        const auto find = [&](const Items& items) -> const Item* {
            const auto it = items.constFind(key);
            return it == items.constEnd() ? nullptr : &it.value();
        };
        const Item *bi = find(b), *mi = find(m), *si = find(s);
        ItemChange c;
        c.key = key;
        c.mine = sideChange(bi, mi);
        c.server = sideChange(bi, si);
        if (c.mine == Side::Unchanged && c.server == Side::Unchanged) continue;
        const bool same = (mi && si) ? mi->value == si->value : (!mi && !si);
        c.status = c.mine == Side::Unchanged     ? Status::Server
                 : c.server == Side::Unchanged ? Status::Mine
                 : same                        ? Status::Same
                                               : Status::Conflict;
        const Item* any = mi ? mi : si ? si : bi;
        c.kind = any->kind;
        c.layerId = any->layerId;
        c.layerName = any->layerName;
        if (bi) c.base = bi->value;
        if (mi) c.mineValue = mi->value;
        if (si) c.serverValue = si->value;
        out << c;
    }
    return out;
}

std::unique_ptr<core::Map> mergeLayouts(const Snapshot& mine, const Snapshot& server,
                                        const QList<ItemChange>& changes, const QHash<QString, Choice>& choices,
                                        QString* error) {
    Merger merger(mine, server);
    // Layers first, so items find the layer they go in.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : changes)
            if ((c.kind == QLatin1String("layer")) == (pass == 0)) merger.take(c, choices.value(c.key, c.defaultChoice()));
    return mapOf(merger.result(), error);
}

namespace {

// A stud coordinate, to a tenth at most: 120, 47.5.
QString studs(double v) { return QString::number(std::round(v * 10) / 10 + 0.0); }  // + 0.0: no "-0"

// " at (x, y)" for an item with a display area, else nothing.
QString at(const QJsonObject& v) {
    const QJsonObject area = v.value(QLatin1String("displayArea")).toObject();
    if (area.isEmpty()) return {};
    return tr(" at (%1, %2)")
        .arg(studs(area.value(QLatin1String("x")).toDouble()), studs(area.value(QLatin1String("y")).toDouble()));
}

// " on "Layer"" for an item in a named layer, else nothing.
QString on(const ItemChange& c) { return c.layerName.isEmpty() ? QString() : tr(" on \"%1\"").arg(c.layerName); }

}  // namespace

QString describe(const ItemChange& c) {
    const QJsonObject v = (c.mineValue.isUndefined() ? (c.serverValue.isUndefined() ? c.base : c.serverValue)
                                                     : c.mineValue)
                              .toObject();
    QString what;
    if (c.kind == QLatin1String("map")) what = tr("Layout details");
    else if (c.kind == QLatin1String("layer")) what = tr("Layer \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("brick"))
        what = tr("Brick %1").arg(v.value(QLatin1String("partNumber")).toString()) + at(v) + on(c);
    else if (c.kind == QLatin1String("group")) {
        const QString part = v.value(QLatin1String("partNumber")).toString();
        what = (part.isEmpty() ? tr("Group") : tr("Group %1").arg(part)) + on(c);
    } else if (c.kind == QLatin1String("text"))
        what = tr("Text \"%1\"").arg(v.value(QLatin1String("text")).toString().left(40)) + at(v) + on(c);
    else if (c.kind == QLatin1String("area")) what = tr("Area cell %1").arg(c.key.section(QLatin1Char(':'), 2)) + on(c);
    else if (c.kind == QLatin1String("ruler")) what = tr("Ruler") + on(c);
    else if (c.kind == QLatin1String("label")) what = tr("Label \"%1\"").arg(v.value(QLatin1String("text")).toString().left(40));
    else if (c.kind == QLatin1String("module")) what = tr("Module \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("venue")) what = tr("Venue");
    else what = tr("Background image");
    if (c.status == Status::Same) return tr("%1: you and the server both %2").arg(what, verb(c.mine));
    if (c.status == Status::Mine) return tr("%1: you %2").arg(what, verb(c.mine));
    if (c.status == Status::Server) return tr("%1: the server %2").arg(what, verb(c.server));
    return tr("%1: you %2, the server %3").arg(what, verb(c.mine), verb(c.server));
}

}  // namespace bld::sync::merge
