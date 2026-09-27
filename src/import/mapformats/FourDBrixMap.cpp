#include "FourDBrixMap.h"

#include "BrickPlacement.h"

#include "../../core/Ids.h"
#include "../../core/LayerBrick.h"
#include "../../core/LayerGrid.h"
#include "../../core/Map.h"
#include "../../edit/Connectivity.h"
#include "../../saveload/XmlPrimitives.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSaveFile>
#include <QXmlStreamReader>

#include <algorithm>
#include <limits>

namespace bld::import {

namespace {

using Type = parts::PartMetadata::FourDBrix::Type;

QString partNumberOf(const parts::PartMetadata& meta) {
    return (meta.colorCode.isEmpty() ? meta.partNumber : meta.partNumber + QLatin1Char('.') + meta.colorCode).toUpper();
}

// BlueBrick un-escapes attribute values once more after the XML parser has.
QString unescape(QString s) {
    return s.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
            .replace(QStringLiteral("&apos;"), QStringLiteral("'"))
            .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
            .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
            .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
}

QString escape(QString s) {
    return s.replace(QStringLiteral("&"), QStringLiteral("&amp;"))
            .replace(QStringLiteral("\""), QStringLiteral("&quot;"))
            .replace(QStringLiteral("'"), QStringLiteral("&apos;"))
            .replace(QStringLiteral("<"), QStringLiteral("&lt;"))
            .replace(QStringLiteral(">"), QStringLiteral("&gt;"));
}

// BlueBrick's ConnectionTypeList.xml <FourDBrixName>s.
QString nodeTypeName(const QString& connectionType) {
    if (connectionType == QLatin1String("mono"))     return QStringLiteral("NT_MONORAIL");
    if (connectionType == QLatin1String("mono45"))   return QStringLiteral("NT_MONORAIL45");
    if (connectionType == QLatin1String("monoramp")) return QStringLiteral("NT_MONORAILRAMP");
    return QStringLiteral("NT_UNDEFINED");
}

float attrFloat(const QXmlStreamReader& r, const char* name) {
    return r.attributes().value(QLatin1String(name)).toFloat();
}

QString fmt(float v) { return saveload::xml::formatFloat(v); }

struct Segment {
    QString id;  // nControl uses integers, BlueBrick writes brick GUIDs
    QString partName;
    float   angle = 0.0f;
    int     originNode = 0;
    core::Brick* brick = nullptr;
    bool    grouped = false;
};

struct Node { float x = 0, y = 0, z = 0; };

// BlueBrick's Orientation setter keeps the displayArea's top-left corner.
void setOrientationKeepingCorner(core::Brick& b, float orientation, parts::PartsLibrary& lib) {
    b.orientation = orientation;
    if (const auto fp = lib.footprint(b.partNumber, orientation)) b.displayArea.setSize(fp->size);
}

class Reader {
public:
    Reader(parts::PartsLibrary& lib) : lib_(lib) {}

    MapReadResult read(const QString& path) {
        MapReadResult out;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            out.error = f.errorString();
            return out;
        }
        map_ = std::make_unique<core::Map>();
        QXmlStreamReader r(&f);
        while (r.readNextStartElement()) {
            if (r.name() != QStringLiteral("data")) { r.skipCurrentElement(); continue; }
            while (r.readNextStartElement()) {
                const auto n = r.name();
                if      (n == QStringLiteral("project"))   project(r);
                else if (n == QStringLiteral("node"))      node(r);
                else if (n == QStringLiteral("segment"))   segment(r);
                else if (n == QStringLiteral("table"))     generic(r, tables_, false);
                else if (n == QStringLiteral("baseplate")) generic(r, baseplates_, false);
                else if (n == QStringLiteral("structure")) generic(r, structures_, true);
                else if (n == QStringLiteral("group"))     group(r);
                else r.skipCurrentElement();
            }
            break;
        }
        if (r.hasError() && r.error() != QXmlStreamReader::PrematureEndOfDocumentError) {
            out.error = r.errorString();
            return out;
        }

