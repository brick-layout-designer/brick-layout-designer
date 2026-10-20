// Doc shape 2 (sync/DocShape.h, the web's packages/ydoc shape.ts): rulers,
// area sheets and the order of parts edited in place, so two people's edits
// at the same moment merge; docs in shape 1 read and move over lazily. The
// shared vectors are fixtures/contracts/doc-shape.json: the same order and
// z as the web, and concurrent edits against a doc and an edit the web made.

#include "Compat.h"
#include "DocShape.h"
#include "SyncDoc.h"
#include "WebModel.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace bld;

namespace {

QJsonObject contract() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/doc-shape.json"));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

std::optional<double> optZ(const QJsonValue& v) { return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt; }

std::unique_ptr<core::Map> mapOf(const sync::SyncDoc& doc) {
    QString err;
    auto map = sync::mapFromDocJson(doc.toJson(), &err);
    EXPECT_TRUE(map) << err.toStdString();
    return map;
}

template <typename T>
T* layerOf(core::Map& map, core::LayerKind kind) {
    for (auto& l : map.layers())
        if (l->kind() == kind) return static_cast<T*>(l.get());
    return nullptr;
}

int schemaOf(const sync::SyncDoc& d) {
    return d.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion")).toInt();
}

// The desktop's half of each concurrent case: what `desktop` says, on the map.
void desktopEdit(const QString& what, core::Map& map) {
    auto* rulers = layerOf<core::LayerRuler>(map, core::LayerKind::Ruler);
    auto* area = layerOf<core::LayerArea>(map, core::LayerKind::Area);
    core::LayerBrick* bricks = nullptr;
    for (auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick && l->guid == QLatin1String("brick-1")) bricks = static_cast<core::LayerBrick*>(l.get());
    ASSERT_TRUE(rulers && area && bricks);
    const auto at0 = std::find_if(bricks->bricks.begin(), bricks->bricks.end(), [](const core::Brick& b) { return b.displayArea.x() == 0; });
    if (what == QLatin1String("delete the ruler")) {
        rulers->rulers.clear();
    } else if (what == QLatin1String("move the ruler by (0, 5)")) {
        auto& r = rulers->rulers.at(0).linear;
        r.point1 += QPointF(0, 5);
        r.point2 += QPointF(0, 5);
        r.displayArea.translate(0, 5);
    } else if (what == QLatin1String("move the area sheet by (0, 1) cells")) {
        for (auto& c : area->cells) c.y += 1;
    } else if (what == QLatin1String("delete the part at x 0")) {
        ASSERT_NE(at0, bricks->bricks.end());
        bricks->bricks.erase(at0);
    } else if (what == QLatin1String("move the part at x 0 to x 40")) {
        ASSERT_NE(at0, bricks->bricks.end());
        at0->displayArea.moveLeft(40);
    } else {
        FAIL() << "unknown desktop edit: " << what.toStdString();
    }
}

void check(const sync::SyncDoc& d, const QJsonObject& want, const QString& name) {
    auto map = mapOf(d);
    ASSERT_TRUE(map);
    SCOPED_TRACE(name.toStdString());
    if (want.contains(QLatin1String("rulers")))
        EXPECT_EQ(static_cast<int>(layerOf<core::LayerRuler>(*map, core::LayerKind::Ruler)->rulers.size()),
                  want.value(QLatin1String("rulers")).toInt());
    if (want.contains(QLatin1String("cells"))) {
        const auto& cells = layerOf<core::LayerArea>(*map, core::LayerKind::Area)->cells;
        ASSERT_EQ(static_cast<int>(cells.size()), want.value(QLatin1String("cells")).toInt());
        QJsonArray at;
        QList<std::pair<int, int>> xy;
        for (const auto& c : cells) xy.append({ c.x, c.y });
        std::sort(xy.begin(), xy.end());
        for (const auto& [x, y] : xy) at.append(QJsonArray{ x, y });
        EXPECT_TRUE(want.value(QLatin1String("cellsAtOneOf")).toArray().contains(at))
            << QJsonDocument(at).toJson(QJsonDocument::Compact).toStdString();
    }
    if (want.contains(QLatin1String("bricksX"))) {
        QJsonArray xs;
        for (auto& l : map->layers())
            if (l->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<core::LayerBrick&>(*l).bricks) xs.append(b.displayArea.x());
        EXPECT_EQ(xs, want.value(QLatin1String("bricksX")).toArray());
    }
}

}  // namespace

