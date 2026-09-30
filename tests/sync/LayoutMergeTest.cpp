// Three-way compare and merge for reconnecting after offline edits.

#include "sync/LayoutMerge.h"

#include "core/LayerBrick.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "sync/SyncDoc.h"
#include "sync/WebModel.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

using namespace bld;
using namespace bld::sync::merge;

namespace {

const QString kDir = QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/compare/");

// A shared document as the desktop holds it, read as a desktop map.
std::unique_ptr<core::Map> load(const QString& name) {
    QFile f(kDir + name + QStringLiteral(".bin"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    sync::SyncDoc doc;
    QString error;
    EXPECT_TRUE(doc.applyUpdate(f.readAll(), &error)) << error.toStdString();
    auto map = sync::mapFromDocJson(doc.toJson(), &error);
    EXPECT_TRUE(map) << error.toStdString();
    return map;
}

struct Case {
    std::unique_ptr<core::Map> base = load(QStringLiteral("base")), mine = load(QStringLiteral("mine")),
                               server = load(QStringLiteral("server"));
    Snapshot b = snapshotOf(*base), m = snapshotOf(*mine), s = snapshotOf(*server);
    QList<ItemChange> changes = compareLayouts(b, m, s);
};

const char* name(Status s) {
    switch (s) {
    case Status::Mine: return "mine";
    case Status::Server: return "server";
    case Status::Same: return "same";
    case Status::Conflict: return "conflict";
    }
    return "?";
}

const char* name(Side s) {
    switch (s) {
    case Side::Added: return "added";
    case Side::Edited: return "edited";
    case Side::Deleted: return "deleted";
    case Side::Unchanged: return "unchanged";
    }
    return "?";
}

const core::Brick* brick(const core::Map& map, const QString& id) {
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.guid == id) return &b;
    return nullptr;
}

int brickCount(const core::Map& map) {
    int n = 0;
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick) n += static_cast<int>(static_cast<const core::LayerBrick&>(*l).bricks.size());
    return n;
}

QStringList labelTexts(const core::Map& map) {
    QStringList out;
    for (const auto& l : map.sidecar.anchoredLabels) out << l.text;
    out.sort();
    return out;
}

}  // namespace

// fixtures/sync/compare/expected.json is what the web server's compare
// reports for the same three files (scripts/sync-fixtures/make-compare-fixture.mjs).
TEST(LayoutMerge, ReportsWhatTheServersCompareReports) {
    Case c;
    QFile f(kDir + QStringLiteral("expected.json"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QStringList expected, got;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("changes")).toArray()) {
        const QJsonObject o = v.toObject();
        expected << QStringLiteral("%1 %2 %3 %4 %5")
                        .arg(o.value(QLatin1String("key")).toString(), o.value(QLatin1String("kind")).toString(),
                             o.value(QLatin1String("status")).toString(), o.value(QLatin1String("mine")).toString(),
                             o.value(QLatin1String("server")).toString());
    }
    for (const auto& ch : c.changes)
        got << QStringLiteral("%1 %2 %3 %4 %5")
                   .arg(ch.key, ch.kind, QString::fromLatin1(name(ch.status)), QString::fromLatin1(name(ch.mine)),
                        QString::fromLatin1(name(ch.server)));
    EXPECT_EQ(got.join(QLatin1Char('\n')).toStdString(), expected.join(QLatin1Char('\n')).toStdString());
}

TEST(LayoutMerge, ByDefaultKeepsMyChangesAndTheServersOnClashes) {
    Case c;
    QString error;
    const auto merged = mergeLayouts(c.m, c.s, c.changes, {}, &error);
    ASSERT_TRUE(merged) << error.toStdString();

    // Only mine: in.
    EXPECT_EQ(brick(*merged, QStringLiteral("222"))->orientation, brick(*c.mine, QStringLiteral("222"))->orientation);
    EXPECT_EQ(brick(*merged, QStringLiteral("223")), nullptr);
    ASSERT_NE(brick(*merged, QStringLiteral("mine-new-brick")), nullptr);
    // Only the server's: kept.
    EXPECT_EQ(brick(*merged, QStringLiteral("3226")), nullptr);
    EXPECT_TRUE(merged->sidecar.venue.has_value());
    // A clash: the server's.
    EXPECT_EQ(brick(*merged, QStringLiteral("224"))->displayArea, brick(*c.server, QStringLiteral("224"))->displayArea);
    EXPECT_EQ(merged->author, c.server->author);
    EXPECT_EQ(merged->event, c.server->event);
    EXPECT_EQ(labelTexts(*merged), (QStringList{ QStringLiteral("Added offline"), QStringLiteral("Server says this") }));
    // Mine's text edit.
    bool edited = false;
    for (const auto& l : merged->layers())
        if (l->kind() == core::LayerKind::Text)
            for (const auto& t : static_cast<const core::LayerText&>(*l).textCells) edited |= t.text == QStringLiteral("Edited offline");
    EXPECT_TRUE(edited);
    EXPECT_EQ(brickCount(*merged), brickCount(*c.base) - 2 + 1);

    // Merged, it's the server's layout plus my changes: comparing it with
    // the server as "mine" shows only those.
    const auto after = compareLayouts(c.s, snapshotOf(*merged), c.s);
    QStringList keys;
    for (const auto& ch : after) keys << ch.key;
    EXPECT_EQ(keys.join(QLatin1Char(' ')).toStdString(), (QStringList{ QStringLiteral("brick:211:222"), QStringLiteral("brick:211:223"),
                                  QStringLiteral("brick:211:mine-new-brick"), QStringLiteral("label:L-mine"),
                                  QStringLiteral("text:3144:T0") })
                                    .join(QLatin1Char(' '))
                                    .toStdString());
}

