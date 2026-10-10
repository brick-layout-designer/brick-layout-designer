#include "LDrawMap.h"
#include "../../parts/BrickPlacement.h"

#include "../../core/Ids.h"
#include "../../core/LayerArea.h"
#include "../../core/LayerBrick.h"
#include "../../core/LayerGrid.h"
#include "../../core/LayerRuler.h"
#include "../../core/LayerText.h"
#include "../../core/Map.h"
#include "../../edit/Connectivity.h"
#include "../../parts/PartsLibrary.h"
#include "../../saveload/XmlPrimitives.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace bld::import {

namespace {

constexpr double kLduPerStud = 20.0;
const QString kDateFormat = QStringLiteral("dd/MM/yyyy");
const QString kHide  = QStringLiteral("0 MLCAD HIDE ");
const QString kBtg   = QStringLiteral("0 MLCAD BTG ");
const QString kGroup = QStringLiteral("0 GROUP ");
const QString kRuler = QStringLiteral("0 !BLUEBRICK RULER 1 ");

// BlueBrick's splitLDrawLine: whitespace-separated, but "double quoted"
// runs stay one token (without the quotes).
QStringList splitLine(const QString& line) {
    QStringList out;
    const QStringList quoted = line.split(QLatin1Char('"'));
    for (int i = 0; i < quoted.size(); ++i) {
        if (quoted[i].isEmpty()) continue;
        if (i % 2) out << quoted[i];
        else out << quoted[i].split(QRegularExpression(QStringLiteral("[ \\t]+")), Qt::SkipEmptyParts);
    }
    return out;
}

QString restAfterToken(const QString& line, const QString& token) {
    return line.mid(line.indexOf(token) + token.size()).trimmed();
}

QPointF rotated(QPointF v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = std::cos(r), s = std::sin(r);
    return { v.x() * c - v.y() * s, v.x() * s + v.y() * c };
}

// "<part>.<color>" split at the last dot.
std::pair<QString, QString> splitPartAndColor(const QString& partNumber) {
    const int dot = partNumber.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return { partNumber, {} };
    return { partNumber.left(dot), partNumber.mid(dot + 1) };
}

// ------------------------------------------------------------------ reading

core::ColorSpec readColor(const QString& token) {
    if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        return core::ColorSpec::fromArgb(QColor::fromRgba(token.mid(2).toUInt(nullptr, 16)));
    }
    const QColor named(token);
    return core::ColorSpec::fromKnown(named.isValid() ? named : QColor(Qt::black), token);
}

QPointF readPointF(const QString& token) {
    const QStringList xy = token.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    return { xy.value(0).toDouble(), xy.value(1).toDouble() };
}

struct PendingGroup {
    QString guid;
    QString partNumber;
    QString parentGuid;
};

class Reader {
public:
    explicit Reader(parts::PartsLibrary& lib) : lib_(lib), map_(std::make_unique<core::Map>()) {
        map_->author.clear();
    }

    MapReadResult run(const QString& path) {
        MapReadResult out;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out.error = f.errorString();
            return out;
        }
        const bool mpd = path.endsWith(QStringLiteral(".mpd"), Qt::CaseInsensitive);
        QTextStream in(&f);
        QString line;
        bool firstFile = true;
        QStringList hiddenLayers;
        while (in.readLineInto(&line)) {
            const QStringList t = splitLine(line);
            if (t.isEmpty()) continue;
            if (t[0] == QLatin1String("0") && t.size() > 1) {
                if (mpd && t[1] == QLatin1String("FILE")) {
                    // The first FILE is the main model; each later one is a layer.
                    if (!firstFile) {
                        finalizeLayer();
                        layerName_ = QFileInfo(line.mid(7).trimmed()).completeBaseName();
                    }
                    firstFile = false;
                } else if (t[1] == QLatin1String("STEP")) {
                    if (!mpd) finalizeLayer();
                } else if (mpd && t[1] == QLatin1String("MLCAD") && t.value(2) == QLatin1String("HIDE")
                           && t.size() > 17 && t[17].endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive)) {
                    hiddenLayers << QFileInfo(t[17]).completeBaseName();
                } else {
                    parseMeta(line, t, 1);
                }
            } else if (t[0] == QLatin1String("1")) {
                parseBrick(t, 1);
            }
        }
        finalizeLayer();

        for (auto& layer : map_->layers()) {
            if (hiddenLayers.contains(layer->name)) layer->visible = false;
        }
        edit::rebuildConnectivity(*map_, lib_);
        if (skippedUnknown_ > 0) {
            out.warnings << QStringLiteral("%1 part(s) are not in the parts library").arg(skippedUnknown_);
        }
        out.map = std::move(map_);
        return out;
    }

