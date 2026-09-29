#include "SyncProtocol.h"

namespace bld::sync::protocol {

namespace {

constexpr quint64 kMessageSync = 0;
constexpr quint64 kMessageAwareness = 1;

void writeVarUint(QByteArray& out, quint64 v) {
    while (v > 0x7f) {
        out.append(static_cast<char>(0x80 | (v & 0x7f)));
        v >>= 7;
    }
    out.append(static_cast<char>(v));
}

// lib0 readVarUint; nullopt when the input ends early or overflows 53 bits
// (lib0's limit, JavaScript's safe integers).
std::optional<quint64> readVarUint(const QByteArray& in, qsizetype& pos) {
    quint64 v = 0;
    for (int shift = 0; shift < 53; shift += 7) {
        if (pos >= in.size()) return std::nullopt;
        const auto byte = static_cast<quint8>(in[pos++]);
        v |= static_cast<quint64>(byte & 0x7f) << shift;
        if (!(byte & 0x80)) return v;
    }
    return std::nullopt;
}

std::optional<QByteArray> readVarBytes(const QByteArray& in, qsizetype& pos) {
    const auto len = readVarUint(in, pos);
    if (!len || *len > static_cast<quint64>(in.size() - pos)) return std::nullopt;
    QByteArray out = in.mid(pos, static_cast<qsizetype>(*len));
    pos += static_cast<qsizetype>(*len);
    return out;
}

}  // namespace

QByteArray encode(Kind kind, const QByteArray& payload) {
    QByteArray out;
    if (kind == Kind::Awareness) {
        writeVarUint(out, kMessageAwareness);
    } else {
        writeVarUint(out, kMessageSync);
        writeVarUint(out, kind == Kind::SyncStep1 ? 0 : kind == Kind::SyncStep2 ? 1 : 2);
    }
    writeVarUint(out, static_cast<quint64>(payload.size()));
    out.append(payload);
    return out;
}

std::optional<Message> decode(const QByteArray& message) {
    qsizetype pos = 0;
    const auto type = readVarUint(message, pos);
    if (!type) return std::nullopt;
    if (*type == kMessageAwareness) {
        auto payload = readVarBytes(message, pos);
        if (!payload) return std::nullopt;
        return Message{ Kind::Awareness, *payload };
    }
    if (*type != kMessageSync) return std::nullopt;
    const auto step = readVarUint(message, pos);
    if (!step || *step > 2) return std::nullopt;
    auto payload = readVarBytes(message, pos);
    if (!payload) return std::nullopt;
    const Kind kind = *step == 0 ? Kind::SyncStep1 : *step == 1 ? Kind::SyncStep2 : Kind::Update;
    return Message{ kind, *payload };
}

}  // namespace bld::sync::protocol
