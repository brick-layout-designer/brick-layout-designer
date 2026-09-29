// Sync phase P3b: desktop edits become the smallest change to the shared
// document, matched by id, so they merge with other people's concurrent
// edits. Every edit is checked by applying its update to another copy of
// the server's document and reading the layout back.

#include "SyncDoc.h"
#include "WebModel.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "saveload/BbmWriter.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QFile>

#include <QDir>
#include <QSaveFile>

#include <functional>

using namespace bld;

namespace {

QByteArray serverDoc() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/layers.ydoc"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::unique_ptr<core::Map> mapOf(const sync::SyncDoc& doc) {
    QString err;
    auto map = sync::mapFromDocJson(doc.toJson(), &err);
    EXPECT_TRUE(map) << err.toStdString();
    return map;
}

QByteArray bbm(const core::Map& map) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    EXPECT_TRUE(saveload::writeBbm(map, buf).ok);
    return buf.data();
}

template <typename T>
T& layerOf(core::Map& map, core::LayerKind kind, int nth = 0) {
    for (auto& l : map.layers())
        if (l->kind() == kind && nth-- == 0) return static_cast<T&>(*l);
    throw std::runtime_error("no such layer");
}

struct Edit {
    const char* name;
    std::function<void(core::Map&)> apply;
};

// Readable names in failure messages.
void PrintTo(const Edit& e, std::ostream* os) { *os << e.name; }

class SyncDocEdit : public ::testing::TestWithParam<Edit> {};

}  // namespace

TEST(SyncDoc, WritingTheSameLayoutChangesNothing) {
    sync::SyncDoc doc;
    ASSERT_TRUE(doc.applyUpdate(serverDoc()));
    const auto map = mapOf(doc);
    EXPECT_TRUE(doc.writeMap(*map).isEmpty());
}

TEST_P(SyncDocEdit, ReachesTheServerCopyIntact) {
    sync::SyncDoc desktop, server;
    ASSERT_TRUE(desktop.applyUpdate(serverDoc()));
    ASSERT_TRUE(server.applyUpdate(serverDoc()));
    auto edited = mapOf(desktop);
    GetParam().apply(*edited);

    const QByteArray update = desktop.writeMap(*edited);
    ASSERT_FALSE(update.isEmpty());
    ASSERT_TRUE(server.applyUpdate(update));
    EXPECT_EQ(bbm(*mapOf(server)), bbm(*edited));
    // Written once: a second write has nothing left to send.
    EXPECT_TRUE(desktop.writeMap(*edited).isEmpty());
}

INSTANTIATE_TEST_SUITE_P(Edits, SyncDocEdit, ::testing::Values(
    Edit{ "MoveBrick", [](core::Map& m) { layerOf<core::LayerBrick>(m, core::LayerKind::Brick).bricks[3].displayArea.translate(8, -4); } },
    Edit{ "TurnBrick", [](core::Map& m) { layerOf<core::LayerBrick>(m, core::LayerKind::Brick).bricks[0].orientation = 22.5f; } },
    Edit{ "AddBrick", [](core::Map& m) {
        auto& bricks = layerOf<core::LayerBrick>(m, core::LayerKind::Brick).bricks;
        core::Brick b = bricks[1];
        b.guid = QStringLiteral("900001");
        for (auto& c : b.connections) { c.guid += QStringLiteral("x"); c.linkedToId.clear(); }
        b.displayArea.moveTo(1000, 1000);
        bricks.insert(bricks.begin() + 2, b);
    } },
    // Deleting doesn't move the document's state vector: still an update.
    Edit{ "DeleteBrick", [](core::Map& m) { auto& b = layerOf<core::LayerBrick>(m, core::LayerKind::Brick).bricks; b.erase(b.begin() + 5); } },
    Edit{ "ReorderBricks", [](core::Map& m) { auto& b = layerOf<core::LayerBrick>(m, core::LayerKind::Brick).bricks; std::swap(b[0], b[1]); } },
    Edit{ "EditText", [](core::Map& m) { layerOf<core::LayerText>(m, core::LayerKind::Text).textCells[0].text = QStringLiteral("Hall B"); } },
    Edit{ "RenameLayer", [](core::Map& m) { m.layers()[1]->name = QStringLiteral("Renamed"); } },
    Edit{ "HideLayer", [](core::Map& m) { m.layers()[1]->visible = !m.layers()[1]->visible; } },
    Edit{ "RemoveLayer", [](core::Map& m) { m.layers().erase(m.layers().begin() + 1); } },
    Edit{ "AddLayer", [](core::Map& m) {
        auto l = std::make_unique<core::LayerArea>();
        l->guid = QStringLiteral("900002");
        l->name = QStringLiteral("New zones");
        l->cells.push_back({ 2, 3, QColor::fromRgba(0xff336699) });
        m.layers().push_back(std::move(l));
    } },
    Edit{ "ReorderLayers", [](core::Map& m) { std::swap(m.layers()[1], m.layers()[2]); } },
    Edit{ "Header", [](core::Map& m) { m.author = QStringLiteral("Desk"); m.date = QDate(2027, 3, 4); } },
    Edit{ "AreaCell", [](core::Map& m) { layerOf<core::LayerArea>(m, core::LayerKind::Area).cells[1].color = QColor::fromRgba(0x40123456); } },
    Edit{ "Ruler", [](core::Map& m) { layerOf<core::LayerRuler>(m, core::LayerKind::Ruler).rulers[0].linear.offsetDistance += 3; } },
    Edit{ "Group", [](core::Map& m) {
        auto& l = layerOf<core::LayerBrick>(m, core::LayerKind::Brick);
        core::Group g;
        g.guid = QStringLiteral("900003");
        l.groups.push_back(g);
        l.bricks[0].myGroupId = g.guid;
        l.bricks[1].myGroupId = g.guid;
    } }),
    [](const auto& info) { return std::string(info.param.name); });