private:
    template <typename T>
    T& currentLayerOf(core::LayerKind kind) {
        if (layer_ && layer_->kind() != kind) finalizeLayer();
        if (!layer_) {
            layer_ = std::make_unique<T>();
            layer_->guid = core::newBbmId();
            layer_->name = layerName_.isEmpty()
                ? QStringLiteral("Layer %1").arg(map_->layers().size() + 1)
                : layerName_;
        }
        return static_cast<T&>(*layer_);
    }

    void finalizeLayer() {
        if (!layer_) return;
        if (layer_->kind() == core::LayerKind::Brick) {
            auto& bl = static_cast<core::LayerBrick&>(*layer_);
            std::stable_sort(bl.bricks.begin(), bl.bricks.end(),
                             [](const core::Brick& a, const core::Brick& b) { return a.altitude < b.altitude; });
            bl.groups = groupsFor(bl.bricks);
        } else if (layer_->kind() == core::LayerKind::Ruler) {
            auto& rl = static_cast<core::LayerRuler&>(*layer_);
            std::vector<core::LayerItem> items;
            for (const auto& r : rl.rulers) {
                items.push_back(r.kind == core::RulerKind::Linear
                    ? static_cast<const core::LayerItem&>(r.linear)
                    : static_cast<const core::LayerItem&>(r.circular));
            }
            rl.groups = groupsFor(items);
        }
        map_->layers().push_back(std::move(layer_));
    }

    // The groups (and their ancestors) the layer's items belong to.
    template <typename Items>
    std::vector<core::Group> groupsFor(const Items& items) {
        std::vector<core::Group> out;
        QSet<QString> done;
        std::vector<QString> pending;
        for (const auto& it : items) if (!it.myGroupId.isEmpty()) pending.push_back(it.myGroupId);
        while (!pending.empty()) {
            const QString guid = pending.back();
            pending.pop_back();
            if (done.contains(guid)) continue;
            done.insert(guid);
            for (const PendingGroup& g : std::as_const(groups_)) {
                if (g.guid != guid) continue;
                core::Group group;
                group.guid = g.guid;
                group.partNumber = g.partNumber;
                group.myGroupId = g.parentGuid;
                out.push_back(group);
                if (!g.parentGuid.isEmpty()) pending.push_back(g.parentGuid);
            }
        }
        return out;
    }

    PendingGroup& groupNamed(const QString& name) {
        auto it = groups_.find(name);
        if (it == groups_.end()) {
            PendingGroup g;
            g.guid = core::newBbmId();
            const int hash = name.lastIndexOf(QLatin1Char('#'));
            if (hash > 0) g.partNumber = name.left(hash);
            it = groups_.insert(name, g);
        }
        return it.value();
    }

    // "0 MLCAD BTG <group>" applies to the next item only.
    QString takePendingGroup() {
        QString guid = pendingGroup_;
        pendingGroup_.clear();
        return guid;
    }

    void parseMeta(const QString& line, const QStringList& t, int i) {
        if (i >= t.size()) return;
        const QString& key = t[i];
        if (key.startsWith(QStringLiteral("Author"))) {
            map_->author = restAfterToken(line, t[1]);
        } else if (key.startsWith(QStringLiteral("Lug"))) {
            map_->lug = restAfterToken(line, t[1]);
        } else if (key.startsWith(QStringLiteral("Event"))) {
            map_->event = restAfterToken(line, t[1]);
        } else if (key.startsWith(QStringLiteral("Date"))) {
            parseDate(restAfterToken(line, t[1]));
        } else if (key.startsWith(QStringLiteral("//"))) {
            const QString next = t.value(i + 1);
            if (next.startsWith(QStringLiteral("LUG")))        map_->lug = restAfterToken(line, t.value(2));
            else if (next.startsWith(QStringLiteral("Event"))) map_->event = restAfterToken(line, t.value(2));
            else if (next.startsWith(QStringLiteral("Date")))  parseDate(restAfterToken(line, t.value(2)));
            else map_->comment += line.mid(5) + QLatin1Char('\n');
        } else if (key == QLatin1String("MLCAD")) {
            if (t.value(i + 1) == QLatin1String("HIDE")) {
                if (t.value(i + 2) == QLatin1String("0")) parseMeta(line, t, i + 3);
                else if (t.value(i + 2) == QLatin1String("1")) parseBrick(t, i + 3);
                if (layer_) layer_->visible = false;
            } else if (t.value(i + 1) == QLatin1String("BTG") && t.size() > 3) {
                pendingGroup_ = groupNamed(line.mid(line.indexOf(t[3])).trimmed()).guid;
            }
        } else if (key == QLatin1String("GROUP") && t.size() > 3) {
            PendingGroup& g = groupNamed(line.mid(line.indexOf(t[3])).trimmed());
            const QString parent = takePendingGroup();
            if (!parent.isEmpty()) g.parentGuid = parent;
        } else if (key == QLatin1String("!BLUEBRICK") && t.value(i + 1) == QLatin1String("RULER")) {
            parseRuler(t, i + 3);
        }
    }

    void parseDate(const QString& text) {
        const QDate d = QDate::fromString(text, kDateFormat);
        if (d.isValid()) map_->date = d;
    }

    void parseBrick(const QStringList& t, int i) {
        if (t.size() < i + 14) return;
        // Only parts; references to submodels (.ldr) are layers, not bricks.
        const QString file = t.mid(i + 13).join(QLatin1Char(' '));
        if (!file.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) return;
        const QString color = t[i];
        double x = t[i + 1].toDouble();
        const double y = t[i + 2].toDouble();
        double z = -t[i + 3].toDouble();
        const double a = t[i + 4].toDouble();
        const double c = t[i + 6].toDouble();
        QString pn = QFileInfo(QString(file).replace(QLatin1Char('\\'), QLatin1Char('/'))).completeBaseName().toUpper();
        QString partNumber = pn + QLatin1Char('.') + color;

        const auto meta = lib_.metadata(partNumber);
        if (meta && meta->isIgnorable()) return;
        double angle = qRadiansToDegrees(std::atan2(c, a));
        if (meta) {
            angle -= meta->ldrawAngle;
            const QPointF tr = meta->ldrawTranslation;
            if (!tr.isNull()) {
                const QPointF off = rotated(QPointF(-tr.x(), tr.y()), angle);
                x += off.x();
                z += off.y();
            }
            // The part's current number (old names map to it), upper-cased
            // like every part number BlueBrick writes.
            partNumber = (meta->colorCode.isEmpty() ? meta->partNumber
                                                    : meta->partNumber + QLatin1Char('.') + meta->colorCode).toUpper();
        } else {
            ++skippedUnknown_;
        }

        auto& layer = currentLayerOf<core::LayerBrick>(core::LayerKind::Brick);
        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = partNumber;
        b.orientation = static_cast<float>(angle);
        b.altitude = static_cast<float>(y);
        // The LDraw origin is the sprite centre.
        parts::placement::placeByImageCentre(b, QPointF(x / kLduPerStud, z / kLduPerStud), lib_);
        b.myGroupId = takePendingGroup();
        layer.bricks.push_back(std::move(b));
    }

    void parseRuler(const QStringList& t, int i) {
        // <type> #id dist unit color guideColor fontColor thick guideThick
        // [dash] unit "font" <geometry>. BlueBrick writes nothing at all for
        // an empty dash pattern, so detect that from the token count.
        const bool linear = t.value(i) == QLatin1String("LINEAR");
        const int geometry = linear ? 6 : 3;
        const bool hasDash = t.size() - (i + 1) >= 11 + geometry;
        int k = i + 2;  // skip type and id
        core::RulerItemBase common;
        common.displayDistance = t.value(k++) == QLatin1String("true");
        common.displayUnit = t.value(k++) == QLatin1String("true");
        common.color = readColor(t.value(k++));
        common.guidelineColor = readColor(t.value(k++));
        common.measureFontColor = readColor(t.value(k++));
        common.lineThickness = t.value(k++).toFloat();
        common.guidelineThickness = t.value(k++).toFloat();
        if (hasDash) {
            for (const QString& v : t.value(k++).split(QLatin1Char('|'), Qt::SkipEmptyParts))
                common.guidelineDashPattern.push_back(v.toFloat());
        }
        common.unit = t.value(k++).toInt();
        const QStringList font = t.value(k++).split(QLatin1Char('|'));
        common.measureFont.familyName = font.value(0);
        common.measureFont.sizePt = font.value(1, QStringLiteral("8")).toFloat();
        common.measureFont.styleString = font.value(2, QStringLiteral("Regular"));
        if (t.size() < k + geometry) return;

        core::LayerRuler::AnyRuler any;
        if (linear) {
            any.kind = core::RulerKind::Linear;
            static_cast<core::RulerItemBase&>(any.linear) = common;
            any.linear.point1 = readPointF(t[k++]);
            any.linear.point2 = readPointF(t[k++]);
            k += 2;  // attached brick ids: bricks get new ids on load, as in BlueBrick
            any.linear.allowOffset = t[k++] == QLatin1String("true");
            any.linear.offsetDistance = t[k++].toFloat();
            any.linear.displayArea = QRectF(any.linear.point1, any.linear.point2).normalized();
            any.linear.guid = core::newBbmId();
            any.linear.myGroupId = takePendingGroup();
        } else {
            any.kind = core::RulerKind::Circular;
            static_cast<core::RulerItemBase&>(any.circular) = common;
            any.circular.center = readPointF(t[k++]);
            any.circular.radius = t[k++].toFloat();
            const double r = any.circular.radius;
            any.circular.displayArea = QRectF(any.circular.center.x() - r, any.circular.center.y() - r, 2 * r, 2 * r);
            any.circular.guid = core::newBbmId();
            any.circular.myGroupId = takePendingGroup();
        }
        currentLayerOf<core::LayerRuler>(core::LayerKind::Ruler).rulers.push_back(std::move(any));
    }

    parts::PartsLibrary& lib_;
    std::unique_ptr<core::Map> map_;
    std::unique_ptr<core::Layer> layer_;
    QString layerName_;
    QHash<QString, PendingGroup> groups_;
    QString pendingGroup_;
    int skippedUnknown_ = 0;
};

