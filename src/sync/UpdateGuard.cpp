#include "UpdateGuard.h"

// The encodings follow yrs 0.28's DecoderV1 (updates/decoder.rs), Update::
// decode and decode_block (update.rs), ItemContent::decode (block.rs),
// TypeRef::decode (types/mod.rs), Any::decode (any.rs), IdSet::decode
// (id_set.rs) and StateVector::decode (state_vector.rs). Where yrs reads a
// field differently from Yjs (a JSON content holds one string more than its
// count), the walk follows yrs: the question is what yrs will do with it.

#include <cstring>

namespace bld::sync::guard {

namespace {

// Block info byte (block.rs).
constexpr quint8 kGc = 0;
constexpr quint8 kSkip = 10;
constexpr quint8 kHasOrigin = 0x80;
constexpr quint8 kHasRightOrigin = 0x40;
constexpr quint8 kHasParentSub = 0x20;

class Walk {
public:
    explicit Walk(const QByteArray& bytes)
        : p_(reinterpret_cast<const quint8*>(bytes.constData())), end_(p_ + bytes.size()) {}

    const QString& error() const { return error_; }

    bool update() {
        quint64 clients = 0;
        if (!count(clients, 3, "clients")) return false;  // each: block count, client id, clock
        for (quint64 c = 0; c < clients; ++c) {
            quint64 blocks = 0;
            if (!count(blocks, 2, "blocks")) return false;  // each: info byte and at least one more
            if (!skipVarUint() || !skipVarUint()) return false;  // client id, first clock
            for (quint64 b = 0; b < blocks; ++b)
                if (!block()) return false;
        }
        return deleteSet();
    }

    bool stateVector() {
        quint64 clients = 0;
        if (!count(clients, 2, "clients")) return false;  // each: client id, clock
        for (quint64 c = 0; c < clients; ++c)
            if (!skipVarUint() || !skipVarUint()) return false;
        return true;
    }

private:
    qsizetype left() const { return end_ - p_; }

    bool fail(const QString& why) {
        if (error_.isEmpty()) error_ = why;
        return false;
    }

    bool truncated() { return fail(QStringLiteral("it ends early")); }

    bool u8(quint8& v) {
        if (p_ == end_) return truncated();
        v = *p_++;
        return true;
    }

    bool skip(quint64 n) {
        if (n > static_cast<quint64>(left())) return truncated();
        p_ += n;
        return true;
    }

    // lib0 varUint. Ten bytes hold 64 bits; Yjs never writes more.
    bool varUint(quint64& v) {
        v = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            quint8 byte = 0;
            if (!u8(byte)) return false;
            v |= static_cast<quint64>(byte & 0x7f) << shift;
            if (!(byte & 0x80)) return true;
        }
        return fail(QStringLiteral("a number runs on past 64 bits"));
    }

    bool skipVarUint() {
        quint64 v = 0;
        return varUint(v);
    }

    // A count of things that each take at least `minBytes`: never more than
    // the bytes left could hold, which is what keeps yrs from reserving room
    // for billions of them.
    bool count(quint64& n, int minBytes, const char* what) {
        if (!varUint(n)) return false;
        if (n > static_cast<quint64>(left() / minBytes))
            return fail(QStringLiteral("it claims %1 %2 with %3 bytes left").arg(n).arg(QLatin1String(what)).arg(left()));
        return true;
    }

    // lib0 varUint8Array: a length, then that many bytes.
    bool bytes(const quint8** data = nullptr, size_t* size = nullptr) {
        quint64 len = 0;
        if (!varUint(len)) return false;
        if (len > static_cast<quint64>(left()))
            return fail(QStringLiteral("a %1-byte string with %2 bytes left").arg(len).arg(left()));
        if (data) *data = p_;
        if (size) *size = static_cast<size_t>(len);
        p_ += len;
        return true;
    }

    // lib0 varString. yffi hands strings to C as NUL-terminated and panics
    // (aborting the app) on one with a NUL inside, so those are refused.
    bool text() {
        const quint8* data = nullptr;
        size_t size = 0;
        if (!bytes(&data, &size)) return false;
        if (size > 0 && std::memchr(data, 0, size)) return fail(QStringLiteral("a string has a NUL character in it"));
        return true;
    }

