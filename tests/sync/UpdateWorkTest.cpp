// What applying an update takes (UpdateGuard.h updateWork): everything the
// apps send passes, from a first sync to deleting a whole layout or a
// 5,000-part layout; an update listing one deleted range many times, or
// splitting one long block many times (by deletions or by inserts), is
// refused before yrs sees it, quickly, and leaves the document as it was.

#include "DocJson.h"
#include "SyncDoc.h"
#include "UpdateGuard.h"
#include "WebModel.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QFile>

#include <utility>
#include <vector>

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

QByteArray varString(const QByteArray& s) {
    return varUint(static_cast<quint64>(s.size())) + s;
}

// Client 7: `n` list entries of root list "a", one after another, values
// and strings in turn (so no two make one block).
QByteArray manyBlocks(int n) {
    QByteArray u = varUint(1) + varUint(static_cast<quint64>(n)) + varUint(7) + varUint(0);
    for (int i = 0; i < n; ++i) {
        if (i == 0) {
            u += QByteArray(1, '\x08') + varUint(1) + varString("a"); // values, root "a"
        } else {
            u += QByteArray(1, static_cast<char>((i % 2 ? 4 : 8) | 0x80)) + varUint(7)
                 + varUint(static_cast<quint64>(i - 1));
        }
        u += (i % 2 && i != 0) ? varString("x") : varUint(1) + QByteArray(1, '\x78'); // "x", or [true]
    }
    return u + varUint(0);
}

// Client 9: one block of `n` values in root list "b".
QByteArray longBlock(int n) {
    QByteArray u = varUint(1) + varUint(1) + varUint(9) + varUint(0);
    u += QByteArray(1, '\x08') + varUint(1) + varString("b") + varUint(static_cast<quint64>(n));
    u += QByteArray(n, '\x78');
    return u + varUint(0);
}

// Only deletions: `ranges` of `client`.
QByteArray deletions(quint64 client, const std::vector<std::pair<quint64, quint64>>& ranges) {
    QByteArray u = varUint(0) + varUint(1) + varUint(client) + varUint(ranges.size());
    for (const auto& [clock, len] : ranges) u += varUint(clock) + varUint(len);
    return u;
}

// Client 11: `n` strings, each between values 3i and 3i+1 of the long block.
QByteArray insertsInto(int n) {
    QByteArray u = varUint(1) + varUint(static_cast<quint64>(n)) + varUint(11) + varUint(0);
    for (quint64 i = 0; i < static_cast<quint64>(n); ++i)
        u += QByteArray(1, static_cast<char>(4 | 0x80 | 0x40)) + varUint(9) + varUint(3 * i) + varUint(9)
             + varUint(3 * i + 1) + varString("y");
    return u + varUint(0);
}

// Expect `update` refused, quickly, with the document as it was.
void expectRefused(sync::SyncDoc& d, const QByteArray& update, const char* what) {
    const QByteArray before = d.encodeState();
    QElapsedTimer t;
    t.start();
    QString err;
    EXPECT_FALSE(d.applyUpdate(update, &err)) << what;
    EXPECT_LT(t.elapsed(), 2000) << what;
    EXPECT_TRUE(err.contains(QLatin1String("steps"))) << what << ": " << err.toStdString();
    EXPECT_EQ(d.encodeState(), before) << what;
}

} // namespace

TEST(UpdateWork, EverythingTheAppsSendPasses) {
    for (const int copies : { 1, 6 }) { // the 940-part layout, and one of 5,000 parts and more
        sync::SyncDoc source;
        QString err;
        ASSERT_TRUE(source.applyUpdate(readFixture(QStringLiteral("sync/layers.ydoc")), &err))
            << err.toStdString();
        auto map = sync::mapFromDocJson(source.toJson());
        ASSERT_TRUE(map);
        size_t parts = 0;
        for (auto& l : map->layers()) {
            if (l->kind() != core::LayerKind::Brick) continue;
            auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
            const auto base = bricks;
            for (int c = 1; c < copies; ++c)
                for (auto b : base) {
                    b.guid += QStringLiteral("-%1").arg(c);
                    b.displayArea.translate(500.0 * c, 0);
                    bricks.push_back(b);
                }
            parts += bricks.size();
        }
        if (copies > 1) {
            EXPECT_GE(parts, 5000u);
            ASSERT_FALSE(source.writeMap(*map).isEmpty());
        }

        // A first sync of it.
        sync::SyncDoc replica;
        const QByteArray whole = source.encodeState();
        EXPECT_LE(sync::guard::updateWork({}, whole), sync::guard::kMaxUpdateWork / 20);
        ASSERT_TRUE(replica.applyUpdate(whole, &err)) << err.toStdString();

        // Half its parts deleted, then every part: each sent on.
        for (const bool everything : { false, true }) {
            const QByteArray sv = replica.stateVector();
            auto edited = sync::mapFromDocJson(source.toJson());
            ASSERT_TRUE(edited);
            for (auto& l : edited->layers()) {
                if (l->kind() != core::LayerKind::Brick) continue;
                auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
                if (everything) bricks.clear();
                else
                    for (size_t i = 0; i < bricks.size(); i += 1)
                        bricks.erase(bricks.begin() + static_cast<std::ptrdiff_t>(i));
            }
            source.writeMap(*edited);
            const QByteArray update = source.diffSince(sv);
            EXPECT_LE(sync::guard::updateWork(replica.encodeState(), update),
                      sync::guard::kMaxUpdateWork / 20);
            ASSERT_TRUE(replica.applyUpdate(update, &err)) << err.toStdString();
        }
        EXPECT_EQ(replica.toJson(), source.toJson());
    }
}

TEST(UpdateWork, ARangeListedManyTimesIsRefused) {
    sync::SyncDoc d;
    QString err;
    ASSERT_TRUE(d.applyUpdate(manyBlocks(5000), &err)) << err.toStdString();
    // Once is nothing much; 2,000 times walks the 5,000 blocks 2,000 times.
    EXPECT_LE(sync::guard::updateWork(d.encodeState(), deletions(7, { { 0, 5000 } })), 10'000);
    expectRefused(d, deletions(7, std::vector<std::pair<quint64, quint64>>(2000, { 0, 5000 })),
                  "one range 2,000 times");
    ASSERT_TRUE(d.applyUpdate(deletions(7, { { 0, 5000 } }), &err)) << err.toStdString();
}

TEST(UpdateWork, SplittingOneLongBlockManyTimesIsRefused) {
    sync::SyncDoc d;
    QString err;
    ASSERT_TRUE(d.applyUpdate(longBlock(200'000), &err)) << err.toStdString();
    std::vector<std::pair<quint64, quint64>> many(100);
    for (quint64 i = 0; i < many.size(); ++i) many[i] = { i * 2, 1 };
    expectRefused(d, deletions(9, many), "100 deletions inside one block");
    expectRefused(d, insertsInto(100), "100 inserts inside one block");
    // A few of either are fine.
    ASSERT_TRUE(d.applyUpdate(deletions(9, { { 10, 2 }, { 150'000, 3 } }), &err)) << err.toStdString();
    ASSERT_TRUE(d.applyUpdate(insertsInto(3), &err)) << err.toStdString();
}