// ------------------------------------------------------------------ writing

QString fmt(float v) { return saveload::xml::formatFloat(v); }
QString fmt(double v) { return fmt(static_cast<float>(v)); }

QString colorToken(const core::ColorSpec& c) {
    if (c.isKnown()) return QLatin1Char('"') + c.knownName + QStringLiteral("\" ");
    return QStringLiteral("\"0x%1\" ").arg(c.color.rgba(), 8, 16, QLatin1Char('0'));
}

class Writer {
public:
    Writer(const core::Map& map, parts::PartsLibrary& lib, QTextStream& out)
        : map_(map), lib_(lib), out_(out) {}

    void header(const QString& path) {
        line(QStringLiteral("0 ") + QFileInfo(path).completeBaseName());
        line(QStringLiteral("0 Name: ") + QFileInfo(path).fileName());
        line(QStringLiteral("0 Author: ") + map_.author);
        line(QStringLiteral("0 Unofficial Model"));
        line(QStringLiteral("0"));
        line(QStringLiteral("0 // LUG: ") + map_.lug);
        line(QStringLiteral("0 // Event: ") + map_.event);
        line(QStringLiteral("0 // Date: ") + map_.date.toString(kDateFormat));
        for (const QString& c : map_.comment.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts))
            line(QStringLiteral("0 // ") + c);
        line(QStringLiteral("0"));
    }

    void layer(const core::Layer& layer, bool useMlcadHide) {
        const bool hide = useMlcadHide && !layer.visible;
        switch (layer.kind()) {
            case core::LayerKind::Brick: bricks(static_cast<const core::LayerBrick&>(layer), hide); break;
            case core::LayerKind::Ruler: rulers(static_cast<const core::LayerRuler&>(layer), hide); break;
            case core::LayerKind::Area:  line(QStringLiteral("0 // Area layer not implemented yet, see you maybe in BB 1.9")); break;
            case core::LayerKind::Grid:  line(QStringLiteral("0 // Grid layer not implemented yet, see you maybe in BB 1.9")); break;
            case core::LayerKind::Text:  line(QStringLiteral("0 // Text layer not implemented yet, see you maybe in BB 1.9")); break;
            default: break;
        }
    }

    void line(const QString& text) { out_ << text << "\r\n"; }