    bool block() {
        quint8 info = 0;
        if (!u8(info)) return false;
        if (info == kGc || info == kSkip) return skipVarUint();  // length
        const bool cantCopyParentInfo = (info & (kHasOrigin | kHasRightOrigin)) == 0;
        if ((info & kHasOrigin) && !(skipVarUint() && skipVarUint())) return false;       // origin id
        if ((info & kHasRightOrigin) && !(skipVarUint() && skipVarUint())) return false;  // right origin id
        if (cantCopyParentInfo) {
            quint64 parentInfo = 0;
            if (!varUint(parentInfo)) return false;
            if (parentInfo == 1 ? !text() : !(skipVarUint() && skipVarUint())) return false;  // root name, or id
            if ((info & kHasParentSub) && !text()) return false;                            // map key
        }
        return content(info & 0x0f);
    }

    bool content(quint8 ref) {
        switch (ref) {
        case 1:  // deleted: a length
            return skipVarUint();
        case 2: {  // JSON: yrs reads count + 1 strings
            quint64 n = 0;
            if (!varUint(n)) return false;
            if (n >= static_cast<quint64>(left()))
                return fail(QStringLiteral("it claims %1 JSON values with %2 bytes left").arg(n + 1).arg(left()));
            for (quint64 i = 0; i <= n; ++i)
                if (!text()) return false;
            return true;
        }
        case 3:  // binary
            return bytes();
        case 4:  // string
        case 5:  // embed (JSON text)
            return text();
        case 6:  // format: key, JSON text
            return text() && text();
        case 7:  // a nested type
            return typeRef();
        case 8: {  // values
            quint64 n = 0;
            if (!count(n, 1, "values")) return false;
            for (quint64 i = 0; i < n; ++i)
                if (!any(0)) return false;
            return true;
        }
        case 9:  // a sub-document: guid, options
            return text() && any(0);
        default:
            return fail(QStringLiteral("unknown content kind %1").arg(ref));
        }
    }

    bool typeRef() {
        quint8 ref = 0;
        if (!u8(ref)) return false;
        switch (ref) {
        case 0: case 1: case 2: case 4: case 5: case 6: case 9: case 15:
            return true;  // array, map, text, XML fragment / hook / text, sub-document, undefined
        case 3:
            return text();  // XML element: its name
        case 7:
            return fail(QStringLiteral("it holds a weak link, which layouts never use"));
        default:
            return fail(QStringLiteral("unknown type %1").arg(ref));
        }
    }

    // lib0 Any.
    bool any(int depth) {
        if (depth > kMaxNesting) return fail(QStringLiteral("values nest more than %1 deep").arg(kMaxNesting));
        quint8 tag = 0;
        if (!u8(tag)) return false;
        switch (tag) {
        case 127: case 126: case 121: case 120:  // undefined, null, false, true
            return true;
        case 125:  // varInt: the same continuation bit as varUint
            return skipVarUint();
        case 124:  // float32
            return skip(4);
        case 123:  // float64
        case 122:  // bigint64
            return skip(8);
        case 119:  // string
            return text();
        case 116:  // bytes
            return bytes();
        case 118: {  // object: key, value pairs
            quint64 n = 0;
            if (!count(n, 2, "object entries")) return false;
            for (quint64 i = 0; i < n; ++i)
                if (!text() || !any(depth + 1)) return false;
            return true;
        }
        case 117: {  // array
            quint64 n = 0;
            if (!count(n, 1, "array items")) return false;
            for (quint64 i = 0; i < n; ++i)
                if (!any(depth + 1)) return false;
            return true;
        }
        default:
            return fail(QStringLiteral("unknown value kind %1").arg(tag));
        }
    }

    bool deleteSet() {
        quint64 clients = 0;
        if (!count(clients, 2, "deleted-range clients")) return false;  // each: client id, range count
        for (quint64 c = 0; c < clients; ++c) {
            if (!skipVarUint()) return false;  // client id
            quint64 ranges = 0;
            if (!count(ranges, 2, "deleted ranges")) return false;  // each: clock, length
            for (quint64 r = 0; r < ranges; ++r)
                if (!skipVarUint() || !skipVarUint()) return false;
        }
        return true;  // yrs ignores anything after the delete set
    }

    const quint8* p_;
    const quint8* end_;
    QString error_;
};

QString tooBig(qsizetype size) {
    return QStringLiteral("%1 bytes is over the %2 MiB limit").arg(size).arg(kMaxUpdateBytes / (1024 * 1024));
}

}  // namespace

QString checkUpdate(const QByteArray& update) {
    if (update.size() > kMaxUpdateBytes) return tooBig(update.size());
    Walk walk(update);
    return walk.update() ? QString() : walk.error();
}

QString checkStateVector(const QByteArray& stateVector) {
    if (stateVector.size() > kMaxUpdateBytes) return tooBig(stateVector.size());
    Walk walk(stateVector);
    return walk.stateVector() ? QString() : walk.error();
}

}  // namespace bld::sync::guard
