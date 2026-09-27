// LDraw (.ldr / .mpd) maps, checked against vanilla BlueBrick 1.9.2's own
// output (fixtures/bluebrick-oracle, made with scripts/bluebrick-oracle).

#include "import/mapformats/FourDBrixMap.h"
#include "import/mapformats/LDrawMap.h"
#include "import/mapformats/TrackDesignerMap.h"
#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace bld;

namespace {

QString oracle(const QString& name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + name;
}
QString corpus(const QString& name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/") + name;
}

class LDrawMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
        if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
        lib_.addSearchPath(root);
        lib_.scan();
    }

    // Lines of an LDraw file, minus BlueBrick's file-name header (the
    // oracle's map was unnamed) and parts the library doesn't have (their
    // placeholder geometry is BlueBrick-specific).
    QStringList comparableLines(const QString& path) {
        QFile f(path);
        EXPECT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QStringList out;
        for (const QString& raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            const QString line = QString(raw).remove(QLatin1Char('\r'));
            if (line.startsWith(QStringLiteral("0 FILE ")) && out.isEmpty()) continue;
            if (line.startsWith(QStringLiteral("0 Name: "))) continue;
            if (out.isEmpty() && line.startsWith(QStringLiteral("0 ")) && !line.contains(QLatin1Char(':'))) continue;
            if (line.startsWith(QStringLiteral("1 "))) {
                const QString part = line.section(QLatin1Char(' '), 14).chopped(4);
                const QString colour = line.section(QLatin1Char(' '), 1, 1);
                if (!lib_.metadata(part + QLatin1Char('.') + colour) && !lib_.metadata(part)) continue;
            }
            out << line;
        }
        return out;
    }

    struct Key { QString part; double x, y, w, h, angle; int active; };

    // Every kept brick's part, displayArea and orientation.
    static std::vector<Key> bricksOf(const core::Map& map, const std::function<bool(const QString&)>& keep) {
        std::vector<Key> out;
        for (const auto& layer : map.layers()) {
            if (layer->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
                if (!keep(b.partNumber)) continue;
                const QRectF a = b.displayArea;
                out.push_back({ b.partNumber.toUpper(), a.x(), a.y(), a.width(), a.height(), b.orientation, b.activeConnectionPointIndex });
            }
        }
        return out;
    }

    // Pairs every brick with an equal one (within 0.01 studs / degrees);
    // describes the leftovers, empty when they all match.
    static std::string unmatched(std::vector<Key> ours, std::vector<Key> theirs, bool compareActive = true) {
        const auto same = [compareActive](const Key& a, const Key& b) {
            const double da = std::remainder(a.angle - b.angle, 360.0);
            return a.part == b.part && std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01
                && std::abs(a.w - b.w) < 0.01 && std::abs(a.h - b.h) < 0.01 && std::abs(da) < 0.01
                && (!compareActive || a.active == b.active);
        };
        for (auto it = ours.begin(); it != ours.end();) {
            const auto m = std::find_if(theirs.begin(), theirs.end(), [&](const Key& k) { return same(*it, k); });
            if (m != theirs.end()) { theirs.erase(m); it = ours.erase(it); } else { ++it; }
        }
        const auto text = [](const Key& k) {
            return QStringLiteral("%1 (%2, %3) %4x%5 @%6").arg(k.part).arg(k.x).arg(k.y).arg(k.w).arg(k.h)
                .arg(k.angle).toStdString() + " active " + std::to_string(k.active);
        };
        std::string out;
        for (size_t i = 0; i < ours.size() && i < 8; ++i) out += "\n  first only:  " + text(ours[i]);
        for (size_t i = 0; i < theirs.size() && i < 8; ++i) out += "\n  second only: " + text(theirs[i]);
        return out;
    }

    parts::PartsLibrary lib_;
    QTemporaryDir tmp_;
};

}  // namespace

