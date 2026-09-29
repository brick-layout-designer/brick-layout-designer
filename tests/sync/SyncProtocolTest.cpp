// The y-websocket wire format (sync phase P3c). The golden bytes come from
// the web server's own y-protocols / lib0 encoders.

#include "SyncProtocol.h"

#include <gtest/gtest.h>

using namespace bld::sync::protocol;

namespace {
QByteArray hex(const char* h) { return QByteArray::fromHex(h); }
}  // namespace

TEST(SyncProtocol, EncodesLikeYProtocols) {
    EXPECT_EQ(encode(Kind::SyncStep1, hex("00")), hex("00000100"));
    EXPECT_EQ(encode(Kind::SyncStep2, hex("010203")), hex("000103010203"));
    EXPECT_EQ(encode(Kind::Awareness, hex("0908")), hex("01020908"));
    // A 300-byte update: its length takes two varuint bytes.
    QByteArray payload(300, '\0');
    for (int i = 0; i < 300; ++i) payload[i] = static_cast<char>(i & 0xff);
    EXPECT_TRUE(encode(Kind::Update, payload).startsWith(hex("0002ac020001")));
}

TEST(SyncProtocol, DecodesWhatItEncodes) {
    for (const Kind k : { Kind::SyncStep1, Kind::SyncStep2, Kind::Update, Kind::Awareness }) {
        const QByteArray payload = QByteArray(200, 'x') + "tail";
        const auto m = decode(encode(k, payload));
        ASSERT_TRUE(m);
        EXPECT_EQ(m->kind, k);
        EXPECT_EQ(m->payload, payload);
    }
    EXPECT_EQ(decode(hex("000100"))->payload, QByteArray());  // empty step 2
}

TEST(SyncProtocol, RefusesMalformedMessages) {
    EXPECT_FALSE(decode(QByteArray()));
    EXPECT_FALSE(decode(hex("00")));            // no step
    EXPECT_FALSE(decode(hex("0003")));          // unknown sync step
    EXPECT_FALSE(decode(hex("03")));            // unknown message type (ignored)
    EXPECT_FALSE(decode(hex("000105aabb")));    // payload shorter than its length
    EXPECT_FALSE(decode(hex("0001ffffffffffffffffff01")));  // length overflows
    EXPECT_FALSE(decode(hex("01")));            // awareness without payload
}
