// Presence (y-protocols awareness): the desktop reads an update the web
// editor made (fixtures/sync/awareness.bin, from
// scripts/sync-fixtures/make-awareness-fixture.mjs), writes updates the
// same way, and keeps other people's states as y-protocols does.

#include "Awareness.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

using namespace bld::sync::awareness;

namespace {
QByteArray fixture(const char* name) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

TEST(Awareness, ReadsWhatTheWebEditorSends) {
    const QByteArray update = fixture("awareness.bin");
    ASSERT_FALSE(update.isEmpty());
    const auto entries = decode(update);
    ASSERT_TRUE(entries);
    const QJsonArray expected = QJsonDocument::fromJson(fixture("awareness.expected.json")).object().value(QLatin1String("clients")).toArray();
    ASSERT_EQ(entries->size(), expected.size());
    for (int i = 0; i < entries->size(); ++i) {
        const QJsonObject want = expected[i].toObject();
        EXPECT_EQ((*entries)[i].clientId, static_cast<quint32>(want.value(QLatin1String("id")).toInteger()));
        if (want.value(QLatin1String("state")).isNull()) EXPECT_FALSE((*entries)[i].state);
        else EXPECT_EQ((*entries)[i].state, want.value(QLatin1String("state")).toObject());
    }
    // Written back, it reads the same.
    EXPECT_EQ(decode(encode(*entries))->at(0).state, entries->at(0).state);
}

TEST(Awareness, KeepsPeersAsYProtocolsDoes) {
    Peers peers(42);
    const QJsonObject alice{ { QStringLiteral("user"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Alice") } } } };
    const QJsonObject moved{ { QStringLiteral("cursor"), QJsonObject{ { QStringLiteral("x"), 1 }, { QStringLiteral("y"), 2 } } } };
    EXPECT_TRUE(peers.apply(encode({ { 7, 3, alice }, { 42, 9, alice } })));
    EXPECT_EQ(peers.states().size(), 1);  // our own id is ignored
    EXPECT_FALSE(peers.apply(encode({ { 7, 2, moved } })));  // older clock
    EXPECT_EQ(peers.states().value(7), alice);
    EXPECT_TRUE(peers.apply(encode({ { 7, 4, moved } })));
    EXPECT_EQ(peers.states().value(7), moved);
    EXPECT_TRUE(peers.apply(encode({ { 7, 4, std::nullopt } })));  // left
    EXPECT_TRUE(peers.states().isEmpty());
    EXPECT_FALSE(peers.apply(QByteArrayLiteral("\x05\x01")));  // truncated
    EXPECT_FALSE(decode(QByteArrayLiteral("\x01\x07\x01\x10{}")));
}