TEST(DocShapeContract, OrdersByZAsTheWebDoes) {
    const QJsonArray cases = contract().value(QLatin1String("orderByZ")).toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const auto& c : cases) {
        QList<std::optional<double>> zs;
        for (const auto& v : c.toObject().value(QLatin1String("z")).toArray()) zs.append(optZ(v));
        QJsonArray got;
        for (const qsizetype i : sync::orderByZ(zs)) got.append(static_cast<int>(i));
        EXPECT_EQ(got, c.toObject().value(QLatin1String("order")).toArray());
    }
}

TEST(DocShapeContract, ReordersWithTheSameZAsTheWeb) {
    const QJsonArray cases = contract().value(QLatin1String("assignZ")).toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const auto& c : cases) {
        QList<sync::ZEntry> cur;
        for (const auto& v : c.toObject().value(QLatin1String("cur")).toArray())
            cur.append({ v.toObject().value(QLatin1String("id")).toString(), optZ(v.toObject().value(QLatin1String("z"))) });
        QStringList want;
        for (const auto& v : c.toObject().value(QLatin1String("want")).toArray()) want << v.toString();
        QJsonObject got;
        for (const auto& [id, z] : sync::assignZ(cur, want)) got.insert(id, z);
        EXPECT_EQ(got, c.toObject().value(QLatin1String("z")).toObject());
    }
}

// The web edited a doc while this desktop edited it too: both orders of
// arrival end where the contract says.
TEST(DocShapeContract, ConcurrentEditsWithTheWebMerge) {
    const QJsonArray cases = contract().value(QLatin1String("concurrent")).toArray();
    ASSERT_EQ(cases.size(), 5);
    for (const auto& v : cases) {
        const QJsonObject c = v.toObject();
        const QString name = c.value(QLatin1String("name")).toString();
        const QByteArray base = QByteArray::fromBase64(c.value(QLatin1String("base")).toString().toLatin1());
        const QByteArray web = QByteArray::fromBase64(c.value(QLatin1String("web")).toString().toLatin1());
        sync::SyncDoc mine;
        ASSERT_TRUE(mine.applyUpdate(base)) << name.toStdString();
        auto map = mapOf(mine);
        desktopEdit(c.value(QLatin1String("desktop")).toString(), *map);
        const QByteArray ours = mine.writeMap(*map);
        ASSERT_FALSE(ours.isEmpty()) << name.toStdString();
        for (const bool webFirst : { true, false }) {
            sync::SyncDoc d;
            ASSERT_TRUE(d.applyUpdate(base));
            ASSERT_TRUE(d.applyUpdate(webFirst ? web : ours));
            ASSERT_TRUE(d.applyUpdate(webFirst ? ours : web));
            check(d, c.value(QLatin1String("expect")).toObject(), name);
        }
        // And the desktop that made the edit, once the web's arrives.
        ASSERT_TRUE(mine.applyUpdate(web));
        check(mine, c.value(QLatin1String("expect")).toObject(), name);
    }
}

// --------------------------------------------------------------- two desktops

namespace {

// The contract's base doc (shape 2: z-ordered parts, Y.Map rulers): two
// desktops editing it at the same moment.
QByteArray contractBase() {
    return QByteArray::fromBase64(contract().value(QLatin1String("concurrent")).toArray().at(0).toObject()
                                      .value(QLatin1String("base")).toString().toLatin1());
}

}  // namespace

TEST(DocShape, TwoDesktopsDeletingAndMovingTheSameRulerLeaveItDeleted) {
    sync::SyncDoc a, b;
    ASSERT_TRUE(a.applyUpdate(contractBase()));
    ASSERT_TRUE(b.applyUpdate(contractBase()));
    auto mapA = mapOf(a);
    auto mapB = mapOf(b);
    desktopEdit(QStringLiteral("delete the ruler"), *mapA);
    desktopEdit(QStringLiteral("move the ruler by (0, 5)"), *mapB);
    const QByteArray fromA = a.writeMap(*mapA), fromB = b.writeMap(*mapB);
    ASSERT_TRUE(a.applyUpdate(fromB));
    ASSERT_TRUE(b.applyUpdate(fromA));
    EXPECT_TRUE(layerOf<core::LayerRuler>(*mapOf(a), core::LayerKind::Ruler)->rulers.empty());
    EXPECT_TRUE(layerOf<core::LayerRuler>(*mapOf(b), core::LayerKind::Ruler)->rulers.empty());
}

