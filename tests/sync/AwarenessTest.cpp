// Presence (y-protocols awareness): the desktop reads an update the web
// editor made (fixtures/sync/awareness.bin, from
// scripts/sync-fixtures/make-awareness-fixture.mjs), writes updates the
// same way, and keeps other people's states as y-protocols does.

#include "Awareness.h"
#include "Presence.h"

#include <gtest/gtest.h>

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

using namespace bld::sync::awareness;

namespace {
QByteArray fixture(const char* name) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

TEST(Awareness, ReadsWhatTheWebEditorSends) {
    const QByteArray update = fixture("awareness.bin");
    ASSERT_FALSE(update.isEmpty());
    const auto entries = decode(update);
    ASSERT_TRUE(entries);
    const QJsonArray expected = QJsonDocument::fromJson(fixture("awareness.expected.json"))
                                    .object()
                                    .value(QLatin1String("clients"))
                                    .toArray();
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
    const QJsonObject alice{ { QStringLiteral("user"),
                               QJsonObject{ { QStringLiteral("name"), QStringLiteral("Alice") } } } };
    const QJsonObject moved{ { QStringLiteral("cursor"),
                               QJsonObject{ { QStringLiteral("x"), 1 }, { QStringLiteral("y"), 2 } } } };
    EXPECT_TRUE(peers.apply(encode({ { 7, 3, alice }, { 42, 9, alice } })));
    EXPECT_EQ(peers.states().size(), 1);                    // our own id is ignored
    EXPECT_FALSE(peers.apply(encode({ { 7, 2, moved } }))); // older clock
    EXPECT_EQ(peers.states().value(7), alice);
    EXPECT_TRUE(peers.apply(encode({ { 7, 4, moved } })));
    EXPECT_EQ(peers.states().value(7), moved);
    EXPECT_TRUE(peers.apply(encode({ { 7, 4, std::nullopt } }))); // left
    EXPECT_TRUE(peers.states().isEmpty());
    EXPECT_FALSE(peers.apply(QByteArrayLiteral("\x05\x01"))); // truncated
    EXPECT_FALSE(decode(QByteArrayLiteral("\x01\x07\x01\x10{}")));
}

TEST(Presence, ColoursMatchTheWebAndStatesUseItsShape) {
    namespace pr = bld::sync::presence;
    // Values from the web's deterministicColor.
    EXPECT_EQ(pr::colorFor(QStringLiteral("u-alice"), QStringLiteral("L1")), QStringLiteral("#f472b6"));
    EXPECT_EQ(
        pr::colorFor(QStringLiteral("0b8f7e1c-2d3a-4c5b-9e6f-7a8b9c0d1e2f"), QStringLiteral("layout-42")),
        QStringLiteral("#fb923c"));
    EXPECT_EQ(pr::colorFor(QStringLiteral("é"), QStringLiteral("x")), QStringLiteral("#f472b6"));

    const QJsonObject s =
        pr::state({ QStringLiteral("u1"), QStringLiteral("Aaron"), QStringLiteral("#60a5fa") },
                  QPointF(12.5, -3), { QStringLiteral("b1") }, 1000);
    const auto peer = pr::peerFrom(s);
    EXPECT_EQ(peer.name, QStringLiteral("Aaron"));
    EXPECT_EQ(peer.color, QStringLiteral("#60a5fa"));
    ASSERT_TRUE(peer.cursor);
    EXPECT_EQ(*peer.cursor, QPointF(12.5, -3));
    EXPECT_EQ(peer.brickIds, QStringList{ QStringLiteral("b1") });
    EXPECT_TRUE(s.value(QLatin1String("cursor")).toObject().contains(QLatin1String("layerId")));
    EXPECT_FALSE(pr::peerFrom(pr::state({}, std::nullopt, {}, 0)).cursor);
    // What the web sent in the fixture reads too.
    const auto entries = decode(fixture("awareness.bin"));
    EXPECT_TRUE(pr::peerFrom(*entries->at(0).state).cursor);
    EXPECT_EQ(pr::label(QStringLiteral("A very long display name indeed")),
              QStringLiteral("A very long display…"));
}

TEST(Awareness, DropsPeersNotHeardFromInAWhile) {
    Peers peers(1);
    ASSERT_TRUE(peers.apply(
        encode({ { 7, 1, QJsonObject{ { QStringLiteral("tool"), QStringLiteral("select") } } } })));
    EXPECT_FALSE(peers.dropOlderThan(QDateTime::currentMSecsSinceEpoch() - 30000)); // heard just now
    EXPECT_TRUE(peers.dropOlderThan(QDateTime::currentMSecsSinceEpoch() + 1));      // as if 30 s went by
    EXPECT_TRUE(peers.states().isEmpty());
}