        buildTracks();
        auto tracks = makeLayer(QStringLiteral("Tracks"), std::move(tracks_));
        auto grid = std::make_unique<core::LayerGrid>();
        grid->guid = core::newBbmId();
        map_->layers().push_back(std::move(grid));
        map_->layers().push_back(makeLayer(QStringLiteral("Tables"), std::move(tables_)));
        map_->layers().push_back(makeLayer(QStringLiteral("Baseplates"), std::move(baseplates_)));
        auto* trackLayer = tracks.get();
        map_->layers().push_back(std::move(tracks));
        map_->layers().push_back(makeLayer(QStringLiteral("Structures"), std::move(structures_)));
        map_->selectedLayerIndex = 3;
        edit::rebuildConnectivity(*map_, lib_);
        // BlueBrick groups the segments after linking them (grouped bricks
        // hand over their active connection differently), and before sorting
        // (Segment::brick points into the layer).
        trackLayer->groups = groups();
        std::stable_sort(trackLayer->bricks.begin(), trackLayer->bricks.end(),
                         [](const core::Brick& a, const core::Brick& b) { return a.altitude < b.altitude; });

        if (!unmapped_.isEmpty())
            out.warnings << QStringLiteral("No part is mapped to these 4DBrix parts: %1").arg(unmapped_.join(QStringLiteral(", ")));
        out.map = std::move(map_);
        return out;
    }

private:
    static std::unique_ptr<core::LayerBrick> makeLayer(const QString& name, std::vector<core::Brick> bricks) {
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = core::newBbmId();
        layer->name = name;
        layer->bricks = std::move(bricks);
        return layer;
    }

    void project(QXmlStreamReader& r) {
        while (r.readNextStartElement()) {
            const auto n = r.name();
            const QString value = unescape(r.attributes().value(QStringLiteral("value")).toString());
            if      (n == QStringLiteral("title"))  map_->event = value;
            else if (n == QStringLiteral("author")) map_->author = value;
            else if (n == QStringLiteral("lug"))    map_->lug = value;
            else if (n == QStringLiteral("info"))   map_->comment = value;
            r.skipCurrentElement();
        }
    }

    void node(QXmlStreamReader& r) {
        Node n;
        while (r.readNextStartElement()) {
            if (r.name() == QStringLiteral("coordinates")) {
                n.x = attrFloat(r, "x");
                n.y = attrFloat(r, "y");
                n.z = attrFloat(r, "z");
            }
            r.skipCurrentElement();
        }
        nodes_.push_back(n);
    }

    void segment(QXmlStreamReader& r) {
        Segment s;
        QList<int> nodes;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            const auto value = r.attributes().value(QStringLiteral("value"));
            if      (n == QStringLiteral("index"))  s.id = value.trimmed().toString();
            else if (n == QStringLiteral("type"))   s.partName = value.toString();
            else if (n == QStringLiteral("angle"))  s.angle = value.toFloat();
            else if (n == QStringLiteral("origin")) s.originNode = value.toInt();
            else if (n.startsWith(QStringLiteral("node")) && n != QStringLiteral("nodes")) {
                bool ok = false;
                const int index = value.toInt(&ok);
                if (ok) nodes << index;
            }
            r.skipCurrentElement();
        }
        if (s.partName.isEmpty()) return;
        // The origin is local to the segment's own node list.
        if (s.originNode >= 0 && s.originNode < nodes.size()) s.originNode = nodes[s.originNode];
        segments_.push_back(s);
    }