TEST(DocShape, TwoDesktopsMovingTheSameAreaSheetLeaveEachCellOnce) {
    sync::SyncDoc a, b;
    ASSERT_TRUE(a.applyUpdate(contractBase()));
    ASSERT_TRUE(b.applyUpdate(contractBase()));
    auto mapA = mapOf(a);
    auto mapB = mapOf(b);
    for (auto& c : layerOf<core::LayerArea>(*mapA, core::LayerKind::Area)->cells) c.x += 1;
    for (auto& c : layerOf<core::LayerArea>(*mapB, core::LayerKind::Area)->cells) c.y += 1;
    const QByteArray fromA = a.writeMap(*mapA), fromB = b.writeMap(*mapB);
    ASSERT_TRUE(a.applyUpdate(fromB));
    ASSERT_TRUE(b.applyUpdate(fromA));
    const auto& cellsA = layerOf<core::LayerArea>(*mapOf(a), core::LayerKind::Area)->cells;
    EXPECT_EQ(cellsA.size(), 2u);
    // Both see the same.
    auto ma = mapOf(a), mb = mapOf(b);
    const auto& ca = layerOf<core::LayerArea>(*ma, core::LayerKind::Area)->cells;
    const auto& cb = layerOf<core::LayerArea>(*mb, core::LayerKind::Area)->cells;
    ASSERT_EQ(ca.size(), cb.size());
    for (size_t i = 0; i < ca.size(); ++i) EXPECT_TRUE(ca[i].x == cb[i].x && ca[i].y == cb[i].y);
    // An undo takes back this desktop's move only.
    a.undo();
    EXPECT_EQ(layerOf<core::LayerArea>(*mapOf(a), core::LayerKind::Area)->cells.size(), 2u);
}

TEST(DocShape, BringToFrontWhileAnotherDesktopDeletesThePartLeavesItDeleted) {
    sync::SyncDoc a, b;
    ASSERT_TRUE(a.applyUpdate(contractBase()));
    ASSERT_TRUE(b.applyUpdate(contractBase()));
    auto mapA = mapOf(a);
    auto mapB = mapOf(b);
    auto& ba = [&]() -> core::LayerBrick& {
        for (auto& l : mapA->layers()) if (l->guid == QLatin1String("brick-1")) return static_cast<core::LayerBrick&>(*l);
        throw std::runtime_error("no sheet");
    }();
    // A brings the part at x 0 (at the back) to the front; B deletes it.
    ASSERT_EQ(ba.bricks.size(), 2u);
    ASSERT_EQ(ba.bricks[0].displayArea.x(), 0);
    std::rotate(ba.bricks.begin(), ba.bricks.begin() + 1, ba.bricks.end());
    desktopEdit(QStringLiteral("delete the part at x 0"), *mapB);
    const QByteArray fromA = a.writeMap(*mapA), fromB = b.writeMap(*mapB);
    ASSERT_TRUE(a.applyUpdate(fromB));
    ASSERT_TRUE(b.applyUpdate(fromA));
    for (const auto* d : { &a, &b }) {
        auto m = mapOf(*d);
        for (auto& l : m->layers())
            if (l->guid == QLatin1String("brick-1")) {
                const auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
                ASSERT_EQ(bricks.size(), 1u);
                EXPECT_EQ(bricks[0].displayArea.x(), 10);
            }
    }
}

TEST(DocShape, ReadsAndMovesAShapeOneDocOnTheFirstEditThatNeedsIt) {
    // A shape-1 doc: the layers fixture the other sync tests use.
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/layers.ydoc"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray one = f.readAll();
    sync::SyncDoc d;
    ASSERT_TRUE(d.applyUpdate(one));
    EXPECT_LE(schemaOf(d), sync::kDocBaseSchema);
    EXPECT_TRUE(sync::canReadDoc(sync::kDocShapeInPlace));
    auto map = mapOf(d);
    auto* sheet = layerOf<core::LayerBrick>(*map, core::LayerKind::Brick);
    ASSERT_TRUE(sheet && sheet->bricks.size() >= 2);
    // A move alone keeps shape 1.
    sheet->bricks[0].displayArea.translate(8, 0);
    ASSERT_FALSE(d.writeMap(*map).isEmpty());
    EXPECT_LE(schemaOf(d), sync::kDocBaseSchema);
    // A reorder moves it to 2, and is ordered by z.
    std::rotate(sheet->bricks.begin(), sheet->bricks.begin() + 1, sheet->bricks.end());
    const QString top = sheet->bricks.back().guid;
    ASSERT_FALSE(d.writeMap(*map).isEmpty());
    EXPECT_EQ(schemaOf(d), sync::kDocShapeInPlace);
    auto again = mapOf(d);
    EXPECT_EQ(layerOf<core::LayerBrick>(*again, core::LayerKind::Brick)->bricks.back().guid, top);
    // Undo takes back the reorder but leaves the doc in shape 2.
    d.undo();
    EXPECT_EQ(schemaOf(d), sync::kDocShapeInPlace);
    EXPECT_NE(layerOf<core::LayerBrick>(*mapOf(d), core::LayerKind::Brick)->bricks.back().guid, top);
}
