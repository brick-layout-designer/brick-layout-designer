// LDraw (.ldr / .mpd) maps, checked against vanilla BlueBrick 1.9.2's own
// output (fixtures/bluebrick-oracle, made with scripts/bluebrick-oracle).

#include "import/mapformats/LDrawMap.h"
#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
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

    struct Key { QString part; double x, y, w, h, angle; };

    // Every kept brick's part, displayArea and orientation.
    static std::vector<Key> bricksOf(const core::Map& map, const std::function<bool(const QString&)>& keep) {
        std::vector<Key> out;
        for (const auto& layer : map.layers()) {
            if (layer->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
                if (!keep(b.partNumber)) continue;
                const QRectF a = b.displayArea;
                out.push_back({ b.partNumber.toUpper(), a.x(), a.y(), a.width(), a.height(), b.orientation });
            }
        }
        return out;
    }

    // Pairs every brick with an equal one (within 0.01 studs / degrees);
    // describes the leftovers, empty when they all match.
    static std::string unmatched(std::vector<Key> ours, std::vector<Key> theirs) {
        const auto same = [](const Key& a, const Key& b) {
            const double da = std::remainder(a.angle - b.angle, 360.0);
            return a.part == b.part && std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01
                && std::abs(a.w - b.w) < 0.01 && std::abs(a.h - b.h) < 0.01 && std::abs(da) < 0.01;
        };
        for (auto it = ours.begin(); it != ours.end();) {
            const auto m = std::find_if(theirs.begin(), theirs.end(), [&](const Key& k) { return same(*it, k); });
            if (m != theirs.end()) { theirs.erase(m); it = ours.erase(it); } else { ++it; }
        }
        const auto text = [](const Key& k) {
            return QStringLiteral("%1 (%2, %3) %4x%5 @%6").arg(k.part).arg(k.x).arg(k.y).arg(k.w).arg(k.h)
                .arg(k.angle).toStdString();
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
    for (const QString ext : { QStringLiteral("ldr"), QStringLiteral("mpd") }) {
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
    const auto vanilla = saveload::readBbm(oracle(QStringLiteral("tight-corner.from-ldr.bbm"))).map;
    ASSERT_TRUE(vanilla);
    for (const QString ext : { QStringLiteral("ldr"), QStringLiteral("mpd") }) {
        SCOPED_TRACE(ext.toStdString());
        auto ours = import::readLDrawMap(oracle(QStringLiteral("tight-corner.") + ext), lib_);
        ASSERT_TRUE(ours.ok()) << ours.error.toStdString();
        // BlueBrick drops parts with a space in their name when reading.
        const auto noSpace = [](const QString& pn) { return !pn.contains(QLatin1Char(' ')); };
        EXPECT_EQ(unmatched(bricksOf(*ours.map, noSpace), bricksOf(*vanilla, noSpace)), "");
    }
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
    EXPECT_EQ(unmatched(bricksOf(*back.map, writable), bricksOf(*map, writable)), "");
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

    for (const QString ext : { QStringLiteral("ldr"), QStringLiteral("mpd") }) {
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
