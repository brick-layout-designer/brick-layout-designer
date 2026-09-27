#include "TrackDesignerMap.h"

#include "../../parts/BrickPlacement.h"

#include "../../core/Ids.h"
#include "../../core/LayerBrick.h"
#include "../../core/Map.h"
#include "../../edit/Connectivity.h"

#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace bld::import {

namespace {

constexpr int kFileVersion = 20;
constexpr int kMonorailUpRamp = 232677;
constexpr int kMonorailDownRamp = 232678;  // not a TD part: the up ramp's second geometry

// Little-endian reader over the whole file, mirroring .NET's BinaryReader
// (whose ReadChar/ReadChars decode UTF-8).
class Reader {
public:
    explicit Reader(QByteArray data) : d_(std::move(data)) {}
    bool atEnd() const { return pos_ >= d_.size(); }
    qint64 remaining() const { return d_.size() - pos_; }
    bool ok() const { return ok_; }
    void skip(qint64 n) { if (!need(n)) return; pos_ += n; }

    template <typename T> T read() {
        if (!need(sizeof(T))) return T{};
        T v;
        std::memcpy(&v, d_.constData() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return qFromLittleEndian(v);
    }
    double readDouble() {
        const quint64 bits = read<quint64>();
        double v;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    // One UTF-8 character, as BinaryReader.ReadChar.
    uint readChar() {
        if (!need(1)) return 0;
        const auto lead = static_cast<uchar>(d_[pos_]);
        const int len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
        if (!need(len)) return 0;
        const QString s = QString::fromUtf8(d_.constData() + pos_, len);
        pos_ += len;
        return s.isEmpty() ? 0 : s.at(0).unicode();
    }
    QString readChars(uint count) {
        QString out;
        for (uint i = 0; i < count && ok_; ++i) out += QChar(static_cast<char16_t>(readChar()));
        return out;
    }

private:
    bool need(qint64 n) {
        if (pos_ + n > d_.size()) ok_ = false;
        return ok_;
    }
    QByteArray d_;
    qint64 pos_ = 0;
    bool ok_ = true;
};

class Writer {
public:
    template <typename T> void write(T v) {
        v = qToLittleEndian(v);
        d_.append(reinterpret_cast<const char*>(&v), sizeof v);
    }
    void writeDouble(double v) {
        quint64 bits;
        std::memcpy(&bits, &v, sizeof bits);
        write<quint64>(bits);
    }
    // .NET BinaryWriter.Write(char) / Write(char[]): UTF-8.
    void writeChars(const QString& s) { d_.append(s.toUtf8()); }
    void writeChar(uint c) { d_.append(QString(QChar(static_cast<char16_t>(c))).toUtf8()); }
    void patch16(qint64 pos, qint16 v) {
        v = qToLittleEndian(v);
        std::memcpy(d_.data() + pos, &v, sizeof v);
    }
    qint64 size() const { return d_.size(); }
    const QByteArray& bytes() const { return d_; }

private:
    QByteArray d_;
};

QString partNumberOf(const parts::PartMetadata& meta) {
    return (meta.colorCode.isEmpty() ? meta.partNumber : meta.partNumber + QLatin1Char('.') + meta.colorCode).toUpper();
}

float connectionAngle(const parts::PartMetadata& meta, int index) {
    return index >= 0 && index < meta.connections.size() ? static_cast<float>(meta.connections[index].angleDegrees) : 0.0f;
}

// BrickLibrary.getConnectionAngleDifference / AddConnectBrick orientation.
float connectedOrientation(const core::Brick& fixed, const parts::PartMetadata& fixedMeta,
                           const parts::PartMetadata& attachMeta, int attachIndex) {
    float diff = connectionAngle(fixedMeta, fixed.activeConnectionPointIndex) + 180.0f
               - connectionAngle(attachMeta, attachIndex);
    if (diff >= 360.0f) diff -= 360.0f;
    if (diff < 0.0f) diff += 360.0f;
    float o = fixed.orientation + diff;
    if (o >= 360.0f) o -= 360.0f;
    if (o < 0.0f) o += 360.0f;
    return o;
}

}  // namespace

MapReadResult readTrackDesignerMap(const QString& path, parts::PartsLibrary& lib) {
    MapReadResult out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        out.error = f.errorString();
        return out;
    }
    Reader in(f.readAll());
    auto map = std::make_unique<core::Map>();

