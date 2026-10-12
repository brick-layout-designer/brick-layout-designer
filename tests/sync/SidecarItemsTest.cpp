// The web's sidecarItems, on the desktop (sync/SidecarItems.h): the blob
// hash matches the web's for the shared vectors (web packages/ydoc
// sidecarItems.test.ts), modules and labels are written one per key, and
// two people changing different ones at once both keep their change.

#include "SidecarItems.h"
#include "SyncDoc.h"
#include "WebModel.h"

#include "core/AnchoredLabel.h"
#include "core/Map.h"
#include "core/Module.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>

using namespace bld;

namespace {

std::unique_ptr<core::Map> mapOf(const sync::SyncDoc& doc) {
    QString err;
    auto map = sync::mapFromDocJson(doc.toJson(), &err);
    EXPECT_TRUE(map) << err.toStdString();
    return map;
}

core::AnchoredLabel label(const QString& id, const QString& text) {
    core::AnchoredLabel l;
    l.id = id;
    l.text = text;
    l.offset = QPointF(4, 8);
    return l;
}

const core::AnchoredLabel* findLabel(const core::Map& m, const QString& id) {
    for (const auto& l : m.sidecar.anchoredLabels)
        if (l.id == id) return &l;
    return nullptr;
}

}  // namespace

TEST(SidecarItems, TheBlobHashMatchesTheWebs) {
    const std::pair<QString, QString> vectors[] = {
        { QString::fromUtf8("{}"), QString::fromUtf8("2:5465b825") },
        { QString::fromUtf8("{\"b\":1,\"a\":\"x\"}"), QString::fromUtf8("15:f7b213ed") },
        { QString::fromUtf8("{\"modules\":[{\"id\":\"m1\",\"name\":\"Yard\",\"memberIds\":[\"a\",\"b\"],\"z\":null,\"ok\":true}],\"anchoredLabels\":[]}"), QString::fromUtf8("100:b3f06b1b") },
        { QString::fromUtf8("{\"n\":[0,-1,1.5,0.1,100,1e+21,1.5e-7,123456789012,-0.000001,2.5e-7],\"s\":\"é \\\"q\\\" \\\\ \\n\\t\\u0001 ✓ 😀\"}"), QString::fromUtf8("100:779387ef") },
        { QString::fromUtf8("{\"nested\":{\"z\":{\"y\":[{\"b\":2,\"a\":1}]},\"a\":false}}"), QString::fromUtf8("48:6b434d83") },
    };
    for (const auto& [json, hash] : vectors) {
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        ASSERT_FALSE(doc.isNull()) << json.toStdString();
        EXPECT_EQ(sync::sidecarBlobHash(doc.object()), hash) << json.toStdString();
    }
}

TEST(SidecarItems, NumbersAndKeysAreWrittenAsJavaScriptDoes) {
    const QJsonObject o{ { QStringLiteral("b"), QJsonArray{ 0.1, 100.0, 1e21, 1.5e-7, -0.000001, 123456789012.0, 2.5 } },
                         { QStringLiteral("a"), QStringLiteral("x") } };
    EXPECT_EQ(sync::stableJson(o), QStringLiteral(R"({"a":"x","b":[0.1,100,1e+21,1.5e-7,-0.000001,123456789012,2.5]})"));
}

TEST(SidecarItems, ItemsAreReadOnlyWithTheBlobTheyGoWith) {
    QJsonObject blob{ { QStringLiteral("anchoredLabels"),
                        QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("a") }, { QStringLiteral("text"), QStringLiteral("old") } } } } };
    QJsonObject items = sync::sidecarItemsFor(blob);
    EXPECT_EQ(items.value(QStringLiteral("__blob")).toString(), sync::sidecarBlobHash(blob));
    EXPECT_EQ(items.value(QStringLiteral("l:a")).toObject().value(QStringLiteral("_order")).toDouble(), 0);
    // Someone changed the item: read with the blob it goes with, it wins.
    QJsonObject changed = items.value(QStringLiteral("l:a")).toObject();
    changed.insert(QStringLiteral("text"), QStringLiteral("new"));
    items.insert(QStringLiteral("l:a"), changed);
    const QJsonObject merged = sync::mergedSidecarCache(blob, items);
    EXPECT_EQ(merged.value(QStringLiteral("anchoredLabels")).toArray()[0].toObject().value(QStringLiteral("text")).toString(),
              QStringLiteral("new"));
    EXPECT_FALSE(merged.value(QStringLiteral("anchoredLabels")).toArray()[0].toObject().contains(QStringLiteral("_order")));
    // A writer that doesn't know the items replaced the blob: read it as it is.
    blob.insert(QStringLiteral("views"), QJsonArray());
    EXPECT_EQ(sync::mergedSidecarCache(blob, items), blob);
}