TEST(SyncDoc, ConcurrentEditsToDifferentBricksAndFieldsBothSurvive) {
    sync::SyncDoc a, b;
    ASSERT_TRUE(a.applyUpdate(serverDoc()));
    ASSERT_TRUE(b.applyUpdate(serverDoc()));
    auto mapA = mapOf(a);
    auto mapB = mapOf(b);
    auto& bricksA = layerOf<core::LayerBrick>(*mapA, core::LayerKind::Brick).bricks;
    auto& bricksB = layerOf<core::LayerBrick>(*mapB, core::LayerKind::Brick).bricks;
    // A moves brick 0 and turns brick 2; B moves brick 1 and moves brick 2.
    bricksA[0].displayArea.translate(16, 0);
    bricksA[2].orientation = 90.0f;
    bricksB[1].displayArea.translate(0, 16);
    bricksB[2].displayArea.translate(-8, 0);
    const QByteArray fromA = a.writeMap(*mapA);
    const QByteArray fromB = b.writeMap(*mapB);
    ASSERT_TRUE(a.applyUpdate(fromB));
    ASSERT_TRUE(b.applyUpdate(fromA));

    const auto mergedA = mapOf(a);
    const auto mergedB = mapOf(b);
    EXPECT_EQ(bbm(*mergedA), bbm(*mergedB));
    const auto& merged = layerOf<core::LayerBrick>(*mergedA, core::LayerKind::Brick).bricks;
    EXPECT_EQ(merged[0].displayArea, bricksA[0].displayArea);
    EXPECT_EQ(merged[1].displayArea, bricksB[1].displayArea);
    EXPECT_EQ(merged[2].orientation, 90.0f);
    EXPECT_EQ(merged[2].displayArea, bricksB[2].displayArea);
}

TEST(SyncDoc, ExportsAnUpdateForTheWebCheck) {
    // scripts/sync-fixtures/check-desktop-update.mjs applies this update with
    // the web's own Yjs and compares its .bbm with the desktop's. Run with
    // BLD_SYNC_EXPORT_DIR set; there's no web checkout in desktop CI.
    const QString dir = qEnvironmentVariable("BLD_SYNC_EXPORT_DIR");
    if (dir.isEmpty()) GTEST_SKIP() << "BLD_SYNC_EXPORT_DIR not set";
    sync::SyncDoc desktop;
    ASSERT_TRUE(desktop.applyUpdate(serverDoc()));
    auto map = mapOf(desktop);
    auto& bricks = layerOf<core::LayerBrick>(*map, core::LayerKind::Brick).bricks;
    bricks[3].displayArea.translate(8, -4);
    core::Brick added = bricks[1];
    added.guid = QStringLiteral("900001");
    for (auto& c : added.connections) { c.guid += QStringLiteral("x"); c.linkedToId.clear(); }
    added.displayArea.moveTo(1000, 1000);
    bricks.insert(bricks.begin() + 2, added);
    bricks.erase(bricks.begin() + 6);
    layerOf<core::LayerText>(*map, core::LayerKind::Text).textCells[0].text = QStringLiteral("Hall B");
    const QByteArray update = desktop.writeMap(*map);
    ASSERT_FALSE(update.isEmpty());
    const auto save = [&](const QString& name, const QByteArray& data) {
        QSaveFile f(QDir(dir).filePath(name));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
        ASSERT_TRUE(f.commit());
    };
    save(QStringLiteral("desktop-update.bin"), update);
    save(QStringLiteral("desktop-expected.bbm"), bbm(*map));
}