    void generic(QXmlStreamReader& r, std::vector<core::Brick>& layer, bool coordInCentre) {
        const QString tag = r.name().toString();
        const QString coordTag = coordInCentre ? QStringLiteral("center") : QStringLiteral("coordinates");
        float x = 0, y = 0, angle = 0;
        QString name;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if (n == coordTag) {
                x = attrFloat(r, "x");
                y = attrFloat(r, "y");
            } else if (n == QStringLiteral("angle")) {
                angle = attrFloat(r, "value");
            } else if (n == QStringLiteral("svgfile")) {
                name = r.attributes().value(QStringLiteral("value")).toString();
            }
            r.skipCurrentElement();
        }
        if (name.isEmpty()) return;
        const auto meta = metaFor(name);
        if (!meta) return;

        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = partNumberOf(*meta);
        b.orientation = angle;
        const QPointF pos(x * 0.125f, y * 0.125f);
        if (coordInCentre) {
            placement::placeByImageCentre(b, pos, lib_);
        } else {
            const auto fp = lib_.footprint(b.partNumber, angle);
            const QSizeF size = fp ? fp->size : QSizeF(2, 2);
            const QPointF corner = fp ? fp->imageCorner : QPointF();
            b.displayArea = QRectF(pos - corner, size);
        }
        if (meta->fourDBrix->orientationDifference != 0.0f)
            setOrientationKeepingCorner(b, angle - meta->fourDBrix->orientationDifference, lib_);
        layer.push_back(std::move(b));
    }

    void group(QXmlStreamReader& r) {
        QStringList ids;
        while (r.readNextStartElement()) {
            if (r.name() == QStringLiteral("segments")) {
                const QString list = r.attributes().value(QStringLiteral("list")).toString();
                for (const QString& id : list.split(QLatin1Char(','))) {
                    if (!id.trimmed().isEmpty()) ids << id.trimmed();
                }
            }
            r.skipCurrentElement();
        }
        if (!ids.isEmpty()) groupIds_ << ids;
    }

    std::optional<parts::PartMetadata> metaFor(const QString& name) {
        const QString key = lib_.partForFourDBrixName(name);
        auto meta = key.isEmpty() ? std::nullopt : lib_.metadata(key);
        if (!meta || !meta->fourDBrix) {
            if (!unmapped_.contains(name)) unmapped_ << name;
            return std::nullopt;
        }
        return meta;
    }

    void buildTracks() {
        tracks_.reserve(segments_.size());  // Segment::brick points into it
        for (Segment& s : segments_) {
            const auto meta = metaFor(s.partName);
            if (!meta) continue;
            core::Brick b;
            b.guid = core::newBbmId();
            b.partNumber = partNumberOf(*meta);
            b.orientation = s.angle - meta->fourDBrix->orientationDifference;
            const auto fp = lib_.footprint(b.partNumber, b.orientation);
            b.displayArea = QRectF(QPointF(), fp ? fp->size : QSizeF(2, 2));
            if (s.originNode >= 0 && s.originNode < static_cast<int>(nodes_.size())) {
                const Node& n = nodes_[s.originNode];
                const int origin = meta->fourDBrix->originConnection;
                if (!meta->connections.isEmpty()) {
                    b.activeConnectionPointIndex = std::clamp(origin, 0, static_cast<int>(meta->connections.size()) - 1);
                    placement::placeByConnection(b, b.activeConnectionPointIndex, QPointF(n.x * 0.125f, n.y * 0.125f), lib_);
                }
                b.altitude = n.z * 2.5f;
            }
            tracks_.push_back(std::move(b));
            s.brick = &tracks_.back();
        }
    }

    // Each segment joins the first group that lists it.
    std::vector<core::Group> groups() {
        std::vector<core::Group> out;
        for (const QStringList& ids : std::as_const(groupIds_)) {
            core::Group g;
            g.guid = core::newBbmId();
            bool any = false;
            for (const QString& id : ids) {
                for (Segment& s : segments_) {
                    if (s.id != id || s.grouped || !s.brick) continue;
                    s.brick->myGroupId = g.guid;
                    s.grouped = true;
                    any = true;
                    break;
                }
            }
            if (any) out.push_back(g);
        }
        return out;
    }

    parts::PartsLibrary& lib_;
    std::unique_ptr<core::Map> map_;
    std::vector<Node> nodes_;
    std::vector<Segment> segments_;
    QList<QStringList> groupIds_;
    std::vector<core::Brick> tables_, baseplates_, tracks_, structures_;
    QStringList unmapped_;
};

