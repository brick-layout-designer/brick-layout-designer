// rebuildConnectivity against the links vanilla BlueBrick saved in real
// maps: same connected brick pairs, and <LinkedTo> naming the partner's
// connection point (not the partner brick), so vanilla can read our files.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QHash>
#include <QSet>

using namespace bld;

namespace {

// Connected brick pairs ("a|b", a < b) and whether every link resolves
// to an existing connection point.
QSet<QString> brickPairs(const core::Map& map, bool* allLinksAreConnections) {
    QHash<QString, QString> owner;  // connection guid -> brick guid
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
            for (const auto& c : b.connections) owner.insert(c.guid, b.guid);
    }
    QSet<QString> pairs;
    *allLinksAreConnections = true;
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
            for (const auto& c : b.connections) {
                if (c.linkedToId.isEmpty()) continue;
                if (!owner.contains(c.linkedToId)) { *allLinksAreConnections = false; continue; }
                const QString other = owner.value(c.linkedToId);
                pairs.insert(b.guid < other ? b.guid + QLatin1Char('|') + other : other + QLatin1Char('|') + b.guid);
            }
        }
    }
    return pairs;
}

}  // namespace

TEST(Connectivity, MatchesLinksSavedByBlueBrick) {
    const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
    if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(root);
    lib.scan();
    for (const char* name : { "tight-corner.bbm", "fordyce-2026.bbm" }) {
        SCOPED_TRACE(name);
        auto map = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/") + QLatin1String(name)).map;
        ASSERT_TRUE(map);
        bool vanillaOk = false, oursOk = false;
        const QSet<QString> vanilla = brickPairs(*map, &vanillaOk);
        ASSERT_TRUE(vanillaOk);
        ASSERT_FALSE(vanilla.isEmpty());
        edit::rebuildConnectivity(*map, lib);
        const QSet<QString> ours = brickPairs(*map, &oursOk);
        EXPECT_TRUE(oursOk) << "a link names something that isn't a connection point";
        EXPECT_EQ(ours.size(), vanilla.size());
        EXPECT_TRUE(ours == vanilla);
    }
}