private:
    static QString groupName(const core::Group& g) { return g.partNumber + QLatin1Char('#') + g.guid; }

    void belongsTo(const QString& groupGuid, const std::vector<core::Group>& groups) {
        if (groupGuid.isEmpty()) return;
        for (const auto& g : groups) {
            if (g.guid == groupGuid) {
                line(kBtg + groupName(g));
                usedGroups_.insert(g.guid);
                return;
            }
        }
    }

    void groupsOf(const std::vector<core::Group>& groups, const std::vector<QString>& memberGroupIds, bool hide) {
        // Include ancestors of used groups so nested groups survive.
        bool grew = true;
        while (grew) {
            grew = false;
            for (const auto& g : groups) {
                if (usedGroups_.contains(g.guid) && !g.myGroupId.isEmpty() && !usedGroups_.contains(g.myGroupId)) {
                    usedGroups_.insert(g.myGroupId);
                    grew = true;
                }
            }
        }
        for (const auto& g : groups) {
            if (!usedGroups_.contains(g.guid)) continue;
            int count = 0;
            for (const QString& id : memberGroupIds) if (id == g.guid) ++count;
            for (const auto& other : groups) if (other.myGroupId == g.guid) ++count;
            if (!g.myGroupId.isEmpty()) {
                for (const auto& p : groups) if (p.guid == g.myGroupId) line(kBtg + groupName(p));
            }
            line((hide ? kHide : QString()) + kGroup + QString::number(count) + QLatin1Char(' ') + groupName(g));
        }
        usedGroups_.clear();
    }

    // Same arithmetic as BlueBrick's saveOneBrickInLDRAW, in 32-bit float,
    // so the written numbers match it digit for digit.
    void oneBrick(const QString& partNumber, const QString& color, float altitude, float orientation,
                  float x, float z, const std::optional<parts::PartMetadata>& remap, bool hide) {
        x *= 20.0f;
        z *= 20.0f;
        float y = altitude;
        float angle = orientation;
        if (remap) {
            const QPointF tr = remap->ldrawTranslation;
            if (!tr.isNull()) {
                // GDI Matrix.Rotate(-angle) applied to (tx, ty).
                const double r = qDegreesToRadians(static_cast<double>(-angle));
                const float c = static_cast<float>(std::cos(r)), s = static_cast<float>(std::sin(r));
                const float tx = static_cast<float>(tr.x()), ty = static_cast<float>(tr.y());
                x += tx * c - ty * s;
                z += tx * s + ty * c;
            }
            if (y == 0.0f) y = static_cast<float>(remap->ldrawPreferredHeight);
            angle += static_cast<float>(remap->ldrawAngle);
        }
        angle *= static_cast<float>(M_PI) / 180.0f;
        const float cosA = static_cast<float>(std::cos(static_cast<double>(angle)));
        const float sinA = static_cast<float>(std::sin(static_cast<double>(angle)));
        const QString cs = fmt(cosA), sn = fmt(sinA), msn = fmt(-sinA);
        line((hide ? kHide : QString()) + QStringLiteral("1 ") + color + QLatin1Char(' ')
             + fmt(x) + QLatin1Char(' ') + fmt(y) + QLatin1Char(' ') + fmt(z) + QLatin1Char(' ')
             + cs + QStringLiteral(" 0 ") + sn + QStringLiteral(" 0 1 0 ") + msn + QStringLiteral(" 0 ") + cs
             + QLatin1Char(' ') + partNumber + QStringLiteral(".DAT"));
    }

    void bricks(const core::LayerBrick& layer, bool hide) {
        QHash<QString, const core::Brick*> byConnection;  // connection guid -> brick
        for (const auto& b : layer.bricks)
            for (const auto& c : b.connections) byConnection.insert(c.guid, &b);
        QSet<QString> sleepered;  // connection guids that already got a sleeper
        std::vector<QString> members;

        for (const auto& b : layer.bricks) {
            auto [pn, color] = splitPartAndColor(b.partNumber);
            bool numeric = false;
            color.toInt(&numeric);
            if (!numeric) continue;  // sets, logos, custom parts
            const QPointF centre = parts::placement::imageCentre(b, lib_);
            const auto meta = lib_.metadata(b.partNumber);
            if (meta && !meta->ldrawAlias.isEmpty()) {
                auto [aliasPn, aliasColor] = splitPartAndColor(meta->ldrawAlias);
                pn = aliasPn;
                if (!aliasColor.isEmpty()) color = aliasColor;
            }
            belongsTo(b.myGroupId, layer.groups);
            members.push_back(b.myGroupId);
            oneBrick(pn, color, b.altitude, b.orientation, static_cast<float>(centre.x()),
                     static_cast<float>(-centre.y()), meta, hide);
            if (!meta || meta->ldrawSleeper.isEmpty() || meta->connections.isEmpty()) continue;

            // Rails get a sleeper at every end (once per joint).
            const auto [sleeperPn, sleeperColor] = splitPartAndColor(meta->ldrawSleeper);
            const auto sleeperMeta = lib_.metadata(meta->ldrawSleeper);
            float sleeperAltitude = b.altitude;
            if (sleeperMeta && sleeperAltitude != 0.0f)
                sleeperAltitude += static_cast<float>(sleeperMeta->ldrawPreferredHeight - meta->ldrawPreferredHeight);
            for (int i = 0; i < meta->connections.size() && i < static_cast<int>(b.connections.size()); ++i) {
                const auto& conn = b.connections[i];
                bool add = true;
                if (!conn.linkedToId.isEmpty()) {
                    if (sleepered.contains(conn.linkedToId)) {
                        add = false;
                    } else if (sleeperPn == QLatin1String("767")) {
                        // 12V grey sleepers clip together; next to a plain
                        // plate the plate wins.
                        const core::Brick* other = byConnection.value(conn.linkedToId);
                        const auto otherMeta = other ? lib_.metadata(other->partNumber) : std::nullopt;
                        if (otherMeta && !otherMeta->ldrawSleeper.isEmpty())
                            add = splitPartAndColor(otherMeta->ldrawSleeper).first == QLatin1String("767");
                    }
                }
                if (!add) continue;
                const auto& cm = meta->connections[i];
                const QPointF at = parts::placement::connectionWorld(b, i, lib_);
                oneBrick(sleeperPn, sleeperColor, sleeperAltitude,
                         b.orientation + static_cast<float>(cm.angleDegrees),
                         static_cast<float>(at.x()), static_cast<float>(-at.y()), sleeperMeta, hide);
                sleepered.insert(conn.guid);
            }
        }
        groupsOf(layer.groups, members, hide);
    }

    void rulers(const core::LayerRuler& layer, bool hide) {
        std::vector<QString> members;
        for (const auto& any : layer.rulers) {
            const core::RulerItemBase& r = any.kind == core::RulerKind::Linear
                ? static_cast<const core::RulerItemBase&>(any.linear)
                : static_cast<const core::RulerItemBase&>(any.circular);
            belongsTo(r.myGroupId, layer.groups);
            members.push_back(r.myGroupId);
            QString text = (hide ? kHide : QString()) + kRuler;
            text += any.kind == core::RulerKind::Linear ? QStringLiteral("LINEAR ") : QStringLiteral("CIRCULAR ");
            text += QLatin1Char('#') + r.guid + QLatin1Char(' ');
            text += (r.displayDistance ? QStringLiteral("true ") : QStringLiteral("false "));
            text += (r.displayUnit ? QStringLiteral("true ") : QStringLiteral("false "));
            text += colorToken(r.color) + colorToken(r.guidelineColor) + colorToken(r.measureFontColor);
            text += fmt(r.lineThickness) + QLatin1Char(' ') + fmt(r.guidelineThickness) + QLatin1Char(' ');
            QStringList dash;
            for (float d : r.guidelineDashPattern) dash << fmt(d);
            if (!dash.isEmpty()) text += dash.join(QLatin1Char('|')) + QLatin1Char(' ');
            text += QString::number(r.unit) + QLatin1Char(' ');
            text += QLatin1Char('"') + r.measureFont.familyName + QLatin1Char('|') + fmt(r.measureFont.sizePt)
                    + QLatin1Char('|') + r.measureFont.styleString + QStringLiteral("\" ");
            const auto point = [](QPointF p) { return fmt(p.x()) + QLatin1Char('|') + fmt(p.y()) + QLatin1Char(' '); };
            const auto id = [](const QString& guid) { return QLatin1Char('#') + guid + QLatin1Char(' '); };
            if (any.kind == core::RulerKind::Linear) {
                text += point(any.linear.point1) + point(any.linear.point2);
                text += id(any.linear.attachedBrick1Id) + id(any.linear.attachedBrick2Id);
                text += (any.linear.allowOffset ? QStringLiteral("true ") : QStringLiteral("false "));
                text += fmt(any.linear.offsetDistance) + QLatin1Char(' ');
            } else {
                text += point(any.circular.center) + fmt(any.circular.radius) + QLatin1Char(' ');
                text += id(any.circular.attachedBrickId);
            }
            line(text);
        }
        groupsOf(layer.groups, members, hide);
    }

    const core::Map& map_;
    parts::PartsLibrary& lib_;
    QTextStream& out_;
    QSet<QString> usedGroups_;
};

}  // namespace