TEST(SidecarItems, WritingTheLayoutWritesEachModuleAndLabel) {
    sync::SyncDoc doc;
    core::Map map;
    map.sidecar.anchoredLabels = { label(QStringLiteral("a"), QStringLiteral("Yard")), label(QStringLiteral("b"), QStringLiteral("Station")) };
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Harbour");
    map.sidecar.modules.push_back(m);
    ASSERT_FALSE(doc.writeMap(map).isEmpty());
    const QJsonObject items = doc.toJson().value(QStringLiteral("sidecarItems")).toObject();
    EXPECT_TRUE(items.contains(QStringLiteral("l:a")));
    EXPECT_TRUE(items.contains(QStringLiteral("l:b")));
    EXPECT_TRUE(items.contains(QStringLiteral("m:m1")));
    const QJsonObject cache = doc.toJson().value(QStringLiteral("meta")).toObject().value(QStringLiteral("cache")).toObject();
    EXPECT_EQ(items.value(QStringLiteral("__blob")).toString(), sync::sidecarBlobHash(cache));
    // A label gone is gone from the items too.
    map.sidecar.anchoredLabels.pop_back();
    doc.writeMap(map);
    EXPECT_FALSE(doc.toJson().value(QStringLiteral("sidecarItems")).toObject().contains(QStringLiteral("l:b")));
}

// The point of it: here a label is renamed while, at the same time, someone
// else adds a module and renames the other label. All three survive.
TEST(SidecarItems, ConcurrentEditsToDifferentLabelsAndModulesAllSurvive) {
    sync::SyncDoc base;
    core::Map start;
    start.sidecar.anchoredLabels = { label(QStringLiteral("a"), QStringLiteral("Yard")), label(QStringLiteral("b"), QStringLiteral("Station")) };
    const QByteArray seed = base.writeMap(start);
    sync::SyncDoc here, there;
    ASSERT_TRUE(here.applyUpdate(seed));
    ASSERT_TRUE(there.applyUpdate(seed));
    auto mapHere = mapOf(here);
    auto mapThere = mapOf(there);
    mapHere->sidecar.anchoredLabels[0].text = QStringLiteral("Goods yard");
    mapThere->sidecar.anchoredLabels[1].text = QStringLiteral("Central");
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Harbour");
    mapThere->sidecar.modules.push_back(m);
    const QByteArray fromHere = here.writeMap(*mapHere);
    const QByteArray fromThere = there.writeMap(*mapThere);
    ASSERT_TRUE(here.applyUpdate(fromThere));
    ASSERT_TRUE(there.applyUpdate(fromHere));

    for (const sync::SyncDoc* doc : { &here, &there }) {
        const auto merged = mapOf(*doc);
        ASSERT_TRUE(findLabel(*merged, QStringLiteral("a")) && findLabel(*merged, QStringLiteral("b")));
        EXPECT_EQ(findLabel(*merged, QStringLiteral("a"))->text, QStringLiteral("Goods yard"));
        EXPECT_EQ(findLabel(*merged, QStringLiteral("b"))->text, QStringLiteral("Central"));
        ASSERT_EQ(merged->sidecar.modules.size(), 1U);
        EXPECT_EQ(merged->sidecar.modules.front().name, QStringLiteral("Harbour"));
    }
}

TEST(SidecarItems, ASidecarWithRepeatedOrMissingIdsIsReadWhole) {
    // As the web's readSidecarCache: per-id items can't hold such entries.
    const QJsonObject odd = QJsonDocument::fromJson(R"({"schemaVersion":1,"bbmHashSha256":"",
        "modules":[{"id":"dup","name":"One","members":[]},{"id":"dup","name":"Two","members":[]},{"name":"No id","members":[]}],
        "anchoredLabels":[{"text":"No id"}]})").object();
    EXPECT_FALSE(sync::sidecarIdsUnique(odd));
    QJsonObject items = sync::sidecarItemsFor(odd);
    items.insert(QStringLiteral("__blob"), sync::sidecarBlobHash(odd));
    EXPECT_EQ(sync::mergedSidecarCache(odd, items), odd);
    const QJsonObject fine = QJsonDocument::fromJson(R"({"modules":[{"id":"a","members":[]}]})").object();
    EXPECT_TRUE(sync::sidecarIdsUnique(fine));
}