namespace {

core::Brick& brickAt(core::Map& m, size_t i) { return layerOf<core::LayerBrick>(m, core::LayerKind::Brick).bricks[i]; }

}  // namespace

TEST(SyncDocUndo, UndoesOnlyThisDesktopsChanges) {
    sync::SyncDoc desktop, other;
    ASSERT_TRUE(desktop.applyUpdate(serverDoc()));
    ASSERT_TRUE(other.applyUpdate(serverDoc()));
    const QRectF start0 = brickAt(*mapOf(desktop), 0).displayArea;

    // The desktop moves brick 0; then someone else moves brick 1.
    auto mine = mapOf(desktop);
    brickAt(*mine, 0).displayArea.translate(16, 0);
    ASSERT_TRUE(other.applyUpdate(desktop.writeMap(*mine)));
    auto theirs = mapOf(other);
    brickAt(*theirs, 1).displayArea.translate(0, 32);
    const QRectF moved1 = brickAt(*theirs, 1).displayArea;
    ASSERT_TRUE(desktop.applyUpdate(other.writeMap(*theirs)));

    // Undo reverts the desktop's move and keeps the other one.
    ASSERT_TRUE(desktop.canUndo());
    const QByteArray undo = desktop.undo();
    ASSERT_FALSE(undo.isEmpty());
    auto after = mapOf(desktop);
    EXPECT_EQ(brickAt(*after, 0).displayArea, start0);
    EXPECT_EQ(brickAt(*after, 1).displayArea, moved1);
    // Sent on, it brings the other copy along.
    ASSERT_TRUE(other.applyUpdate(undo));
    EXPECT_EQ(bbm(*mapOf(other)), bbm(*after));
    EXPECT_FALSE(desktop.canUndo());

    // Redo puts the move back.
    ASSERT_TRUE(desktop.canRedo());
    ASSERT_FALSE(desktop.redo().isEmpty());
    EXPECT_EQ(brickAt(*mapOf(desktop), 0).displayArea, start0.translated(16, 0));
}

TEST(SyncDocUndo, OtherPeoplesChangesAloneLeaveNothingToUndo) {
    sync::SyncDoc desktop, other;
    ASSERT_TRUE(desktop.applyUpdate(serverDoc()));
    ASSERT_TRUE(other.applyUpdate(serverDoc()));
    auto theirs = mapOf(other);
    brickAt(*theirs, 0).orientation = 90.0f;
    ASSERT_TRUE(desktop.applyUpdate(other.writeMap(*theirs)));
    EXPECT_FALSE(desktop.canUndo());
    EXPECT_TRUE(desktop.undo().isEmpty());
    EXPECT_EQ(brickAt(*mapOf(desktop), 0).orientation, 90.0f);
}

TEST(SyncDocUndo, EachWriteIsOneStepUndoneInReverse) {
    sync::SyncDoc desktop;
    ASSERT_TRUE(desktop.applyUpdate(serverDoc()));
    auto m = mapOf(desktop);
    const QString name = m->layers()[1]->name;
    const QString author = m->author;
    m->layers()[1]->name = QStringLiteral("First");
    desktop.writeMap(*m);
    m->author = QStringLiteral("Second");
    desktop.writeMap(*m);

    ASSERT_FALSE(desktop.undo().isEmpty());
    auto a = mapOf(desktop);
    EXPECT_EQ(a->author, author);
    EXPECT_EQ(a->layers()[1]->name, QStringLiteral("First"));
    ASSERT_FALSE(desktop.undo().isEmpty());
    EXPECT_EQ(mapOf(desktop)->layers()[1]->name, name);
    EXPECT_FALSE(desktop.canUndo());
}