TEST_F(LDrawMapTest, WriteMatchesBlueBrick) {
    auto map = saveload::readBbm(corpus(QStringLiteral("tight-corner.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib_);  // sleepers depend on the links
    for (const QString& ext : { QStringLiteral("ldr"), QStringLiteral("mpd") }) {
        SCOPED_TRACE(ext.toStdString());
        const QString out = tmp_.filePath(QStringLiteral("tight-corner.") + ext);
        QString err;
        ASSERT_TRUE(import::writeLDrawMap(*map, out, lib_, &err)) << err.toStdString();
        const QStringList ours = comparableLines(out);
        const QStringList vanilla = comparableLines(oracle(QStringLiteral("tight-corner.") + ext));
        ASSERT_EQ(ours.size(), vanilla.size());
        for (int i = 0; i < ours.size(); ++i) ASSERT_EQ(ours[i], vanilla[i]) << "line " << i;
    }
}

TEST_F(LDrawMapTest, ReadMatchesBlueBrick) {
    // What vanilla BlueBrick itself makes of each file (read, saved as .bbm):
    // parts, positions, rotations and active connections must agree.
    for (const char* ext : { "ldr", "mpd", "tdl" }) {
        SCOPED_TRACE(ext);
        const QString file = oracle(QStringLiteral("tight-corner.") + QLatin1String(ext));
        const auto vanilla = saveload::readBbm(oracle(QStringLiteral("tight-corner.from-") + QLatin1String(ext) + QStringLiteral(".bbm"))).map;
        ASSERT_TRUE(vanilla);
        auto ours = QLatin1String(ext) == QLatin1String("tdl") ? import::readTrackDesignerMap(file, lib_)
                                                               : import::readLDrawMap(file, lib_);
        ASSERT_TRUE(ours.ok()) << ours.error.toStdString();
        // BlueBrick drops parts with a space in their name when reading LDraw.
        const auto noSpace = [](const QString& pn) { return !pn.contains(QLatin1Char(' ')); };
        EXPECT_EQ(unmatched(bricksOf(*ours.map, noSpace), bricksOf(*vanilla, noSpace)), "");
    }
}

namespace {

// Pieces of a TrackDesigner file, with instance ids replaced by piece
// indices (BlueBrick writes object hash codes) and polarity dropped.
std::vector<std::string> tdlPieces(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray d = f.readAll();
    qint64 pos = 0;
    const auto i32 = [&]() { qint32 v; std::memcpy(&v, d.constData() + pos, 4); pos += 4; return v; };
    const auto i16 = [&]() { qint16 v; std::memcpy(&v, d.constData() + pos, 2); pos += 2; return v; };
    const auto f64 = [&]() { double v; std::memcpy(&v, d.constData() + pos, 8); pos += 8; return v; };
    const auto str = [&]() { const int n = static_cast<uchar>(d[pos++]); pos += n; };  // ASCII in these fixtures
    pos = 4 * 4 + 4 * 4 + 4 * 8 + 4 * 4;
    str();
    pos += 5 * 4;
    str();
    str();
    const int pieceList = i16();
    pos += pieceList > 0 ? 12 + pieceList * 40 : 0;
    const int count = i16();
    if (count <= 0) return {};
    pos += 17;
    struct Piece { qint32 id, inst; double a, x, y, z; qint32 type, port; qint32 conn[4][2]; qint32 flags; };
    std::vector<Piece> pieces;
    while (pos < d.size()) {
        Piece p{};
        p.id = i32(); p.inst = i32(); p.a = f64(); p.x = f64(); p.y = f64(); p.z = f64();
        p.type = i32(); p.port = i32();
        for (auto& c : p.conn) { c[0] = i32(); c[1] = i32(); i32(); }
        p.flags = i32(); i32();
        pieces.push_back(p);
        if (pos < d.size()) pos += 2;
    }
    QHash<qint32, int> index;
    for (int i = 0; i < static_cast<int>(pieces.size()); ++i) index.insert(pieces[i].inst, i);
    std::vector<std::string> out;
    for (const auto& p : pieces) {
        QString text = QStringLiteral("%1 %2 %3 %4 %5 %6 %7").arg(p.id).arg(p.a, 0, 'f', 3).arg(p.x, 0, 'f', 3)
            .arg(p.y, 0, 'f', 3).arg(p.z, 0, 'f', 3).arg(p.type).arg(p.port);
        for (const auto& c : p.conn) {
            const int other = c[0] ? index.value(c[0], -1) : -1;
            text += other >= 0 ? QStringLiteral(" %1:%2").arg(other).arg(c[1]) : QStringLiteral(" -");
        }
        out.push_back((text + QStringLiteral(" f%1").arg(p.flags)).toStdString());
    }
    return out;
}

}  // namespace

TEST_F(LDrawMapTest, TrackDesignerWriteMatchesBlueBrick) {
    auto map = saveload::readBbm(corpus(QStringLiteral("tight-corner.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib_);
    const QString out = tmp_.filePath(QStringLiteral("tight-corner.tdl"));
    QString err;
    ASSERT_TRUE(import::writeTrackDesignerMap(*map, out, lib_, &err)) << err.toStdString();
    const auto ours = tdlPieces(out), vanilla = tdlPieces(oracle(QStringLiteral("tight-corner.tdl")));
    ASSERT_EQ(ours.size(), vanilla.size());
    ASSERT_FALSE(ours.empty());
    for (size_t i = 0; i < ours.size(); ++i) EXPECT_EQ(ours[i], vanilla[i]) << "piece " << i;
}

TEST_F(LDrawMapTest, RoundTripKeepsEveryBrick) {
    auto map = saveload::readBbm(corpus(QStringLiteral("fordyce-2026.bbm"))).map;
    ASSERT_TRUE(map);
    const QString out = tmp_.filePath(QStringLiteral("fordyce.mpd"));
    ASSERT_TRUE(import::writeLDrawMap(*map, out, lib_));
    auto back = import::readLDrawMap(out, lib_);
    ASSERT_TRUE(back.ok());
    // Parts the library lacks come back with a placeholder size, and parts
    // without a numeric colour aren't written (as in BlueBrick).
    const auto writable = [&](const QString& pn) {
        bool numeric = false;
        pn.section(QLatin1Char('.'), -1).toInt(&numeric);
        return numeric && lib_.metadata(pn).has_value();
    };
    EXPECT_EQ(unmatched(bricksOf(*back.map, writable), bricksOf(*map, writable), false), "");
}

TEST_F(LDrawMapTest, GroupsRulersAndHiddenLayersSurvive) {
    core::Map map;
    auto bricks = std::make_unique<core::LayerBrick>();
    bricks->name = QStringLiteral("Track");
    bricks->visible = false;
    core::Group group;
    group.guid = core::newBbmId();
    group.partNumber = QStringLiteral("MYSET");
    for (int i = 0; i < 2; ++i) {
        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = QStringLiteral("2865.8");
        b.displayArea = QRectF(i * 16.0 - 8.0, -4.0, 16.0, 8.0);
        b.myGroupId = group.guid;
        bricks->bricks.push_back(b);
    }
    bricks->groups.push_back(group);
    map.layers().push_back(std::move(bricks));
    auto rulers = std::make_unique<core::LayerRuler>();
    core::LayerRuler::AnyRuler lin;
    lin.kind = core::RulerKind::Linear;
    lin.linear.point1 = QPointF(1, 2);
    lin.linear.point2 = QPointF(30, 2);
    lin.linear.offsetDistance = -4;
    lin.linear.allowOffset = true;
    lin.linear.guidelineDashPattern = { 2, 4 };
    lin.linear.color = core::ColorSpec::fromArgb(QColor::fromRgba(0xff00ff40));
    rulers->rulers.push_back(lin);
    core::LayerRuler::AnyRuler circ;
    circ.kind = core::RulerKind::Circular;
    circ.circular.center = QPointF(10, 10);
    circ.circular.radius = 5;
    circ.circular.displayDistance = false;  // and an empty dash pattern
    rulers->rulers.push_back(circ);
    map.layers().push_back(std::move(rulers));

    for (const QString& ext : { QStringLiteral("ldr"), QStringLiteral("mpd") }) {
        SCOPED_TRACE(ext.toStdString());
        const QString out = tmp_.filePath(QStringLiteral("small.") + ext);
        ASSERT_TRUE(import::writeLDrawMap(map, out, lib_));
        auto back = import::readLDrawMap(out, lib_);
        ASSERT_TRUE(back.ok());
        ASSERT_EQ(back.map->layers().size(), 2u);
        const auto& bl = static_cast<const core::LayerBrick&>(*back.map->layers()[0]);
        EXPECT_FALSE(bl.visible);
        ASSERT_EQ(bl.bricks.size(), 2u);
        ASSERT_EQ(bl.groups.size(), 1u);
        EXPECT_EQ(bl.groups[0].partNumber, QStringLiteral("MYSET"));
        EXPECT_EQ(bl.bricks[0].myGroupId, bl.groups[0].guid);
        EXPECT_EQ(bl.bricks[1].myGroupId, bl.groups[0].guid);
        const auto& rl = static_cast<const core::LayerRuler&>(*back.map->layers()[1]);
        ASSERT_EQ(rl.rulers.size(), 2u);
        EXPECT_EQ(rl.rulers[0].linear.point2, QPointF(30, 2));
        EXPECT_FLOAT_EQ(rl.rulers[0].linear.offsetDistance, -4.0f);
        EXPECT_EQ(rl.rulers[0].linear.guidelineDashPattern, (std::vector<float>{ 2, 4 }));
        EXPECT_EQ(rl.rulers[0].linear.color.color.rgba(), 0xff00ff40u);
        EXPECT_EQ(rl.rulers[1].circular.center, QPointF(10, 10));
        EXPECT_FLOAT_EQ(rl.rulers[1].circular.radius, 5.0f);
        EXPECT_FALSE(rl.rulers[1].circular.displayDistance);
    }
}

namespace {

// A 4DBrix file's lines, minus the created / modified times.
QStringList ncpLines(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QString& line : QString::fromUtf8(f.readAll()).split(QStringLiteral("\r\n"))) {
        if (line.contains(QStringLiteral("<created ")) || line.contains(QStringLiteral("<modified "))) continue;
        out << line;
    }
    return out;
}

}  // namespace

TEST_F(LDrawMapTest, FourDBrixWriteMatchesBlueBrick) {
    // Vanilla saved this map, links included; keep them (some partners are
    // BrickTracks parts only vanilla's installed library has).
    auto map = saveload::readBbm(oracle(QStringLiteral("fourdbrix.bbm"))).map;
    ASSERT_TRUE(map);
    const QString out = tmp_.filePath(QStringLiteral("fourdbrix.ncp"));
    QString err;
    ASSERT_TRUE(import::writeFourDBrixMap(*map, out, lib_, &err)) << err.toStdString();
    const QStringList ours = ncpLines(out), vanilla = ncpLines(oracle(QStringLiteral("fourdbrix.ncp")));
    ASSERT_EQ(ours.size(), vanilla.size());
    // Numbers may differ in the last digit (BlueBrick adds up in float).
    static const QRegularExpression number(QStringLiteral("-?\\d+(\\.\\d+)?(E[-+]\\d+)?"));
    for (int i = 0; i < ours.size(); ++i) {
        const QString a = QString(ours[i]).replace(number, QStringLiteral("#"));
        const QString b = QString(vanilla[i]).replace(number, QStringLiteral("#"));
        ASSERT_EQ(a.toStdString(), b.toStdString()) << "line " << i;
        auto x = number.globalMatch(ours[i]), y = number.globalMatch(vanilla[i]);
        while (x.hasNext() && y.hasNext()) {
            const double p = x.next().captured().toDouble(), q = y.next().captured().toDouble();
            ASSERT_LE(std::abs(p - q), 1e-3 * std::max(1.0, std::abs(q)))
                << "line " << i << ": " << ours[i].toStdString() << " vs " << vanilla[i].toStdString();
        }
    }
}

TEST_F(LDrawMapTest, FourDBrixReadMatchesBlueBrick) {
    const auto vanilla = saveload::readBbm(oracle(QStringLiteral("fourdbrix.from-ncp.bbm"))).map;
    ASSERT_TRUE(vanilla);
    auto ours = import::readFourDBrixMap(oracle(QStringLiteral("fourdbrix.ncp")), lib_);
    ASSERT_TRUE(ours.ok()) << ours.error.toStdString();
    EXPECT_TRUE(ours.warnings.isEmpty()) << ours.warnings.join(QLatin1Char('\n')).toStdString();
    const auto all = [](const QString&) { return true; };
    EXPECT_EQ(unmatched(bricksOf(*ours.map, all), bricksOf(*vanilla, all)), "");
    ASSERT_EQ(ours.map->layers().size(), vanilla->layers().size());
    for (size_t i = 0; i < ours.map->layers().size(); ++i) {
        if (ours.map->layers()[i]->kind() != core::LayerKind::Brick) continue;  // grid names are a global counter
        EXPECT_EQ(ours.map->layers()[i]->name, vanilla->layers()[i]->name);
        EXPECT_EQ(static_cast<const core::LayerBrick&>(*ours.map->layers()[i]).groups.size(),
                  static_cast<const core::LayerBrick&>(*vanilla->layers()[i]).groups.size()) << "layer " << i;
    }
}

TEST_F(LDrawMapTest, FourDBrixRoundTripKeepsGroups) {
    auto map = saveload::readBbm(oracle(QStringLiteral("fourdbrix.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib_);
    const QString out = tmp_.filePath(QStringLiteral("again.ncp"));
    ASSERT_TRUE(import::writeFourDBrixMap(*map, out, lib_));
    auto back = import::readFourDBrixMap(out, lib_);
    ASSERT_TRUE(back.ok());
    const auto mapped = [&](const QString& pn) { const auto m = lib_.metadata(pn); return m && m->fourDBrix; };
    EXPECT_EQ(unmatched(bricksOf(*back.map, mapped), bricksOf(*map, mapped), false), "");
    size_t grouped = 0;
    for (const auto& layer : back.map->layers())
        if (layer->kind() == core::LayerKind::Brick) grouped += static_cast<const core::LayerBrick&>(*layer).groups.size();
    EXPECT_GT(grouped, 0u);
}