    // ---- header
    in.skip(8);                       // origin
    in.read<qint32>();                // piece count
    if (in.read<qint32>() != kFileVersion) {
        out.error = QStringLiteral("Only TrackDesigner files of version %1 can be read.").arg(kFileVersion);
        return out;
    }
    in.skip(16);                      // bounds
    in.skip(32);                      // cursor x, y, z, angle
    in.skip(16);                      // selected port, part, document size
    in.readChars(in.readChar());      // old empty CString
    map->trackDesigner.allowElectricShortCuts = in.read<qint32>() != 0;
    in.read<qint32>();                // slope
    map->trackDesigner.allowUnderground = in.read<qint32>() != 0;
    map->trackDesigner.allowSteps = in.read<qint32>() != 0;
    map->trackDesigner.allowSlopeMismatch = in.read<qint32>() != 0;
    map->event = in.readChars(in.readChar());
    map->comment = in.readChars(in.readChar());
    const qint16 pieceList = in.read<qint16>();
    if (pieceList > 0) in.skip(12 + qint64(pieceList) * 40);
    in.read<qint16>();                // CTrackPiece count
    in.skip(17);                      // CTrackPiece list header
    if (!in.ok()) {
        out.error = QStringLiteral("Truncated TrackDesigner file.");
        return out;
    }

    // ---- pieces
    std::vector<core::Brick> baseplate, rail, monorail;
    QList<int> unmapped;
    bool end = in.atEnd();
    while (!end) {
        int tdId = in.read<qint32>();
        in.read<qint32>();            // instance pointer
        const double angle = in.readDouble();
        double x = in.readDouble();
        double y = in.readDouble();
        in.readDouble();              // z
        in.read<qint32>();            // piece type
        const int port = in.read<qint32>();
        // 4 x (instance, port, polarity), flags, slope, and the next piece's
        // separator word; the last piece has no separator.
        if (in.remaining() >= 58) in.skip(58);
        else end = true;
        if (!in.ok()) break;

        if (tdId == kMonorailUpRamp && port == 1) tdId = kMonorailDownRamp;
        const QString key = lib.partForTrackDesignerId(tdId);
        const auto meta = key.isEmpty() ? std::nullopt : lib.metadata(key);
        if (!meta) {
            if (!unmapped.contains(tdId)) unmapped << tdId;
            continue;
        }

        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = partNumberOf(*meta);
        std::vector<core::Brick>* layer = &baseplate;
        if (!meta->connections.isEmpty()) {
            // BlueBrick numbers connection types by their order in its
            // ConnectionTypeList.xml ("1" = 1, "narrow" = 2, "2" = 3,
            // "mono" = 4) and files types 1 as rail, 3 and 4 as monorail.
            const QString type = meta->connections[0].type;
            if (type == QLatin1String("1")) layer = &rail;
            else if (type == QLatin1String("2") || type == QLatin1String("mono")) layer = &monorail;
        }

        float diff = 0.0f;
        const auto& ports = meta->trackDesigner->ports;
        if (port >= 0 && port < ports.size()) {
            b.activeConnectionPointIndex = ports[port].bbConnectionIndex;
            diff = ports[port].angleDifference;
        }
        if (!meta->connections.isEmpty()) {
            // Parts with connections are positioned by their active one.
            b.orientation = static_cast<float>(angle) + diff;
            parts::placement::placeByConnection(b, b.activeConnectionPointIndex, QPointF(x, y), lib);
        } else {
            // Otherwise TD's origin is the middle of the part's left edge.
            const auto fpTd = lib.footprint(b.partNumber, diff);
            const double width = fpTd ? fpTd->size.width() : 2.0;
            const QPointF toCentre = parts::placement::rotated(QPointF(width / 2.0, 0.0), angle);
            b.orientation = static_cast<float>(angle) + diff;
            const auto fp = lib.footprint(b.partNumber, b.orientation);
            const QSizeF size = fp ? fp->size : QSizeF(2, 2);
            const QPointF c(x + toCentre.x(), y + toCentre.y());
            b.displayArea = QRectF(c.x() - size.width() / 2.0, c.y() - size.height() / 2.0, size.width(), size.height());
        }

        // TD's monorail ramp is one piece; BlueBrick has an up and a down half.
        if (tdId == kMonorailUpRamp || tdId == kMonorailDownRamp) {
            const bool up = tdId == kMonorailUpRamp;
            const auto rampMeta = lib.metadata(up ? QStringLiteral("2678.7") : QStringLiteral("2677.7"));
            b.activeConnectionPointIndex = up ? 1 : 0;
            if (rampMeta) {
                core::Brick ramp;
                ramp.guid = core::newBbmId();
                ramp.partNumber = partNumberOf(*rampMeta);
                ramp.activeConnectionPointIndex = up ? 0 : 1;
                ramp.orientation = connectedOrientation(b, *meta, *rampMeta, ramp.activeConnectionPointIndex);
                parts::placement::placeByConnection(ramp, ramp.activeConnectionPointIndex,
                                             parts::placement::connectionWorld(b, b.activeConnectionPointIndex, lib), lib);
                layer->push_back(b);
                layer->push_back(ramp);
                continue;
            }
        }
        layer->push_back(std::move(b));
    }

