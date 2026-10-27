#include "UpdateGuard.h"

// The encodings follow yrs 0.28's DecoderV1 (updates/decoder.rs), Update::
// decode and decode_block (update.rs), ItemContent::decode (block.rs),
// TypeRef::decode (types/mod.rs), Any::decode (any.rs), IdSet::decode
// (id_set.rs) and StateVector::decode (state_vector.rs). Where yrs reads a
// field differently from Yjs (a JSON content holds one string more than its
// count), the walk follows yrs: the question is what yrs will do with it.

#include <QHash>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace bld::sync::guard {

namespace {

// What an update holds, for updateWork: each client's blocks (clock,
// length) in order, the ids its items are placed after (origin) or before
// (right origin), and its deleted ranges.
struct Id {
    quint64 client = 0;
    quint64 clock = 0;
};
struct Block {
    quint64 clock = 0;
    quint64 length = 0;
};
struct Range {
    quint64 client = 0;
    quint64 clock = 0;
    quint64 length = 0;
};
struct Shape {
    QHash<quint64, std::vector<Block>> blocks;
    std::vector<Id> origins;
    std::vector<Id> rightOrigins;
    std::vector<Range> deleted;
};

// Block info byte (block.rs).
constexpr quint8 kGc = 0;
constexpr quint8 kSkip = 10;
constexpr quint8 kHasOrigin = 0x80;
constexpr quint8 kHasRightOrigin = 0x40;
constexpr quint8 kHasParentSub = 0x20;

class Walk {
public:
    explicit Walk(const QByteArray& bytes, Shape* shape = nullptr)
        : p_(reinterpret_cast<const quint8*>(bytes.constData())), end_(p_ + bytes.size()), shape_(shape) {}

    const QString& error() const { return error_; }

