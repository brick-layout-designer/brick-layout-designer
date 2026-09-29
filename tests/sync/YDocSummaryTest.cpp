// Sync phase P2 go/no-go: the desktop reads the shared document the web
// server holds for a layout, through yrs. fixtures/sync/<name>.ydoc is that
// document as the web code builds it (scripts/sync-fixtures), and
// <name>.expected.json what it must contain.

#include "DocJson.h"
#include "WebModel.h"
#include "YDocSummary.h"

#include "core/Map.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <random>

using namespace bld;

namespace {

QByteArray readFile(const QString& name) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/") + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST(YDocSummary, ReadsTheServerDocOfARealLayout) {
    const QByteArray update = readFile(QStringLiteral("tight-corner.ydoc"));
    ASSERT_FALSE(update.isEmpty());
    const QJsonObject expected = QJsonDocument::fromJson(readFile(QStringLiteral("tight-corner.expected.json"))).object();
    ASSERT_FALSE(expected.isEmpty());

    QString err;
    const auto doc = sync::summarizeDoc(update, &err);
    ASSERT_TRUE(doc) << err.toStdString();
    EXPECT_EQ(doc->schemaVersion, expected.value(QStringLiteral("schemaVersion")).toInt());

    const QJsonArray layers = expected.value(QStringLiteral("layers")).toArray();
    ASSERT_EQ(doc->layers.size(), static_cast<size_t>(layers.size()));
    int bricks = 0;
    for (int i = 0; i < layers.size(); ++i) {
        const QJsonObject want = layers[i].toObject();
        const sync::LayerSummary& got = doc->layers[static_cast<size_t>(i)];
        SCOPED_TRACE(want.value(QStringLiteral("name")).toString().toStdString());
        EXPECT_EQ(got.id, want.value(QStringLiteral("id")).toString());
        EXPECT_EQ(got.type, want.value(QStringLiteral("type")).toString());
        EXPECT_EQ(got.name, want.value(QStringLiteral("name")).toString());
        if (got.type != QLatin1String("brick")) continue;
        EXPECT_EQ(got.brickCount, want.value(QStringLiteral("bricks")).toInt());
        bricks += got.brickCount;
        const QJsonObject first = want.value(QStringLiteral("firstBrick")).toObject();
        ASSERT_TRUE(got.firstBrick);
        EXPECT_EQ(got.firstBrick->id, first.value(QStringLiteral("id")).toString());
        EXPECT_EQ(got.firstBrick->partNumber, first.value(QStringLiteral("partNumber")).toString());
        const QJsonObject area = first.value(QStringLiteral("displayArea")).toObject();
        EXPECT_DOUBLE_EQ(got.firstBrick->displayArea.x(), area.value(QStringLiteral("x")).toDouble());
        EXPECT_DOUBLE_EQ(got.firstBrick->displayArea.y(), area.value(QStringLiteral("y")).toDouble());
        EXPECT_DOUBLE_EQ(got.firstBrick->displayArea.width(), area.value(QStringLiteral("width")).toDouble());
        EXPECT_DOUBLE_EQ(got.firstBrick->displayArea.height(), area.value(QStringLiteral("height")).toDouble());
        EXPECT_DOUBLE_EQ(got.firstBrick->orientation, first.value(QStringLiteral("orientation")).toDouble());
    }
    EXPECT_GT(bricks, 0);
}

TEST(YDocSummary, RefusesAnUpdateItCannotApply) {
    QString err;
    EXPECT_FALSE(sync::summarizeDoc(QByteArray("\x05\xff\xff\xff\xff not a yjs update", 30), &err));
    EXPECT_FALSE(err.isEmpty());
    // An empty doc is a valid update with no layers.
    const auto empty = sync::summarizeDoc(QByteArray("\x00\x00", 2), &err);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->layers.empty());
}

TEST(YDocSummary, SurvivesDamagedUpdates) {
    // Byte flips, truncations and duplications of a real document are
    // refused or read, but never crash (a Rust panic would abort here).
    const QByteArray original = readFile(QStringLiteral("tight-corner.ydoc"));
    ASSERT_FALSE(original.isEmpty());
    std::mt19937 rng(0xb10c);
    for (int i = 0; i < 300; ++i) {
        QByteArray data = original;
        const int edits = 1 + static_cast<int>(rng() % 8);
        for (int e = 0; e < edits && !data.isEmpty(); ++e) {
            const auto at = static_cast<qsizetype>(rng() % static_cast<unsigned>(data.size()));
            switch (rng() % 3) {
            case 0: data[at] = static_cast<char>(rng()); break;
            case 1: data.truncate(at); break;
            default: data.insert(at, data.mid(at, static_cast<qsizetype>(rng() % 64))); break;
            }
        }
        QString err;
        (void)sync::summarizeDoc(data, &err);
        if (const auto json = sync::docToJson(data, &err)) (void)sync::mapFromDocJson(*json, &err);
    }
    SUCCEED();
}
