#include "Awareness.h"

#include <QJsonDocument>

namespace bld::sync::awareness {

namespace {

void writeVarUint(QByteArray& out, quint64 v) {
    while (v > 0x7f) {
        out.append(static_cast<char>(0x80 | (v & 0x7f)));
        v >>= 7;
    }
    out.append(static_cast<char>(v));
}

struct Reader {
    const QByteArray& data;
    qsizetype pos = 0;
    std::optional<quint64> varUint() {
        quint64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (pos >= data.size()) return std::nullopt;
            const auto b = static_cast<quint8>(data[pos++]);
            v |= static_cast<quint64>(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
        }
        return std::nullopt;
    }
    std::optional<QByteArray> varString() {
        const auto len = varUint();
        if (!len || *len > static_cast<quint64>(data.size() - pos)) return std::nullopt;
        QByteArray s = data.mid(pos, static_cast<qsizetype>(*len));
        pos += static_cast<qsizetype>(*len);
        return s;
    }
};

}  // namespace

QByteArray encode(const QList<Entry>& entries) {
    QByteArray out;
    writeVarUint(out, static_cast<quint64>(entries.size()));
    for (const auto& e : entries) {
        writeVarUint(out, e.clientId);
        writeVarUint(out, e.clock);
        const QByteArray json = e.state ? QJsonDocument(*e.state).toJson(QJsonDocument::Compact) : QByteArrayLiteral("null");
        writeVarUint(out, static_cast<quint64>(json.size()));
        out.append(json);
    }
    return out;
}

std::optional<QList<Entry>> decode(const QByteArray& update) {
    Reader r{ update };
    const auto count = r.varUint();
    if (!count || *count > static_cast<quint64>(update.size())) return std::nullopt;
    QList<Entry> out;
    for (quint64 i = 0; i < *count; ++i) {
        const auto id = r.varUint(), clock = r.varUint();
        const auto json = r.varString();
        if (!id || !clock || !json || *id > 0xffffffffULL || *clock > 0xffffffffULL) return std::nullopt;
        Entry e{ static_cast<quint32>(*id), static_cast<quint32>(*clock), std::nullopt };
        const QJsonDocument doc = QJsonDocument::fromJson(*json);
        if (doc.isObject()) e.state = doc.object();
        out.append(e);
    }
    return out;
}

bool Peers::apply(const QByteArray& update) {
    const auto entries = decode(update);
    if (!entries) return false;
    bool changed = false;
    for (const auto& e : *entries) {
        if (e.clientId == ownId_) continue;
        const auto known = clocks_.find(e.clientId);
        // y-protocols: a newer clock wins; the same clock with a null state is a removal.
        if (known != clocks_.end() && (*known > e.clock || (*known == e.clock && e.state))) continue;
        clocks_[e.clientId] = e.clock;
        if (e.state) states_[e.clientId] = *e.state;
        else states_.remove(e.clientId);
        changed = true;
    }
    return changed;
}

}  // namespace bld::sync::awareness