TEST(LayoutMerge, TakesTheChoicesMadeForClashes) {
    Case c;
    const QHash<QString, Choice> choices{
        { QStringLiteral("brick:211:224"), Choice::Both },
        { QStringLiteral("label:L-both"), Choice::Mine },
        { QStringLiteral("map"), Choice::Mine },
        { QStringLiteral("venue"), Choice::Mine },  // mine has none: the server's goes
        { QStringLiteral("brick:211:222"), Choice::Server },  // my own change dropped
    };
    const auto merged = mergeLayouts(c.m, c.s, c.changes, choices);
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->author, c.mine->author);
    EXPECT_EQ(merged->event, c.mine->event);
    EXPECT_FALSE(merged->sidecar.venue.has_value());
    EXPECT_EQ(labelTexts(*merged), (QStringList{ QStringLiteral("Added offline"), QStringLiteral("Mine says this") }));
    EXPECT_EQ(brick(*merged, QStringLiteral("222"))->orientation, brick(*c.server, QStringLiteral("222"))->orientation);
    // Both: the server's stays, mine comes in again as a copy.
    EXPECT_EQ(brick(*merged, QStringLiteral("224"))->displayArea, brick(*c.server, QStringLiteral("224"))->displayArea);
    const core::Brick* copy = nullptr;
    for (const auto& l : merged->layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (!brick(*c.mine, b.guid) && !brick(*c.server, b.guid)) copy = &b;
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->displayArea, brick(*c.mine, QStringLiteral("224"))->displayArea);
    for (const auto& link : copy->connections) EXPECT_TRUE(link.linkedToId.isEmpty());
}

TEST(LayoutMerge, MyBrickBringsBackTheLayerTheServerDeleted) {
    Case c;
    // The server deletes brick layer 211; mine only added a brick to it.
    Snapshot server = c.b;
    QJsonObject data = server.doc.value(QLatin1String("layerData")).toObject();
    data.remove(QStringLiteral("211"));
    server.doc.insert(QStringLiteral("layerData"), data);
    QJsonArray order = server.doc.value(QLatin1String("layers")).toArray();
    const int at = static_cast<int>(order.toVariantList().indexOf(QStringLiteral("211")));
    ASSERT_GE(at, 0);
    order.removeAt(at);
    server.doc.insert(QStringLiteral("layers"), order);
    Snapshot mine = c.b;
    QJsonObject mineData = mine.doc.value(QLatin1String("layerData")).toObject();
    QJsonObject layer = mineData.value(QStringLiteral("211")).toObject();
    QJsonArray bricks = layer.value(QLatin1String("bricks")).toArray();
    QJsonObject added = bricks[0].toObject();
    added.insert(QStringLiteral("id"), QStringLiteral("offline-brick"));
    bricks.append(added);
    layer.insert(QStringLiteral("bricks"), bricks);
    mineData.insert(QStringLiteral("211"), layer);
    mine.doc.insert(QStringLiteral("layerData"), mineData);

    const auto changes = compareLayouts(c.b, mine, server);
    const auto merged = mergeLayouts(mine, server, changes, {});
    ASSERT_TRUE(merged);
    const auto layers = snapshotOf(*merged).doc.value(QLatin1String("layers")).toArray();
    EXPECT_EQ(layers.toVariantList().indexOf(QStringLiteral("211")), at);  // back where it was
    ASSERT_NE(brick(*merged, QStringLiteral("offline-brick")), nullptr);
    EXPECT_EQ(brick(*merged, bricks[0].toObject().value(QLatin1String("id")).toString()), nullptr);

    // With the layers before it gone from the server too, it comes back first.
    Snapshot fewer = server;
    QJsonArray rest = fewer.doc.value(QLatin1String("layers")).toArray();
    for (int i = 0; i < at; ++i) rest.removeAt(0);
    fewer.doc.insert(QStringLiteral("layers"), rest);
    const auto first = mergeLayouts(mine, fewer, compareLayouts(c.b, mine, fewer), {});
    ASSERT_TRUE(first);
    EXPECT_EQ(snapshotOf(*first).doc.value(QLatin1String("layers")).toArray().first().toString(), QStringLiteral("211"));

    // Taking the server's side of the layer too drops it with my brick.
    QHash<QString, Choice> server211;
    for (const auto& ch : changes)
        if (ch.layerId == QLatin1String("211")) server211.insert(ch.key, Choice::Server);
    const auto dropped = mergeLayouts(mine, server, changes, server211);
    ASSERT_TRUE(dropped);
    EXPECT_EQ(brick(*dropped, QStringLiteral("offline-brick")), nullptr);
}