struct Part {
    const core::Brick* brick;
    parts::PartMetadata meta;
};

class Writer {
public:
    Writer(const core::Map& map, parts::PartsLibrary& lib) : map_(map), lib_(lib) {}

    QByteArray write(const QString& path) {
        header(path);
        std::vector<Part> tables, tracks, baseplates, structures;
        std::vector<std::pair<const core::LayerBrick*, QString>> topGroups;
        for (const auto& layer : map_.layers()) {
            if (layer->kind() != core::LayerKind::Brick) continue;
            const auto& bl = static_cast<const core::LayerBrick&>(*layer);
            for (const auto& b : bl.bricks) {
                for (int i = 0; i < static_cast<int>(b.connections.size()); ++i)
                    owner_.insert(b.connections[i].guid, { &b, i });
                if (!b.myGroupId.isEmpty()) {
                    const QString top = topGroup(bl, b.myGroupId);
                    const auto entry = std::make_pair(&bl, top);
                    if (std::find(topGroups.begin(), topGroups.end(), entry) == topGroups.end()) topGroups.push_back(entry);
                }
                const auto meta = lib_.metadata(b.partNumber);
                if (!meta || !meta->fourDBrix) continue;
                const Part p{ &b, *meta };
                switch (meta->fourDBrix->type) {
                case Type::Segment:   tracks.push_back(p); break;
                case Type::Table:     tables.push_back(p); break;
                case Type::Baseplate: baseplates.push_back(p); break;
                case Type::Structure: structures.push_back(p); break;
                }
            }
        }
        trackParts(tracks);
        genericParts(tables, QStringLiteral("table"), false);
        genericParts(baseplates, QStringLiteral("baseplate"), false);
        genericParts(structures, QStringLiteral("structure"), true);
        for (const auto& [layer, top] : topGroups) group(*layer, top);
        line(QStringLiteral("</data>"));
        return out_.toUtf8();
    }

private:
    struct Owner { const core::Brick* brick = nullptr; int index = 0; };

    void line(const QString& s) { out_ += s + QStringLiteral("\r\n"); }