    const auto addLayer = [&](std::vector<core::Brick>& bricks, const QString& name) {
        if (bricks.empty()) return;
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = core::newBbmId();
        layer->name = name;
        layer->bricks = std::move(bricks);
        map->layers().push_back(std::move(layer));
    };
    addLayer(baseplate, QStringLiteral("Baseplate"));
    addLayer(rail, QStringLiteral("Rail"));
    addLayer(monorail, QStringLiteral("Monorail"));
    edit::rebuildConnectivity(*map, lib);

    if (!unmapped.isEmpty()) {
        QStringList ids;
        for (int id : unmapped) ids << QString::number(id);
        out.warnings << QStringLiteral("No part is mapped to these TrackDesigner ids: %1").arg(ids.join(QStringLiteral(", ")));
    }
    out.map = std::move(map);
    return out;
}

bool writeTrackDesignerMap(const core::Map& map, const QString& path, parts::PartsLibrary& lib, QString* error) {
    // Instance ids (BlueBrick uses object hash codes) and connection owners.
    QHash<const core::Brick*, qint32> instance;
    struct Owner { const core::Brick* brick; int index; };
    QHash<QString, Owner> connectionOwner;
    int nbItems = 0;
    QRectF bounds;
    QPointF topLeft(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
            instance.insert(&b, ++nbItems);
            for (int i = 0; i < static_cast<int>(b.connections.size()); ++i)
                connectionOwner.insert(b.connections[i].guid, { &b, i });
            if (layer->visible) bounds |= b.displayArea;
            topLeft.setX(std::min(topLeft.x(), b.displayArea.x()));
            topLeft.setY(std::min(topLeft.y(), b.displayArea.y()));
        }
    }
    if (nbItems == 0) topLeft = QPointF();

    Writer w;
    // ---- header
    constexpr int margin = 5;
    // Clamped so bricks at absurd (or NaN) positions from a damaged file
    // can't overflow the header's 32-bit fields.
    const auto studs = [](double v) {
        if (!(v > -1e8)) return -100000000;
        if (!(v < 1e8)) return 100000000;
        return static_cast<int>(std::round(v));
    };
    const int bx = studs(bounds.x()) - margin;
    const int by = studs(bounds.y()) - margin;
    const int bw = studs(bounds.width()) + margin * 2;
    const int bh = studs(bounds.height()) + margin * 2;
    w.write<qint32>(-bx);
    w.write<qint32>(-by);
    w.write<qint32>(nbItems);
    w.write<qint32>(kFileVersion);
    w.write<qint32>(bx);
    w.write<qint32>(by);
    w.write<qint32>(bx + bw);
    w.write<qint32>(by + bh);
    w.writeDouble(topLeft.x());  // cursor: BlueBrick uses the selection, or the top-left brick
    w.writeDouble(topLeft.y());
    w.writeDouble(0.0);
    w.writeDouble(0.0);
    w.write<qint32>(0);          // selected port
    w.write<qint32>(0);          // selected part
    w.write<qint32>(bw);
    w.write<qint32>(bh);
    w.writeChar(0);              // old empty CString
    w.write<qint32>(map.trackDesigner.allowElectricShortCuts ? 1 : 0);
    w.write<qint32>(0);          // slope
    w.write<qint32>(map.trackDesigner.allowUnderground ? 1 : 0);
    w.write<qint32>(map.trackDesigner.allowSteps ? 1 : 0);
    w.write<qint32>(map.trackDesigner.allowSlopeMismatch ? 1 : 0);
    // Strings are length-prefixed by one character; TrackDesigner reads that
    // as a byte, so keep them under 128 characters.
    const auto shortString = [&](const QString& s) {
        const QString t = s.left(127);
        w.writeChar(static_cast<uint>(t.size()));
        w.writeChars(t);
    };
    shortString(map.event);
    shortString(map.comment);
    w.write<qint16>(0);          // piece list
    const qint64 countPos = w.size();
    w.write<qint16>(static_cast<qint16>(nbItems));