MapReadResult readLDrawMap(const QString& path, parts::PartsLibrary& lib) {
    return Reader(lib).run(path);
}

bool writeLDrawMap(const core::Map& map, const QString& path, parts::PartsLibrary& lib, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    Writer w(map, lib, out);
    if (path.endsWith(QStringLiteral(".mpd"), Qt::CaseInsensitive)) {
        // Main model referencing one submodel per layer, then the submodels.
        const QString base = QFileInfo(path).completeBaseName();
        w.line(QStringLiteral("0 FILE ") + base + QStringLiteral(".ldr"));
        w.header(path);
        QStringList names;
        for (const auto& layer : map.layers()) {
            QString name = layer->name.trimmed();
            name.replace(QLatin1Char(' '), QLatin1Char('_')).replace(QLatin1Char('\t'), QLatin1Char('_'));
            names << name + QStringLiteral(".ldr");
            w.line((layer->visible ? QString() : kHide) + QStringLiteral("1 1 0 0 0 1 0 0 0 1 0 0 0 1 ") + names.last());
        }
        w.line(QStringLiteral("0"));
        for (size_t i = 0; i < map.layers().size(); ++i) {
            w.line(QStringLiteral("0 FILE ") + names[static_cast<int>(i)]);
            w.line(QStringLiteral("0 Author: ") + map.author);
            w.line(QStringLiteral("0 Unofficial Model"));
            w.line(QStringLiteral("0"));
            w.layer(*map.layers()[i], false);
            w.line(QStringLiteral("0"));
        }
    } else {
        w.header(path);
        w.line(QStringLiteral("0"));
        for (const auto& layer : map.layers()) {
            w.layer(*layer, true);
            w.line(QStringLiteral("0 STEP"));
        }
    }
    out.flush();
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace bld::import