TEST(LayoutMerge, SaysWhatEachChangeIs) {
    Case c;
    QStringList lines;
    for (const auto& ch : c.changes) lines << describe(ch);
    // Bricks of one part tell apart by where they are and their layer.
    EXPECT_TRUE(lines.contains(
        QStringLiteral("Brick TABLE96X190 at (296, 96) on \"Tables\": you changed it, the server changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Brick TABLE96X190 at (288, 192) on \"Tables\": you changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Brick TABLE96X190 at (96, 192) on \"Tables\": you deleted it")));
    EXPECT_TRUE(
        lines.contains(QStringLiteral("Brick TABLE96X190 at (96, 112) on \"Tables\": you and the server both changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Brick 3811.1 at (176, 208) on \"Buildings\": the server deleted it")));
    EXPECT_TRUE(lines.contains(
        QStringLiteral("Text \"Edited offline\" at (332.6, 212.1) on \"Building labels\": you changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Label \"Added offline\": you added it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Venue: the server added it")));
    for (const auto& ch : c.changes) {
        const bool copyable = ch.kind == QLatin1String("brick") || ch.kind == QLatin1String("text")
                           || ch.kind == QLatin1String("label") || ch.kind == QLatin1String("module");
        EXPECT_EQ(ch.allowsBoth(), copyable && !ch.mineValue.isUndefined() && !ch.serverValue.isUndefined())
            << ch.key.toStdString();
    }
}

// Where the item is comes from mine, else the server's, else base; a group
// says its part; no layer name, no "on".
TEST(LayoutMerge, SaysWhereFromWhicheverSideHasIt) {
    const auto brickAt = [](double x, double y) {
        return QJsonObject{ { QStringLiteral("partNumber"), QStringLiteral("3001") },
                            { QStringLiteral("displayArea"),
                              QJsonObject{ { QStringLiteral("x"), x }, { QStringLiteral("y"), y } } } };
    };
    ItemChange c;
    c.kind = QStringLiteral("brick");
    c.layerName = QStringLiteral("Track");
    c.base = brickAt(1, 2);
    c.serverValue = brickAt(-3.04, 4.25);
    c.mine = Side::Deleted;
    EXPECT_EQ(describe(c).toStdString(), std::string("Brick 3001 at (-3, 4.3) on \"Track\": you deleted it"));
    c.mineValue = brickAt(7, 8);
    c.mine = Side::Edited;
    EXPECT_EQ(describe(c).toStdString(), std::string("Brick 3001 at (7, 8) on \"Track\": you changed it"));
    c.mineValue = QJsonValue(QJsonValue::Undefined);
    c.serverValue = QJsonValue(QJsonValue::Undefined);
    c.mine = Side::Deleted;
    c.layerName.clear();
    EXPECT_EQ(describe(c).toStdString(), std::string("Brick 3001 at (1, 2): you deleted it"));

    ItemChange g;
    g.kind = QStringLiteral("group");
    g.layerName = QStringLiteral("Track");
    g.mineValue = QJsonObject{ { QStringLiteral("partNumber"), QStringLiteral("SET-1") } };
    g.mine = Side::Added;
    EXPECT_EQ(describe(g).toStdString(), std::string("Group SET-1 on \"Track\": you added it"));
    g.mineValue = QJsonObject{};
    EXPECT_EQ(describe(g).toStdString(), std::string("Group on \"Track\": you added it"));
}
