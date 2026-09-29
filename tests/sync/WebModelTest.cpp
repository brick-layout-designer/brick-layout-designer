// Sync phase P3a, cross-language: the shared document the web server builds
// for a .bbm (fixtures/sync/<name>.ydoc, scripts/sync-fixtures), read by the
// desktop through yrs and mapped onto core::Map, must write the very bytes
// the desktop writes for that .bbm itself. layers.bbm has every layer kind:
// grid, brick, text (with groups), ruler, and area.

#include "DocJson.h"
#include "WebModel.h"

#include "core/LayerBrick.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QFile>
#include <QJsonArray>

using namespace bld;

namespace {

QByteArray readFile(const QString& path) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/") + path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray bbmBytes(const core::Map& map) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    const auto res = saveload::writeBbm(map, buf);
    EXPECT_TRUE(res.ok) << res.error.toStdString();
    return buf.data();
}

struct Case {
    const char* ydoc;
    const char* bbm;
};

class WebModelRoundTrip : public ::testing::TestWithParam<Case> {};

}  // namespace

TEST_P(WebModelRoundTrip, WritesWhatTheDesktopWritesForTheSameBbm) {
    const Case c = GetParam();
    const QByteArray bbm = readFile(QString::fromLatin1(c.bbm));
    ASSERT_FALSE(bbm.isEmpty());
    QBuffer in;
    in.setData(bbm);
    in.open(QIODevice::ReadOnly);
    const auto desktop = saveload::readBbm(in);
    ASSERT_TRUE(desktop.ok()) << desktop.error.toStdString();

    QString err;
    const auto json = sync::docToJson(readFile(QString::fromLatin1(c.ydoc)), &err);
    ASSERT_TRUE(json) << err.toStdString();
    const auto fromDoc = sync::mapFromDocJson(*json, &err);
    ASSERT_TRUE(fromDoc) << err.toStdString();

    const QByteArray want = bbmBytes(*desktop.map);
    const QByteArray got = bbmBytes(*fromDoc);
    if (got != want) {
        // Point at the first differing line rather than dumping 500 kB.
        const QList<QByteArray> a = got.split('\n'), b = want.split('\n');
        for (qsizetype i = 0; i < std::min(a.size(), b.size()); ++i)
            ASSERT_EQ(a[i].toStdString(), b[i].toStdString()) << "line " << i + 1;
        ASSERT_EQ(a.size(), b.size());
    }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, WebModelRoundTrip,
    ::testing::Values(Case{ "fixtures/sync/tight-corner.ydoc", "fixtures/bbm-corpus/tight-corner.bbm" },
                      Case{ "fixtures/sync/layers.ydoc", "fixtures/sync/layers.bbm" }),
    [](const auto& info) { return info.index == 0 ? std::string("TightCorner") : std::string("AllLayerKinds"); });

TEST(WebModel, RefusesADocWithoutAHeaderOrWithAnUnknownLayer) {
    QString err;
    EXPECT_FALSE(sync::mapFromDocJson(QJsonObject{}, &err));
    EXPECT_FALSE(err.isEmpty());
    const QJsonObject meta{ { QStringLiteral("version"), 9 } };
    const QJsonObject bad{
        { QStringLiteral("meta"), meta },
        { QStringLiteral("layers"), QJsonArray{ QStringLiteral("L") } },
        { QStringLiteral("layerData"), QJsonObject{ { QStringLiteral("L"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("hologram") } } } } },
    };
    EXPECT_FALSE(sync::mapFromDocJson(bad, &err));
    EXPECT_EQ(err, QStringLiteral("unknown layer type \"hologram\""));
    // Ids in the order without layer data are skipped, as the web does.
    const QJsonObject empty{ { QStringLiteral("meta"), meta }, { QStringLiteral("layers"), QJsonArray{ QStringLiteral("gone") } } };
    const auto map = sync::mapFromDocJson(empty, &err);
    ASSERT_TRUE(map);
    EXPECT_TRUE(map->layers().empty());
}

TEST(WebModel, ItemsKeepTheirDocumentIds) {
    // Bricks, groups and text cells carry the doc's ids: sync matches edits by them.
    QString err;
    const auto json = sync::docToJson(readFile(QStringLiteral("fixtures/sync/layers.ydoc")), &err);
    ASSERT_TRUE(json) << err.toStdString();
    const auto map = sync::mapFromDocJson(*json, &err);
    ASSERT_TRUE(map);
    const QJsonObject data = json->value(QStringLiteral("layerData")).toObject();
    int texts = 0, bricks = 0;
    for (const auto& layer : map->layers()) {
        const QJsonObject l = data.value(layer->guid).toObject();
        if (layer->kind() == core::LayerKind::Text) {
            const auto& cells = static_cast<const core::LayerText&>(*layer).textCells;
            const QJsonArray docCells = l.value(QStringLiteral("textCells")).toArray();
            ASSERT_EQ(cells.size(), static_cast<size_t>(docCells.size()));
            for (size_t i = 0; i < cells.size(); ++i) {
                EXPECT_FALSE(cells[i].guid.isEmpty());
                EXPECT_EQ(cells[i].guid, docCells[static_cast<qsizetype>(i)].toObject().value(QStringLiteral("id")).toString());
            }
            texts += static_cast<int>(cells.size());
        } else if (layer->kind() == core::LayerKind::Brick) {
            const auto& bl = static_cast<const core::LayerBrick&>(*layer);
            const QJsonArray docBricks = l.value(QStringLiteral("bricks")).toArray();
            for (size_t i = 0; i < bl.bricks.size(); ++i)
                EXPECT_EQ(bl.bricks[i].guid, docBricks[static_cast<qsizetype>(i)].toObject().value(QStringLiteral("id")).toString());
            bricks += static_cast<int>(bl.bricks.size());
        }
    }
    EXPECT_GT(texts, 0);
    EXPECT_GT(bricks, 0);
}
