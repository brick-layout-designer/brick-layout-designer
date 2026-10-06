// UpdateGuard: real documents and the updates the desktop writes pass;
// updates that claim more than their bytes hold, nest too deep, are too big
// or hold what layouts never use are refused before yrs reads them, quickly,
// and leave the document as it was.

#include "DocJson.h"
#include "SyncDoc.h"
#include "UpdateGuard.h"
#include "WebModel.h"
#include "YDocSummary.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

using namespace bld;

namespace {

QByteArray readFixture(const QString& path) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/") + path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray varUint(quint64 v) {
    QByteArray out;
    while (v > 0x7f) {
        out.append(static_cast<char>(0x80 | (v & 0x7f)));
        v >>= 7;
    }
    out.append(static_cast<char>(v));
    return out;
}

QByteArray varString(const QByteArray& s) { return varUint(static_cast<quint64>(s.size())) + s; }

// One client (7) writing meta[key] with content `contentRef` (8: lib0 Any
// values) encoded as `content`, and an empty delete set: what Yjs sends for
// `meta.set(key, value)`.
QByteArray setMetaContent(const QByteArray& key, const QByteArray& content, quint8 contentRef) {
    QByteArray u;
    u += varUint(1);              // clients
    u += varUint(1);              // blocks
    u += varUint(7);              // client id
    u += varUint(0);              // clock
    u += static_cast<char>(contentRef | 0x20);  // no origins, a map key
    u += varUint(1) + varString("meta");        // parent: the root map "meta"
    u += varString(key);
    u += content;
    u += varUint(0);              // delete set: no clients
    return u;
}

// meta[key] = one lib0 Any value.
QByteArray setMeta(const QByteArray& key, const QByteArray& value) { return setMetaContent(key, varUint(1) + value, 8); }

// lib0 Any: `depth` arrays, one inside the other, around a null.
QByteArray nestedArrays(int depth) {
    QByteArray v;
    for (int i = 0; i < depth; ++i) v += QByteArray("\x75\x01", 2);  // array of one
    v += '\x7e';                                                      // null
    return v;
}

}  // namespace

TEST(UpdateGuard, PassesRealDocumentsAndWhatTheDesktopWrites) {
    for (const QString& name : { QStringLiteral("sync/tight-corner.ydoc"), QStringLiteral("sync/layers.ydoc") }) {
        const QByteArray doc = readFixture(name);
        ASSERT_FALSE(doc.isEmpty()) << name.toStdString();
        EXPECT_EQ(sync::guard::checkUpdate(doc), QString()) << name.toStdString();

        sync::SyncDoc d;
        QString err;
        ASSERT_TRUE(d.applyUpdate(doc, &err)) << err.toStdString();
        EXPECT_EQ(sync::guard::checkUpdate(d.encodeState()), QString());
        EXPECT_EQ(sync::guard::checkStateVector(d.stateVector()), QString());

        // Edits written back: moves, deletes and new bricks.
        auto map = sync::mapFromDocJson(d.toJson());
        ASSERT_TRUE(map);
        for (auto& l : map->layers()) {
            if (l->kind() != core::LayerKind::Brick) continue;
            auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
            if (bricks.empty()) continue;
            bricks.front().displayArea.translate(8, 8);
            bricks.front().orientation = 90.0f;
            bricks.push_back(bricks.front());
            bricks.back().guid = QStringLiteral("guard-test-brick");
            bricks.erase(bricks.begin());
        }
        const QByteArray edit = d.writeMap(*map);
        ASSERT_FALSE(edit.isEmpty());
        EXPECT_EQ(sync::guard::checkUpdate(edit), QString());
        EXPECT_EQ(sync::guard::checkUpdate(d.diffSince(QByteArray(1, '\0'))), QString());
    }
    // The empty document, and values nested a little, as the web writes them.
    EXPECT_EQ(sync::guard::checkUpdate(QByteArray("\x00\x00", 2)), QString());
    EXPECT_EQ(sync::guard::checkStateVector(QByteArray(1, '\0')), QString());
    const QByteArray nested = setMeta("x", nestedArrays(3));
    EXPECT_EQ(sync::guard::checkUpdate(nested), QString());
    sync::SyncDoc d;
    QString err;
    ASSERT_TRUE(d.applyUpdate(nested, &err)) << err.toStdString();
    // As text: brace-initialising a QJsonArray from a QJsonArray copies it on
    // some compilers instead of nesting it.
    const QJsonArray x{ d.toJson().value(QStringLiteral("meta")).toObject().value(QStringLiteral("x")) };
    EXPECT_EQ(QJsonDocument(x).toJson(QJsonDocument::Compact), QByteArray("[[[[null]]]]"));
}

// Byte strings from https://github.com/y-crdt/y-crdt/issues/675 and
// friends: a few bytes that claim billions of things.
TEST(UpdateGuard, RefusesClaimsTheBytesCantHold) {
    struct Case {
        const char* what;
        QByteArray update;
    };
    const Case cases[] = {
        { "4 billion clients", QByteArray("\xff\xff\xff\xff\x0f\x00\x00\x00", 8) },
        { "204 million blocks", varUint(1) + varUint(204'000'000) + varUint(1) + varUint(0) },
        { "a 4 GB string", setMeta("x", QByteArray("\x77", 1) + varUint(0xffffffffu)) },
        { "250 million object entries", setMeta("x", QByteArray("\x76\xff\xff\xff\x7a", 5)) },
        { "250 million array items", setMeta("x", QByteArray("\x75\xff\xff\xff\x7a", 5)) },
        { "4 billion values", setMetaContent("x", varUint(0xffffffffu), 8) },
        { "4 billion JSON values", setMetaContent("x", varUint(0xfffffffeu), 2) },
        { "4 billion deleted ranges", QByteArray("\x00\x01\x07\xff\xff\xff\xff\x0f", 8) },
        { "a number past 64 bits", QByteArray(11, '\xff') },
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.what);
        QElapsedTimer t;
        t.start();
        EXPECT_NE(sync::guard::checkUpdate(c.update), QString());
        sync::SyncDoc d;
        const QJsonObject before = d.toJson();
        QString err;
        EXPECT_FALSE(d.applyUpdate(c.update, &err));
        EXPECT_TRUE(err.startsWith(QLatin1String("refused"))) << err.toStdString();
        EXPECT_EQ(d.toJson(), before);
        EXPECT_FALSE(sync::summarizeDoc(c.update));
        EXPECT_LT(t.elapsed(), 1000);
    }
}