    int written = 0;
    if (nbItems > 0) {
        w.write<qint32>(0x14FFFF);
        w.write<qint16>(0x0B);
        w.writeChars(QStringLiteral("CTrackPiece"));
        for (const auto& layer : map.layers()) {
            if (layer->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
                const auto meta = lib.metadata(b.partNumber);
                if (!meta || !meta->trackDesigner) continue;
                const auto& td = *meta->trackDesigner;
                if (td.id() == kMonorailDownRamp) continue;  // merged into the up ramp in TD
                if (written > 0) w.write<quint16>(0x8001);
                w.write<qint32>(td.id());
                w.write<qint32>(instance.value(&b));

                const int connectionIndex = td.hasSeveralPorts ? b.activeConnectionPointIndex : 0;
                int port = -1, type = 0;
                float diff = 0.0f;
                for (const auto& p : td.ports) {
                    ++port;
                    if (p.bbConnectionIndex == connectionIndex) { type = p.type; diff = p.angleDifference; break; }
                }
                double orientation = static_cast<double>(b.orientation - diff);
                QPointF position;
                if (!meta->connections.isEmpty() && connectionIndex < meta->connections.size()) {
                    position = parts::placement::connectionWorld(b, connectionIndex, lib);
                } else {
                    position = b.displayArea.center();
                    const auto fpTd = lib.footprint(b.partNumber, diff);
                    const double width = fpTd ? fpTd->size.width() : b.displayArea.width();
                    position -= parts::placement::rotated(QPointF(width / 2.0, 0.0), orientation);
                }
                orientation = std::fmod(orientation, 360.0);  // [0, 360); no loop on absurd angles
                if (orientation < 0.0) orientation += 360.0;
                w.writeDouble(orientation);
                w.writeDouble(position.x());
                w.writeDouble(position.y());
                w.writeDouble(b.altitude);
                w.write<qint32>(type);
                w.write<qint32>(port);

                for (int i = 0; i < 4; ++i) {
                    qint32 otherInstance = 0, otherPort = 0;
                    if (i < td.ports.size()) {
                        const int bbIndex = td.ports[i].bbConnectionIndex;
                        Owner linked{ nullptr, 0 };
                        if (bbIndex < static_cast<int>(b.connections.size()))
                            linked = connectionOwner.value(b.connections[bbIndex].linkedToId, { nullptr, 0 });
                        // The down ramp doesn't exist in TD: step through it.
                        // Bounded: damaged links can form a ring of down ramps.
                        for (int hop = 0; linked.brick && linked.brick->partNumber.startsWith(QStringLiteral("2678.")); ++hop) {
                            if (hop > nbItems) { linked = { nullptr, 0 }; break; }
                            const int next = linked.index == 0 ? 1 : 0;
                            if (next >= static_cast<int>(linked.brick->connections.size())) { linked = { nullptr, 0 }; break; }
                            linked = connectionOwner.value(linked.brick->connections[next].linkedToId, { nullptr, 0 });
                        }
                        if (linked.brick) {
                            otherInstance = instance.value(linked.brick);
                            const auto otherMeta = lib.metadata(linked.brick->partNumber);
                            if (otherMeta && otherMeta->trackDesigner) {
                                const auto& otherPorts = otherMeta->trackDesigner->ports;
                                for (int j = 0; j < otherPorts.size(); ++j) {
                                    if (otherPorts[j].bbConnectionIndex == linked.index) { otherPort = j; break; }
                                }
                            }
                        }
                    }
                    w.write<qint32>(otherInstance);
                    w.write<qint32>(otherPort);
                    w.write<qint32>(0);  // polarity: unassigned
                }
                w.write<qint32>(td.flags);
                w.write<qint32>(0);      // slope
                ++written;
            }
        }
    }
    QByteArray bytes = w.bytes();
    if (written < nbItems) {
        // Pieces without a TrackDesigner remap were skipped.
        const qint32 n32 = qToLittleEndian<qint32>(written);
        std::memcpy(bytes.data() + 8, &n32, sizeof n32);
        const qint16 n16 = qToLittleEndian<qint16>(static_cast<qint16>(written));
        std::memcpy(bytes.data() + countPos, &n16, sizeof n16);
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace bld::import