    bool update() {
        quint64 clients = 0;
        if (!count(clients, 3, "clients")) return false;  // each: block count, client id, clock
        for (quint64 c = 0; c < clients; ++c) {
            quint64 blocks = 0;
            if (!count(blocks, 2, "blocks")) return false;  // each: info byte and at least one more
            if (!varUint(client_) || !varUint(clock_)) return false; // client id, first clock
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
    bool text() { return stringBytes(nullptr, nullptr); }

    bool stringBytes(const quint8** out, size_t* outSize) {
        const quint8* data = nullptr;
        size_t size = 0;
        if (!bytes(&data, &size)) return false;
        if (size > 0 && std::memchr(data, 0, size)) return fail(QStringLiteral("a string has a NUL character in it"));
        if (out) *out = data;
        if (outSize) *outSize = size;
        return true;
    }

    // Where the next block of the client being read starts, and its length.
    void place(quint64 length) {
        if (shape_ && length > 0) shape_->blocks[client_].push_back({ clock_, length });
        clock_ += length;
    }

    bool id(std::vector<Id>* into) {
        quint64 client = 0;
        quint64 clock = 0;
        if (!varUint(client) || !varUint(clock)) return false;
        if (shape_ && into) into->push_back({ client, clock });
        return true;
    }

    bool block() {
        quint8 info = 0;
        if (!u8(info)) return false;
        if (info == kGc || info == kSkip) {
            // Yjs never writes an empty one, and yrs 0.28 divides by zero
            // looking up a clock after an empty GC block (block_store.rs).
            quint64 length = 0;
            if (!varUint(length)) return false;
            if (info == kGc) place(length);
            else clock_ += length;
            return length > 0 || fail(QStringLiteral("it holds an empty block"));
        }
        const bool cantCopyParentInfo = (info & (kHasOrigin | kHasRightOrigin)) == 0;
        if ((info & kHasOrigin) && !id(shape_ ? &shape_->origins : nullptr)) return false; // origin id
        if ((info & kHasRightOrigin) && !id(shape_ ? &shape_->rightOrigins : nullptr))
            return false; // right origin id
        if (cantCopyParentInfo) {
            quint64 parentInfo = 0;
            if (!varUint(parentInfo)) return false;
            if (parentInfo == 1 ? !text() : !(skipVarUint() && skipVarUint())) return false;  // root name, or id
            if ((info & kHasParentSub) && !text()) return false;                            // map key
        }
        quint64 length = 0;
        if (!content(info & 0x0f, length)) return false;
        place(length);
        return true;
    }

    // `length`: how many clocks the content takes.
    bool content(quint8 ref, quint64& length) {
        length = 1;
        switch (ref) {
        case 1:  // deleted: a length
            return varUint(length);
        case 2: {  // JSON: yrs reads count + 1 strings
            quint64 n = 0;
            if (!varUint(n)) return false;
            if (n >= static_cast<quint64>(left()))
                return fail(QStringLiteral("it claims %1 JSON values with %2 bytes left").arg(n + 1).arg(left()));
            for (quint64 i = 0; i <= n; ++i)
                if (!text()) return false;
            length = n;
            return true;
        }
        case 3:  // binary
            return bytes();
        case 4: { // string: as many clocks as UTF-16 code units
            const quint8* data = nullptr;
            size_t size = 0;
            if (!stringBytes(&data, &size)) return false;
            length = 0;
            for (size_t i = 0; i < size; ++i) {
                const quint8 c = data[i];
                if ((c & 0xc0) != 0x80) length += (c >= 0xf0) ? 2 : 1;
            }
            return true;
        }
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
            length = n;
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
            quint64 client = 0;
            if (!varUint(client)) return false; // client id
            quint64 ranges = 0;
            if (!count(ranges, 2, "deleted ranges")) return false;  // each: clock, length
            for (quint64 r = 0; r < ranges; ++r) {
                quint64 clock = 0;
                quint64 length = 0;
                if (!varUint(clock) || !varUint(length)) return false;
                if (shape_) shape_->deleted.push_back({ client, clock, length });
            }
        }
        return true;  // yrs ignores anything after the delete set
    }

    const quint8* p_;
    const quint8* end_;
    Shape* shape_;
    quint64 client_ = 0;
    quint64 clock_ = 0;
    QString error_;
};

// The blocks of `client` in `shape`, sorted by clock (an update lists them in order).
const std::vector<Block>* blocksOf(const Shape& shape, quint64 client) {
    const auto it = shape.blocks.constFind(client);
    return it == shape.blocks.constEnd() ? nullptr : &it.value();
}

// Index of the block holding `clock`, or -1.
qsizetype indexOf(const std::vector<Block>& list, quint64 clock) {
    const auto it = std::upper_bound(list.cbegin(), list.cend(), clock,
                                     [](quint64 c, const Block& b) { return c < b.clock; });
    if (it == list.cbegin()) return -1;
    const auto& b = *(it - 1);
    return clock < b.clock + b.length ? static_cast<qsizetype>(it - 1 - list.cbegin()) : -1;
}

// What one deleted range costs on one client's blocks: one step per block
// it covers, and the length of a block it starts or ends inside (split).
qint64 rangeWork(const std::vector<Block>& list, quint64 clock, quint64 length) {
    if (list.empty() || length == 0) return 0;
    const quint64 end = clock + length;
    const quint64 from = std::max(clock, list.front().clock);
    const quint64 to = std::min(end, list.back().clock + list.back().length);
    if (from >= to) return 0;
    const qsizetype i = indexOf(list, from);
    const qsizetype j = indexOf(list, to - 1);
    if (i < 0 || j < 0) return static_cast<qint64>(list.size());
    qint64 work = j - i + 1;
    const Block& a = list[static_cast<size_t>(i)];
    const Block& b = list[static_cast<size_t>(j)];
    if (a.clock < from) work += static_cast<qint64>(a.length);
    if (b.clock + b.length > to) work += static_cast<qint64>(b.length);
    return work;
}

// What splitting the block holding `at` costs, so that `at` ends a piece (`after`) or starts one.
qint64 splitWork(const std::vector<Block>& list, const Id& at, bool after) {
    const qsizetype i = indexOf(list, at.clock);
    if (i < 0) return 0;
    const Block& b = list[static_cast<size_t>(i)];
    const bool splits = after ? at.clock != b.clock + b.length - 1 : at.clock != b.clock;
    return splits ? static_cast<qint64>(b.length) : 0;
}

QString tooBig(qsizetype size) {
    return QStringLiteral("%1 bytes is over the %2 MiB limit").arg(size).arg(kMaxUpdateBytes / kMiB);
}

}  // namespace

QString checkUpdate(const QByteArray& update) {
    if (update.size() > kMaxUpdateBytes) return tooBig(update.size());
    Walk walk(update);
    return walk.update() ? QString() : walk.error();
}

qint64 updateWork(const QByteArray& docState, const QByteArray& update) {
    Shape doc;
    Shape incoming;
    if (!docState.isEmpty() && !Walk(docState, &doc).update()) return 0;
    if (!Walk(update, &incoming).update()) return 0;
    const auto both = [&](quint64 client) {
        return std::array<const std::vector<Block>*, 2>{ blocksOf(doc, client), blocksOf(incoming, client) };
    };
    qint64 work = 0;
    for (const Range& r : incoming.deleted) {
        work += 1;
        for (const auto* list : both(r.client))
            if (list) work += rangeWork(*list, r.clock, r.length);
    }
    for (const auto& [ids, after] :
         { std::pair{ &incoming.origins, true }, std::pair{ &incoming.rightOrigins, false } }) {
        for (const Id& at : *ids) {
            for (const auto* list : both(at.client)) {
                if (!list) continue;
                const qint64 w = splitWork(*list, at, after);
                work += w;
                if (indexOf(*list, at.clock) >= 0) break;
            }
        }
    }
    return work;
}

qint64 updateWorkBound(const QByteArray& stateVector, const QByteArray& update) {
    Shape incoming;
    if (!Walk(update, &incoming).update()) return 0;
    // How many clocks each client may have once it lands: no block is longer.
    QHash<quint64, quint64> clocks;
    {
        const auto* p = reinterpret_cast<const quint8*>(stateVector.constData());
        const auto* end = p + stateVector.size();
        const auto next = [&](quint64& v) {
            v = 0;
            for (int shift = 0; shift < 70 && p < end; shift += 7) {
                const quint8 byte = *p++;
                v |= static_cast<quint64>(byte & 0x7f) << shift;
                if (!(byte & 0x80)) return true;
            }
            return false;
        };
        quint64 n = 0;
        if (next(n))
            for (quint64 i = 0; i < n; ++i) {
                quint64 client = 0;
                quint64 clock = 0;
                if (!next(client) || !next(clock)) break;
                clocks[client] = clock;
            }
    }
    for (auto it = incoming.blocks.cbegin(); it != incoming.blocks.cend(); ++it)
        if (!it.value().empty())
            clocks[it.key()] =
                std::max(clocks.value(it.key()), it.value().back().clock + it.value().back().length);
    qint64 bound = 0;
    for (const Range& r : incoming.deleted) bound += 1 + 3 * static_cast<qint64>(clocks.value(r.client));
    for (const Id& at : incoming.origins) bound += static_cast<qint64>(clocks.value(at.client));
    for (const Id& at : incoming.rightOrigins) bound += static_cast<qint64>(clocks.value(at.client));
    return bound;
}

QString checkStateVector(const QByteArray& stateVector) {
    if (stateVector.size() > kMaxUpdateBytes) return tooBig(stateVector.size());
    Walk walk(stateVector);
    return walk.stateVector() ? QString() : walk.error();
}

}  // namespace bld::sync::guard