TEST(UpdateGuard, RefusesAStateVectorClaimingBillionsOfClients) {
    const QByteArray sv("\xff\xff\xff\xff\x0f\x01", 6);
    EXPECT_NE(sync::guard::checkStateVector(sv), QString());
    // The server's answer to it is the whole document, at once.
    sync::SyncDoc d;
    ASSERT_TRUE(d.applyUpdate(readFixture(QStringLiteral("sync/tight-corner.ydoc"))));
    QElapsedTimer t;
    t.start();
    EXPECT_EQ(d.diffSince(sv), d.encodeState());
    EXPECT_LT(t.elapsed(), 1000);
}

TEST(UpdateGuard, RefusesValuesNestedTooDeep) {
    // yrs reads each level by recursing: deep enough overflows the stack.
    EXPECT_EQ(sync::guard::checkUpdate(setMeta("x", nestedArrays(sync::guard::kMaxNesting))), QString());
    const QByteArray deep = setMeta("x", nestedArrays(100'000));
    EXPECT_NE(sync::guard::checkUpdate(deep), QString());
    sync::SyncDoc d;
    EXPECT_FALSE(d.applyUpdate(deep));
}

TEST(UpdateGuard, RefusesWhatLayoutsNeverHold) {
    // A weak link (yrs-only), an unknown content kind and an unknown type.
    const QByteArray weak = setMetaContent("x", QByteArray("\x07\x00", 2), 7);
    EXPECT_TRUE(sync::guard::checkUpdate(weak).contains(QLatin1String("weak link")));
    EXPECT_NE(sync::guard::checkUpdate(setMetaContent("x", QByteArray(), 11)), QString());
    EXPECT_NE(sync::guard::checkUpdate(setMetaContent("x", QByteArray(1, '\x0c'), 7)), QString());
    // Strings and keys with a NUL in them (yffi panics reading them out);
    // bytes may hold anything.
    EXPECT_TRUE(sync::guard::checkUpdate(setMeta("x", QByteArray("\x77\x03" "a\0b", 5))).contains(QLatin1String("NUL")));
    EXPECT_NE(sync::guard::checkUpdate(setMeta(QByteArray("x\0y", 3), QByteArray(1, '\x7e'))), QString());
    EXPECT_EQ(sync::guard::checkUpdate(setMeta("x", QByteArray("\x74\x03" "a\0b", 5))), QString());
    // A map typed value nested in meta is fine.
    EXPECT_EQ(sync::guard::checkUpdate(setMetaContent("x", QByteArray(1, '\x01'), 7)), QString());
    // An update that ends early.
    const QByteArray doc = readFixture(QStringLiteral("sync/tight-corner.ydoc"));
    EXPECT_NE(sync::guard::checkUpdate(doc.left(doc.size() / 2)), QString());
}

// An empty GC block after another one: yrs 0.28 divides by zero looking
// up a clock in that client's blocks, and the panic aborts the app.
TEST(UpdateGuard, RefusesAnEmptyGcBlock) {
    // One client (5) with two GC blocks of length 1 and 0; no deletions.
    const QByteArray empty("\x01\x02\x05\x00\x00\x01\x00\x00\x00", 9);
    EXPECT_TRUE(sync::guard::checkUpdate(empty).contains(QLatin1String("empty block")));
    sync::SyncDoc d;
    EXPECT_FALSE(d.applyUpdate(empty));
    // The same with lengths 1 and 2 is fine.
    EXPECT_EQ(sync::guard::checkUpdate(QByteArray("\x01\x02\x05\x00\x00\x01\x00\x02\x00", 9)), QString());
}

TEST(UpdateGuard, RefusesAnUpdateOverTheSizeLimit) {
    const QByteArray big(sync::guard::kMaxUpdateBytes + 1, '\0');
    EXPECT_TRUE(sync::guard::checkUpdate(big).contains(QLatin1String("limit")));
    EXPECT_TRUE(sync::guard::checkStateVector(big).contains(QLatin1String("limit")));
}

// Inputs the ydoc fuzzer (or the upstream report) turned up, replayed.
TEST(UpdateGuard, FuzzRegressionsAreRefusedQuickly) {
    const QDir dir(QStringLiteral(BLD_SOURCE_DIR "/fixtures/fuzz-regressions"));
    const QStringList files = dir.entryList({ QStringLiteral("ydoc-*.bin") }, QDir::Files);
    ASSERT_FALSE(files.isEmpty());
    for (const QString& name : files) {
        SCOPED_TRACE(name.toStdString());
        const QByteArray update = readFixture(QStringLiteral("fuzz-regressions/") + name);
        QElapsedTimer t;
        t.start();
        (void)sync::summarizeDoc(update);
        sync::SyncDoc d;
        EXPECT_FALSE(d.applyUpdate(update));
        EXPECT_LT(t.elapsed(), 2000);
    }
}