    void header(const QString& path) {
        // Map.getTotalAreaInStud(true): visible brick layers only.
        QRectF area;
        for (const auto& layer : map_.layers()) {
            if (layer->kind() != core::LayerKind::Brick || !layer->visible) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) area |= b.displayArea;
        }
        const QLocale c = QLocale::c();
        const QString format = QStringLiteral("d-MMM-yyyy, HH:mm:ss");
        const QDateTime nowTime = QDateTime::currentDateTime();
        const QString now = c.toString(nowTime, format);
        QString created = now;
        const QFileInfo existing(path);
        if (existing.exists()) {
            const QDateTime birth = existing.birthTime();
            created = c.toString(birth.isValid() ? birth : existing.lastModified(), format);
        }
        const QString attr = QStringLiteral("      <%1 value=\"%2\"/>");
        line(QStringLiteral("<?xml version=\"1.0\"?>"));
        line(QStringLiteral("<data type=\"nControl\" version=\"1\">"));
        line(QStringLiteral("   <project>"));
        line(QStringLiteral("      <ncontrol version=\"2020.0\"/>"));
        line(attr.arg(QStringLiteral("title"), escape(map_.event)));
        line(attr.arg(QStringLiteral("author"), escape(map_.author)));
        line(attr.arg(QStringLiteral("lug"), escape(map_.lug)));
        line(attr.arg(QStringLiteral("created"), created));
        line(attr.arg(QStringLiteral("modified"), now));
        line(attr.arg(QStringLiteral("description"), c.toString(QDateTime(map_.date, QTime(0, 0)), format)));
        line(attr.arg(QStringLiteral("info"), escape(map_.comment)));
        line(QStringLiteral("      <tracklayout width=\"%1\" height=\"%2\" scale=\"0.25\"/>")
                 .arg(static_cast<int>(static_cast<float>(area.right())))
                 .arg(static_cast<int>(static_cast<float>(area.bottom()))));
        line(QStringLiteral("      <tilepanel rows=\"1\" columns=\"6\"/>"));
        line(QStringLiteral("   </project>"));
        for (const char* script : { "ST_ACTIVATION", "ST_DEACTIVATION" }) {
            line(QStringLiteral("   <script type=\"%1\">").arg(QLatin1String(script)));
            line(QStringLiteral("      <action value=\"\"/>"));
            line(QStringLiteral("   </script>"));
        }
    }

    // One node per junction: a linked connection shares its partner's node.
    void trackParts(const std::vector<Part>& tracks) {
        QHash<QString, int> nodeOf;  // connection guid -> node index
        int nodes = 0;
        for (const Part& p : tracks) {
            const core::Brick& b = *p.brick;
            for (int i = 0; i < static_cast<int>(b.connections.size()) && i < p.meta.connections.size(); ++i) {
                const auto& cp = b.connections[i];
                const auto partner = nodeOf.constFind(cp.linkedToId);
                if (!cp.linkedToId.isEmpty() && partner != nodeOf.constEnd()) {
                    nodeOf.insert(cp.guid, partner.value());
                    continue;
                }
                nodeOf.insert(cp.guid, nodes++);
                const QPointF world = placement::connectionWorld(b, i, lib_);
                const Owner other = owner_.value(cp.linkedToId);
                line(QStringLiteral("   <node>"));
                line(QStringLiteral("      <coordinates x=\"%1\" y=\"%2\" z=\"%3\"/>")
                         .arg(fmt(static_cast<float>(world.x()) * 8), fmt(static_cast<float>(world.y()) * 8), fmt(b.altitude * 0.4f)));
                line(QStringLiteral("      <segments a=\"%1\" b=\"%2\"/>")
                         .arg(b.guid, other.brick ? other.brick->guid : QStringLiteral("None")));
                line(QStringLiteral("      <anchor value=\"no\"/>"));
                line(QStringLiteral("      <type value=\"%1\"/>").arg(nodeTypeName(p.meta.connections[i].type)));
                line(QStringLiteral("   </node>"));
            }
        }
        for (const Part& p : tracks) {
            const core::Brick& b = *p.brick;
            const auto& fd = *p.meta.fourDBrix;
            line(QStringLiteral("   <segment>"));
            line(QStringLiteral("      <index value=\"%1\"/>").arg(b.guid));
            line(QStringLiteral("      <type value=\"%1\"/>").arg(fd.partName));
            line(QStringLiteral("      <label value=\"\"/>"));
            const int count = std::min<int>(static_cast<int>(b.connections.size()), p.meta.connections.size());
            const bool hasConnection = std::any_of(p.meta.connections.cbegin(), p.meta.connections.cend(),
                                                   [](const auto& c) { return !c.type.isEmpty(); });
            if (hasConnection && count > 0) {
                line(QStringLiteral("      <nodes value=\"%1\"/>").arg(count));
                const int origin = std::clamp(fd.originConnection, 0, count - 1);
                int local = 1, i = origin;
                do {
                    line(QStringLiteral("      <node%1 value=\"%2\"/>").arg(local++).arg(nodeOf.value(b.connections[i].guid)));
                    if (++i == count) i = 0;
                } while (i != origin);
            }
            line(QStringLiteral("      <angle value=\"%1\"/>").arg(fmt(b.orientation + fd.orientationDifference)));
            line(QStringLiteral("      <origin value=\"0\"/>"));
            line(QStringLiteral("      <slope value=\"0\"/>"));
            line(QStringLiteral("   </segment>"));
        }
    }

    void genericParts(const std::vector<Part>& parts, const QString& tag, bool coordInCentre) {
        const QString coordTag = coordInCentre ? QStringLiteral("center") : QStringLiteral("coordinates");
        for (const Part& p : parts) {
            const core::Brick& b = *p.brick;
            const auto& fd = *p.meta.fourDBrix;
            // BlueBrick turns the brick by the difference (keeping its
            // displayArea's corner) and reads the position from that.
            const float orientation = b.orientation + fd.orientationDifference;
            const auto fp = lib_.footprint(b.partNumber, orientation);
            const QRectF area(b.displayArea.topLeft(), fp ? fp->size : b.displayArea.size());
            const QPointF pos = coordInCentre ? area.center() : area.topLeft() + (fp ? fp->imageCorner : QPointF());
            line(QStringLiteral("   <%1>").arg(tag));
            line(QStringLiteral("      <%1 x =\"%2\" y=\"%3\"/>")
                     .arg(coordTag, fmt(static_cast<float>(pos.x()) * 8.0f), fmt(static_cast<float>(pos.y()) * 8.0f)));
            line(QStringLiteral("      <angle value=\"%1\"/>").arg(fmt(orientation)));
            line(QStringLiteral("      <svgfile value=\"%1\"/>").arg(fd.partName));
            if (tag != QLatin1String("table")) {
                const QPixmap pm = lib_.pixmap(b.partNumber);
                line(QStringLiteral("      <size height=\"%1\" width=\"%2\"/>").arg(pm.height()).arg(pm.width()));
            }
            line(QStringLiteral("   </%1>").arg(tag));
        }
    }

    static const core::Group* findGroup(const core::LayerBrick& layer, const QString& guid) {
        for (const auto& g : layer.groups) if (g.guid == guid) return &g;
        return nullptr;
    }

    static QString topGroup(const core::LayerBrick& layer, QString guid) {
        for (int depth = 0; depth < 64; ++depth) {
            const core::Group* g = findGroup(layer, guid);
            if (!g || g->myGroupId.isEmpty()) break;
            guid = g->myGroupId;
        }
        return guid;
    }

    // Group.getAllLeafItems(): a group's own bricks (in layer order) come
    // before its sub-groups (in <Groups> order), as BlueBrick loads them.
    static void leaves(const core::LayerBrick& layer, const QString& guid, QStringList& ids, QRectF& area, int depth = 0) {
        if (depth > 64) return;
        for (const auto& b : layer.bricks) {
            if (b.myGroupId != guid) continue;
            ids << b.guid;
            area |= b.displayArea;
        }
        for (const auto& g : layer.groups)
            if (g.myGroupId == guid && g.guid != guid) leaves(layer, g.guid, ids, area, depth + 1);
    }

    void group(const core::LayerBrick& layer, const QString& top) {
        QStringList ids;
        QRectF area;
        leaves(layer, top, ids, area);
        line(QStringLiteral("   <group>"));
        line(QStringLiteral("      <segments list=\"%1\"/>").arg(ids.join(QLatin1Char(','))));
        line(QStringLiteral("      <boundingbox x=\"%1\" y=\"%2\" w=\"%3\" h=\"%4\"/>")
                 .arg(fmt(static_cast<float>(area.x()) * 8), fmt(static_cast<float>(area.y()) * 8),
                      fmt(static_cast<float>(area.width()) * 8), fmt(static_cast<float>(area.height()) * 8)));
        line(QStringLiteral("   </group>"));
    }

    const core::Map& map_;
    parts::PartsLibrary& lib_;
    QHash<QString, Owner> owner_;
    QString out_;
};

}  // namespace

MapReadResult readFourDBrixMap(const QString& path, parts::PartsLibrary& lib) {
    return Reader(lib).read(path);
}

bool writeFourDBrixMap(const core::Map& map, const QString& path, parts::PartsLibrary& lib, QString* error) {
    const QByteArray bytes = Writer(map, lib).write(path);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace bld::import
