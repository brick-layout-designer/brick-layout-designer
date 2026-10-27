#include "SyncDoc.h"

#include "SidecarItems.h"

#include "DocJsonDetail.h"
#include "DocShape.h"
#include "UpdateGuard.h"
#include "WebModelWriter.h"

#include "core/Map.h"

extern "C" {
#include "libyrs.h"
}

#include <QJsonArray>
#include <QHash>
#include <QSet>

#include <deque>
#include <iterator>
#include <algorithm>
#include <vector>

namespace bld::sync {

namespace {

// Indexes into `seq` of a longest strictly increasing run (patience sort).
QSet<qsizetype> longestIncreasingRun(const QList<qsizetype>& seq) {
    QList<qsizetype> tails;            // index into seq of the smallest tail per length
    QList<qsizetype> prev(seq.size(), -1);
    for (qsizetype i = 0; i < seq.size(); ++i) {
        qsizetype lo = 0, hi = tails.size();
        while (lo < hi) {
            const qsizetype mid = (lo + hi) / 2;
            if (seq[tails[mid]] < seq[i]) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0) prev[i] = tails[lo - 1];
        if (lo == tails.size()) tails << i;
        else tails[lo] = i;
    }
    QSet<qsizetype> out;
    for (qsizetype k = tails.isEmpty() ? -1 : tails.last(); k >= 0; k = prev[k]) out.insert(k);
    return out;
}

// Keys of a layer's Y.Map that hold Y.Arrays: of Y.Maps (items matched by
// id) or of plain values (replaced as a whole). Ruler items are plain
// objects in doc shape 1 and Y.Maps in shape 2 (DocShape.h).
const char* const kItemArrays[] = { "bricks", "groups", "textCells", "rulerItems" };
const char* const kPlainArrays[] = { "areas" };

// Item arrays drawn by z in doc shape 2: a reorder sets z, never moves.
bool isZArray(const QString& k) {
    return k == QLatin1String("bricks") || k == QLatin1String("rulerItems");
}

QStringList idsOf(const QJsonArray& items) {
    QStringList out;
    for (const auto& v : items) out << v.toObject().value(QLatin1String("id")).toString();
    return out;
}

bool uniqueIds(const QStringList& ids) {
    return QSet<QString>(ids.begin(), ids.end()).size() == ids.size() && !ids.contains(QString());
}

// Whether writing `want` over `cur` (a bricks or rulers array) needs z: it
// already has some, or the kept entries change order, or a new one goes
// anywhere but on top.
bool needsZ(const QJsonArray& cur, const QJsonArray& want) {
    if (hasZOrder(cur)) return true;
    const QStringList curIds = idsOf(cur), wantIds = idsOf(want);
    const QSet<QString> curSet(curIds.begin(), curIds.end()), wantSet(wantIds.begin(), wantIds.end());
    QStringList keptNow, keptWant;
    for (const QString& id : curIds)
        if (wantSet.contains(id)) keptNow << id;
    qsizetype firstNew = -1, lastKept = -1;
    for (qsizetype i = 0; i < wantIds.size(); ++i) {
        if (curSet.contains(wantIds[i])) {
            keptWant << wantIds[i];
            lastKept = i;
        } else if (firstNew < 0) {
            firstNew = i;
        }
    }
    return keptNow != keptWant || (firstNew >= 0 && firstNew < lastKept);
}

QJsonArray withoutZ(const QJsonArray& items) {
    QJsonArray out;
    for (const auto& v : items) {
        QJsonObject o = v.toObject();
        o.remove(QStringLiteral("z"));
        out.append(o);
    }
    return out;
}

// Cells relative to an offset.
QJsonArray shifted(const QJsonArray& cells, int dx, int dy) {
    QJsonArray out;
    for (const auto& v : cells) {
        QJsonObject c = v.toObject();
        c.insert(QStringLiteral("x"), c.value(QLatin1String("x")).toInt() + dx);
        c.insert(QStringLiteral("y"), c.value(QLatin1String("y")).toInt() + dy);
        out.append(c);
    }
    return out;
}

// When `want` is `cur` with every cell moved by the same whole cells (an
// area sheet moved), that move.
std::optional<std::pair<int, int>> sheetMove(const QJsonArray& cur, const QJsonArray& want) {
    if (cur.isEmpty() || cur.size() != want.size()) return std::nullopt;
    struct C {
        int x, y;
        quint32 color;
    };
    const auto cellsOf = [](const QJsonArray& a) {
        QList<C> out;
        for (const auto& v : a) {
            const QJsonObject o = v.toObject();
            out.append(
                { o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                  static_cast<quint32>(o.value(QLatin1String("color")).toString().toLongLong(nullptr, 16)) });
        }
        std::sort(out.begin(), out.end(),
                  [](const C& a, const C& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        return out;
    };
    const QList<C> a = cellsOf(cur), b = cellsOf(want);
    const int dx = b[0].x - a[0].x, dy = b[0].y - a[0].y;
    if (dx == 0 && dy == 0) return std::nullopt;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (b[i].x - a[i].x != dx || b[i].y - a[i].y != dy || a[i].color != b[i].color) return std::nullopt;
    return std::make_pair(dx, dy);
}

bool isArrayKey(const QString& k) {
    for (const char* a : kItemArrays) if (k == QLatin1String(a)) return true;
    for (const char* a : kPlainArrays) if (k == QLatin1String(a)) return true;
    return false;
}

// --------------------------------------------------------------- comparing

bool same(const QJsonValue& a, const QJsonValue& b);

// Hex ARGB text in any case or padding ("FF00FF00", "ff00ff00").
bool sameHex(const QString& a, const QString& b) {
    bool okA = false, okB = false;
    const qint64 x = a.toLongLong(&okA, 16), y = b.toLongLong(&okB, 16);
    return okA && okB ? x == y : a == b;
}

bool sameObject(const QJsonObject& a, const QJsonObject& b) {
    // Colors: { kind: 'known', name } | { kind: 'argb', argb }.
    if (a.contains(QLatin1String("kind")) && a.value(QLatin1String("kind")) == b.value(QLatin1String("kind"))
        && a.value(QLatin1String("kind")).toString() == QLatin1String("argb"))
        return sameHex(a.value(QLatin1String("argb")).toString(), b.value(QLatin1String("argb")).toString());
    const QStringList ak = a.keys(), bk = b.keys();
    QSet<QString> keys(ak.begin(), ak.end());
    keys.unite(QSet<QString>(bk.begin(), bk.end()));
    for (const QString& k : keys) {
        const QJsonValue x = a.value(k), y = b.value(k);
        if (k == QLatin1String("color") && x.isString() && y.isString()) {  // area cells
            if (!sameHex(x.toString(), y.toString())) return false;
        } else if (!same(x, y)) {
            return false;
        }
    }
    return true;
}

// Equal for the layout: numbers equal once rounded to float (the desktop
// keeps many as float), colors by value, a missing key equal to an empty
// string (optional group fields).
bool same(const QJsonValue& a, const QJsonValue& b) {
    const auto empty = [](const QJsonValue& v) { return v.isUndefined() || v.isNull() || (v.isString() && v.toString().isEmpty()); };
    if (empty(a) && empty(b)) return true;
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        return x == y || static_cast<float>(x) == static_cast<float>(y);
    }
    if (a.type() != b.type()) return false;
    if (a.isObject()) return sameObject(a.toObject(), b.toObject());
    if (a.isArray()) {
        const QJsonArray x = a.toArray(), y = b.toArray();
        if (x.size() != y.size()) return false;
        for (qsizetype i = 0; i < x.size(); ++i) if (!same(x[i], y[i])) return false;
        return true;
    }
    return a == b;
}

// --------------------------------------------------------------- building

// YInput cells point at strings, keys and child arrays; this keeps them
// alive until the insert that reads them.
class Arena {
public:
    const char* str(const QString& s) { return strings_.emplace_back(s.toUtf8()).constData(); }

    YInput plain(const QJsonValue& v) {
        switch (v.type()) {
        case QJsonValue::Bool:   return yinput_bool(v.toBool() ? 1 : 0);
        case QJsonValue::Double: return yinput_float(v.toDouble());
        case QJsonValue::String: return yinput_string(str(v.toString()));
        case QJsonValue::Array: {
            const QJsonArray a = v.toArray();
            auto& vals = values_.emplace_back();
            for (const auto& x : a) vals.push_back(plain(x));
            return yinput_json_array(vals.data(), static_cast<uint32_t>(vals.size()));
        }
        case QJsonValue::Object: {
            const QJsonObject o = v.toObject();
            auto& ks = keys_.emplace_back();
            auto& vals = values_.emplace_back();
            for (auto it = o.begin(); it != o.end(); ++it) {
                ks.push_back(const_cast<char*>(str(it.key())));
                vals.push_back(plain(it.value()));
            }
            return yinput_json_map(ks.data(), vals.data(), static_cast<uint32_t>(vals.size()));
        }
        default:
            return yinput_null();
        }
    }

    // An item (brick, group, text cell) as a Y.Map of plain values.
    YInput itemMap(const QJsonObject& o) {
        auto& ks = keys_.emplace_back();
        auto& vals = values_.emplace_back();
        for (auto it = o.begin(); it != o.end(); ++it) {
            ks.push_back(const_cast<char*>(str(it.key())));
            vals.push_back(plain(it.value()));
        }
        return yinput_ymap(ks.data(), vals.data(), static_cast<uint32_t>(vals.size()));
    }

    std::vector<YInput>& itemMaps(const QJsonArray& items) {
        auto& vals = values_.emplace_back();
        for (const auto& v : items) vals.push_back(itemMap(v.toObject()));
        return vals;
    }

    std::vector<YInput>& plains(const QJsonArray& items) {
        auto& vals = values_.emplace_back();
        for (const auto& v : items) vals.push_back(plain(v));
        return vals;
    }

    // A whole layer as the web lays it out: plain fields, Y.Arrays of item
    // Y.Maps, Y.Arrays of plain values.
    YInput layerMap(const QJsonObject& o) {
        auto& ks = keys_.emplace_back();
        auto& vals = values_.emplace_back();
        for (auto it = o.begin(); it != o.end(); ++it) {
            ks.push_back(const_cast<char*>(str(it.key())));
            vals.push_back(arrayOrPlain(it.key(), it.value()));
        }
        return yinput_ymap(ks.data(), vals.data(), static_cast<uint32_t>(vals.size()));
    }

    YInput arrayOrPlain(const QString& key, const QJsonValue& v) {
        for (const char* a : kItemArrays) {
            if (key == QLatin1String(a)) {
                auto& items = itemMaps(v.toArray());
                return yinput_yarray(items.data(), static_cast<uint32_t>(items.size()));
            }
        }
        for (const char* a : kPlainArrays) {
            if (key == QLatin1String(a)) {
                auto& items = plains(v.toArray());
                return yinput_yarray(items.data(), static_cast<uint32_t>(items.size()));
            }
        }
        return plain(v);
    }

private:
    std::deque<QByteArray> strings_;
    std::deque<std::vector<YInput>> values_;
    std::deque<std::vector<char*>> keys_;
};

struct OutputDeleter { void operator()(YOutput* o) const { if (o) youtput_destroy(o); } };
using Output = std::unique_ptr<YOutput, OutputDeleter>;
struct ArrayIterDeleter { void operator()(YArrayIter* i) const { if (i) yarray_iter_destroy(i); } };

SyncDoc::ArrayWork work;  // (written from the UI thread only)

YOutput* readAt(const Branch* arr, const YTransaction* txn, uint32_t index) {
    ++work.readsByIndex;
    return yarray_get(arr, txn, index);
}

void insertAt(const Branch* arr, YTransaction* txn, uint32_t index, const YInput* items, uint32_t count) {
    ++work.inserts;
    yarray_insert_range(arr, txn, index, items, count);
}

// Every entry of an array, in order, read in one pass. yarray_get walks
// the array from its start each time, so reading the entries one by one by
// index took time growing with the square of their count: a live layout of
// 5,000 parts spent over half a second on each edit doing that.
std::vector<Output> entriesOf(const Branch* arr, YTransaction* txn) {
    std::vector<Output> out;
    out.reserve(yarray_len(arr));
    std::unique_ptr<YArrayIter, ArrayIterDeleter> it(yarray_iter(arr, txn));
    while (YOutput* o = yarray_iter_next(it.get())) out.emplace_back(o);
    return out;
}

QByteArray takeBinary(char* data, uint32_t len) {
    QByteArray out(data, static_cast<qsizetype>(len));
    ybinary_destroy(data, len);
    return out;
}

}  // namespace

SyncDoc::ArrayWork SyncDoc::arrayWork() { return work; }

// Transactions from this desktop's own edits; only these are undoable.
constexpr char kLocalOrigin[] = "bld-local";

struct SyncDoc::Impl {
    YDoc* doc = nullptr;
    YUndoManager* undo = nullptr;
    Branch* meta = nullptr;
    Branch* layers = nullptr;
    Branch* layerData = nullptr;
    Branch* sidecarItems = nullptr;
    YTransaction* txn = nullptr;
    Arena arena;
    int ops = 0;  // changes made by the current writeMap

    // Before an edit that needs doc shape 2 (DocShape.h): turn a ruler
    // sheet's plain entries into Y.Maps and mark the doc 2, in a step of its
    // own that undo never takes back. Builds that read only shape 1 then
    // refuse the doc rather than misread it.
    void shapePass(const QJsonObject& cur, const QJsonObject& want) {
        const QJsonObject curData = cur.value(QLatin1String("layerData")).toObject();
        const QJsonObject wantData = want.value(QLatin1String("layerData")).toObject();
        txn = ydoc_write_transaction(doc, 0, nullptr);
        bool mark = false;
        for (auto it = wantData.begin(); it != wantData.end(); ++it) {
            const QJsonObject w = it.value().toObject(), c = curData.value(it.key()).toObject();
            const QString type = w.value(QLatin1String("type")).toString();
            const QByteArray key = it.key().toUtf8();
            Output layer(ymap_get(layerData, txn, key.constData()));
            if (!layer || layer->tag != Y_MAP || c.value(QLatin1String("type")).toString() != type) {
                // A new ruler sheet is written with Y.Map rulers.
                if (type == QLatin1String("ruler")
                    && !w.value(QLatin1String("rulerItems")).toArray().isEmpty())
                    mark = true;
                continue;
            }
            const Branch* l = youtput_read_ymap(layer.get());
            if (type == QLatin1String("ruler")) {
                const QJsonArray cr = c.value(QLatin1String("rulerItems")).toArray(),
                                 wr = w.value(QLatin1String("rulerItems")).toArray();
                if (same(withoutZ(inZOrder(cr)), wr)) continue;
                mark = true;
                Output items(ymap_get(l, txn, "rulerItems"));
                if (!items || items->tag != Y_ARRAY) continue;
                const Branch* arr = youtput_read_yarray(items.get());
                for (uint32_t i = yarray_len(arr); i-- > 0;) {
                    Output o(readAt(arr, txn, i));
                    if (o && o->tag == Y_MAP) continue;
                    if (static_cast<qsizetype>(i) >= cr.size()) continue;
                    yarray_remove_range(arr, txn, i, 1);
                    const YInput m = arena.itemMap(cr[i].toObject());
                    insertAt(arr, txn, i, &m, 1);
                }
            } else if (type == QLatin1String("brick")) {
                const QJsonArray cb = c.value(QLatin1String("bricks")).toArray(),
                                 wb = w.value(QLatin1String("bricks")).toArray();
                if (needsZ(cb, wb)) mark = true;
            } else if (type == QLatin1String("area")) {
                const QJsonObject off = c.value(QLatin1String("cellOffset")).toObject();
                const QJsonArray rel =
                    shifted(w.value(QLatin1String("areas")).toArray(), -off.value(QLatin1String("x")).toInt(),
                            -off.value(QLatin1String("y")).toInt());
                if (sheetMove(c.value(QLatin1String("areas")).toArray(), rel)) mark = true;
            }
        }
        if (mark) {
            const QJsonValue v =
                cur.value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion"));
            if (!v.isDouble() || v.toInt() < kDocShapeInPlace) {
                const YInput sv = yinput_float(kDocShapeInPlace);
                ymap_insert(meta, txn, "schemaVersion", &sv);
            }
        }
        ytransaction_commit(txn);
        txn = nullptr;
        arena = Arena();
    }

    // Set changed fields of an item or layer map in place.
    void setFields(const Branch* map, const QJsonObject& cur, const QJsonObject& want, bool skipArrays) {
        for (auto it = want.begin(); it != want.end(); ++it) {
            if (skipArrays && isArrayKey(it.key())) continue;
            if (same(cur.value(it.key()), it.value())) continue;
            const YInput v = arena.plain(it.value());
            ymap_insert(map, txn, arena.str(it.key()), &v);
            ++ops;
        }
    }

    // Bricks, groups or text cells: match by id, delete, update in place,
    // insert; rebuild the list if the kept items changed order.
    void syncItems(const Branch* arr, QJsonArray cur, const QJsonArray& want) {
        QStringList wantIds;
        for (const auto& v : want) wantIds << v.toObject().value(QLatin1String("id")).toString();
        const QSet<QString> wantSet(wantIds.begin(), wantIds.end());

        bool rebuild = wantSet.size() != wantIds.size();  // duplicate ids: no reliable matching
        QStringList curIds;
        for (const auto& v : cur) curIds << v.toObject().value(QLatin1String("id")).toString();
        if (QSet<QString>(curIds.begin(), curIds.end()).size() != curIds.size()) rebuild = true;

        if (!rebuild) {
            for (qsizetype i = cur.size() - 1; i >= 0; --i) {
                if (wantSet.contains(curIds[i])) continue;
                yarray_remove_range(arr, txn, static_cast<uint32_t>(i), 1);
                ++ops;
                cur.removeAt(i);
                curIds.removeAt(i);
            }
            // Kept items out of order (Bring to Front, an altitude sort): move
            // only the fewest, the ones off the longest run already in order,
            // so someone else's edit to any other item survives (a moved
            // item is deleted and inserted again, which drops concurrent
            // edits to it).
            QHash<QString, qsizetype> wantPos;
            for (qsizetype i = 0; i < wantIds.size(); ++i) wantPos.insert(wantIds[i], i);
            QList<qsizetype> seq;
            for (const QString& id : curIds) seq << wantPos.value(id);
            const QSet<qsizetype> keep = longestIncreasingRun(seq);
            for (qsizetype i = curIds.size() - 1; i >= 0; --i) {
                if (keep.contains(i)) continue;
                yarray_remove_range(arr, txn, static_cast<uint32_t>(i), 1);
                ++ops;
                cur.removeAt(i);
                curIds.removeAt(i);
            }
        }
        if (rebuild) {
            ++ops;
            yarray_remove_range(arr, txn, 0, yarray_len(arr));
            auto& items = arena.itemMaps(want);
            if (!items.empty()) insertAt(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
            return;
        }
        uint32_t pos = 0;
        qsizetype curIndex = 0;
        // The kept entries, in order (the inserts below go between them).
        const std::vector<Output> entries = entriesOf(arr, txn);
        // New items next to each other go in together: one at a time, each
        // insert walked the array again (a paste of 500 into a big layout).
        std::vector<YInput> run;
        uint32_t runAt = 0;
        const auto flush = [&] {
            if (run.empty()) return;
            insertAt(arr, txn, runAt, run.data(), static_cast<uint32_t>(run.size()));
            run.clear();
        };
        for (const auto& v : want) {
            const QJsonObject w = v.toObject();
            if (curIndex < curIds.size() && curIds[curIndex] == w.value(QLatin1String("id")).toString()) {
                flush();
                const YOutput* o = curIndex < static_cast<qsizetype>(entries.size()) ? entries[curIndex].get() : nullptr;
                if (o && o->tag == Y_MAP) setFields(youtput_read_ymap(o), cur[curIndex].toObject(), w, false);
                ++curIndex;
            } else {
                if (run.empty()) runAt = pos;
                run.push_back(arena.itemMap(w));
                ++ops;
            }
            ++pos;
        }
        flush();
    }

    // An area sheet: cells are kept relative to its cellOffset (doc shape
    // 2). Moving the whole sheet sets the offset; any other change writes
    // the cells (relative) as before.
    void syncArea(const Branch* layer, const Branch* arr, const QJsonObject& curLayer, const QJsonArray& cur,
                  const QJsonArray& want) {
        const QJsonObject off = curLayer.value(QLatin1String("cellOffset")).toObject();
        const int ox = off.value(QLatin1String("x")).toInt(), oy = off.value(QLatin1String("y")).toInt();
        const QJsonArray rel = shifted(want, -ox, -oy);
        if (same(cur, rel)) return;
        if (const auto move = sheetMove(cur, rel)) {
            const QJsonValue next = QJsonObject{ { QStringLiteral("x"), ox + move->first },
                                                 { QStringLiteral("y"), oy + move->second } };
            const YInput v = arena.plain(next);
            ymap_insert(layer, txn, "cellOffset", &v);
            ++ops;
            return;
        }
        ++ops;
        yarray_remove_range(arr, txn, 0, yarray_len(arr));
        auto& items = arena.plains(rel);
        if (!items.empty())
            insertAt(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
    }

    // A ruler there twice (two people turned the sheet into Y.Maps at the
    // same moment): remove the later copies. Returns the list without them.
    QJsonArray dropRepeats(const Branch* arr, const QJsonArray& cur) {
        const QStringList ids = idsOf(cur);
        QSet<QString> seen;
        QList<qsizetype> repeats;
        for (qsizetype i = 0; i < ids.size(); ++i) {
            if (!ids[i].isEmpty() && seen.contains(ids[i])) repeats << i;
            seen.insert(ids[i]);
        }
        if (repeats.isEmpty()) return cur;
        QJsonArray out = cur;
        for (qsizetype k = repeats.size() - 1; k >= 0; --k) {
            yarray_remove_range(arr, txn, static_cast<uint32_t>(repeats[k]), 1);
            out.removeAt(repeats[k]);
            ++ops;
        }
        return out;
    }

    // Whether every entry of the array is a Y.Map.
    bool allMaps(const Branch* arr) {
        const std::vector<Output> entries = entriesOf(arr, txn);
        return std::all_of(entries.cbegin(), entries.cend(), [](const Output& o) { return o && o->tag == Y_MAP; });
    }

    // Bricks or rulers in doc shape 2: delete, update in place, add new
    // ones on the end, and put them in order by setting z, never by moving
    // an entry (a moved entry is a copy, which loses someone else's delete
    // or edit of it at the same moment).
    void syncByZ(const Branch* arr, const QJsonArray& cur, const QJsonArray& want) {
        const QStringList curIds = idsOf(cur);
        const QSet<QString> wantSet = [&] {
            const QStringList w = idsOf(want);
            return QSet<QString>(w.begin(), w.end());
        }();
        QJsonArray kept;
        for (qsizetype i = cur.size() - 1; i >= 0; --i) {
            if (wantSet.contains(curIds[i])) continue;
            yarray_remove_range(arr, txn, static_cast<uint32_t>(i), 1);
            ++ops;
        }
        for (qsizetype i = 0; i < cur.size(); ++i)
            if (wantSet.contains(curIds[i])) kept.append(cur[i]);
        QHash<QString, qsizetype> at; // id -> index in the array now
        const QStringList keptIds = idsOf(kept);
        for (qsizetype i = 0; i < keptIds.size(); ++i) at.insert(keptIds[i], i);
        QList<ZEntry> order;
        for (const auto& v : inZOrder(kept))
            order.append({ v.toObject().value(QLatin1String("id")).toString(), zOf(v) });
        QHash<QString, double> z;
        for (const auto& [id, value] : assignZ(order, idsOf(want))) z.insert(id, value);
        const auto end = static_cast<uint32_t>(kept.size());
        std::vector<YInput> added;
        // The kept entries (new ones only go on the end).
        const std::vector<Output> entries = entriesOf(arr, txn);
        for (const auto& v : want) {
            QJsonObject w = v.toObject();
            const QString id = w.value(QLatin1String("id")).toString();
            if (at.contains(id)) {
                const qsizetype i = at.value(id);
                const YOutput* o = i < static_cast<qsizetype>(entries.size()) ? entries[i].get() : nullptr;
                if (!o || o->tag != Y_MAP) continue;
                const Branch* m = youtput_read_ymap(o);
                setFields(m, kept[at.value(id)].toObject(), w, false);
                if (z.contains(id)) {
                    const YInput zv = yinput_float(z.value(id));
                    ymap_insert(m, txn, "z", &zv);
                    ++ops;
                }
            } else {
                if (z.contains(id)) w.insert(QStringLiteral("z"), z.value(id));
                added.push_back(arena.itemMap(w));
                ++ops;
            }
        }
        // The new ones on the end together: one by one, each insert walked
        // the array again (a paste of 500 into a big layout, or its first
        // upload, took time growing with the square of the count).
        if (!added.empty()) insertAt(arr, txn, end, added.data(), static_cast<uint32_t>(added.size()));
    }

    void syncLayer(const Branch* map, const QJsonObject& cur, const QJsonObject& want) {
        setFields(map, cur, want, true);
        for (auto it = want.begin(); it != want.end(); ++it) {
            if (!isArrayKey(it.key())) continue;
            const QByteArray key = it.key().toUtf8();
            Output existing(ymap_get(map, txn, key.constData()));
            QJsonArray curList = cur.value(it.key()).toArray();
            const QJsonArray wantList = it.value().toArray();
            if (existing && existing->tag == Y_ARRAY && it.key() == QLatin1String("rulerItems"))
                curList = dropRepeats(youtput_read_yarray(existing.get()), curList);
            if (!existing || existing->tag != Y_ARRAY) {
                const YInput v = arena.arrayOrPlain(it.key(), it.value());
                ymap_insert(map, txn, arena.str(it.key()), &v);
                ++ops;
            } else if (it.key() == QLatin1String("areas")) {
                syncArea(map, youtput_read_yarray(existing.get()), cur, curList, wantList);
            } else if (isZArray(it.key()) && uniqueIds(idsOf(curList)) && uniqueIds(idsOf(wantList))
                       && allMaps(youtput_read_yarray(existing.get())) && needsZ(curList, wantList)) {
                syncByZ(youtput_read_yarray(existing.get()), curList, wantList);
            } else if (it.key() != QLatin1String("rulerItems")
                       || allMaps(youtput_read_yarray(existing.get()))) {
                syncItems(youtput_read_yarray(existing.get()), curList, wantList);
            } else if (!same(cur.value(it.key()), it.value())) {
                ++ops;
                const Branch* arr = youtput_read_yarray(existing.get());
                yarray_remove_range(arr, txn, 0, yarray_len(arr));
                auto& items = arena.plains(it.value().toArray());
                if (!items.empty()) insertAt(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
            }
        }
    }
};

SyncDoc::SyncDoc() : d_(std::make_unique<Impl>()) { init(); }

void SyncDoc::init() {
    d_->doc = ydoc_new();
    // Root types must be declared before use; DocJson.cpp reads the same ones.
    d_->meta = ymap(d_->doc, "meta");
    d_->layers = yarray(d_->doc, "layers");
    d_->layerData = ymap(d_->doc, "layerData");
    d_->sidecarItems = ymap(d_->doc, "sidecarItems");
    for (const char* extra : { "labels", "modules", "venue" }) ymap(d_->doc, extra);

    // No capture window: every writeMap is its own undo step.
    const YUndoManagerOptions opts{ 0 };
    d_->undo = yundo_manager(&opts);
    for (const Branch* root : { d_->meta, d_->layers, d_->layerData, d_->sidecarItems })
        yundo_manager_add_scope(d_->undo, d_->doc, root);
    yundo_manager_add_origin(d_->undo, sizeof(kLocalOrigin) - 1, kLocalOrigin);
}

// Empty again, with a new client id: what a SyncDoc is when made.
void SyncDoc::reset() {
    yundo_manager_destroy(d_->undo);
    ydoc_destroy(d_->doc);
    d_ = std::make_unique<Impl>();
    init();
}

SyncDoc::~SyncDoc() {
    yundo_manager_destroy(d_->undo);
    ydoc_destroy(d_->doc);
}

bool SyncDoc::applyUpdate(const QByteArray& update, QString* error) {
    // Checked before yrs sees it (UpdateGuard.h); a refused update changes nothing.
    if (const QString refused = guard::checkUpdate(update); !refused.isEmpty()) {
        if (error) *error = QStringLiteral("refused the document update: %1").arg(refused);
        return false;
    }
    YTransaction* txn = ydoc_write_transaction(d_->doc, 0, nullptr);
    const uint8_t rc = ytransaction_apply(txn, update.constData(), static_cast<uint32_t>(update.size()));
    ytransaction_commit(txn);
    if (rc != 0 && error) *error = QStringLiteral("could not apply the document update (yrs error %1)").arg(rc);
    return rc == 0;
}

QByteArray SyncDoc::encodeState() const {
    YTransaction* txn = ydoc_read_transaction(d_->doc);
    uint32_t len = 0;
    char* data = ytransaction_state_diff_v1(txn, nullptr, 0, &len);
    ytransaction_commit(txn);
    return takeBinary(data, len);
}

QByteArray SyncDoc::diffSince(const QByteArray& sv) const {
    if (!guard::checkStateVector(sv).isEmpty()) return encodeState();
    YTransaction* txn = ydoc_read_transaction(d_->doc);
    uint32_t len = 0;
    char* data = ytransaction_state_diff_v1(txn, sv.constData(), static_cast<uint32_t>(sv.size()), &len);
    ytransaction_commit(txn);
    if (!data) return encodeState();
    return takeBinary(data, len);
}

QByteArray SyncDoc::stateVector() const {
    YTransaction* txn = ydoc_read_transaction(d_->doc);
    uint32_t len = 0;
    char* data = ytransaction_state_vector_v1(txn, &len);
    ytransaction_commit(txn);
    return takeBinary(data, len);
}

QJsonObject SyncDoc::toJson() const { return detail::rootsToJson(d_->doc); }

QByteArray SyncDoc::writeMap(const core::Map& map) {
    const QJsonObject want = docJsonFromMap(map);
    const QByteArray before = stateVector();

    Impl& d = *d_;
    d.shapePass(toJson(), want);
    const QJsonObject cur = toJson();
    d.ops = 0;
    d.txn = ydoc_write_transaction(d.doc, sizeof(kLocalOrigin) - 1, kLocalOrigin);
    const QJsonObject curMeta = cur.value(QLatin1String("meta")).toObject();
    QJsonObject wantMeta = want.value(QLatin1String("meta")).toObject();
    const QJsonObject cache =
        mergeSidecarCache(curMeta.value(QLatin1String("cache")).toObject(), map.sidecar);
    if (curMeta.contains(QLatin1String("cache")) || !cache.isEmpty())
        wantMeta.insert(QStringLiteral("cache"), cache);
    d.setFields(d.meta, curMeta, wantMeta, false);
    // Modules and labels one per key too, as the web keeps them, so edits
    // made here and on the web at the same time merge (SidecarItems.h).
    if (!cache.isEmpty()) {
        const QJsonObject curItems = cur.value(QLatin1String("sidecarItems")).toObject();
        const QJsonObject wantItems = sidecarItemsFor(cache);
        for (auto it = curItems.begin(); it != curItems.end(); ++it)
            if ((it.key().startsWith(QLatin1String("m:")) || it.key().startsWith(QLatin1String("l:")))
                && !wantItems.contains(it.key())) {
                ymap_remove(d.sidecarItems, d.txn, d.arena.str(it.key()));
                ++d.ops;
            }
        d.setFields(d.sidecarItems, curItems, wantItems, false);
    }

    const QJsonArray order = want.value(QLatin1String("layers")).toArray();
    if (!same(cur.value(QLatin1String("layers")), order)) {
        ++d.ops;
        yarray_remove_range(d.layers, d.txn, 0, yarray_len(d.layers));
        auto& ids = d.arena.plains(order);
        if (!ids.empty()) insertAt(d.layers, d.txn, 0, ids.data(), static_cast<uint32_t>(ids.size()));
    }

    const QJsonObject curData = cur.value(QLatin1String("layerData")).toObject();
    const QJsonObject wantData = want.value(QLatin1String("layerData")).toObject();
    for (auto it = curData.begin(); it != curData.end(); ++it)
        if (!wantData.contains(it.key())) {
            ymap_remove(d.layerData, d.txn, d.arena.str(it.key()));
            ++d.ops;
        }
    for (auto it = wantData.begin(); it != wantData.end(); ++it) {
        const QJsonObject w = it.value().toObject();
        const QJsonObject c = curData.value(it.key()).toObject();
        const QByteArray key = it.key().toUtf8();
        Output existing(ymap_get(d.layerData, d.txn, key.constData()));
        if (!existing || existing->tag != Y_MAP || c.value(QLatin1String("type")) != w.value(QLatin1String("type"))) {
            const YInput v = d.arena.layerMap(w);
            ymap_insert(d.layerData, d.txn, d.arena.str(it.key()), &v);
            ++d.ops;
        } else {
            d.syncLayer(youtput_read_ymap(existing.get()), c, w);
        }
    }
    ytransaction_commit(d.txn);
    d.txn = nullptr;
    d.arena = Arena();
    yundo_manager_stop(d.undo);  // seal this write as one step

    YTransaction* read = ydoc_read_transaction(d.doc);
    uint32_t len = 0;
    char* diff = ytransaction_state_diff_v1(read, before.constData(), static_cast<uint32_t>(before.size()), &len);
    ytransaction_commit(read);
    QByteArray update = takeBinary(diff, len);
    // Deletions don't move the state vector, so count what was done instead.
    return d.ops == 0 ? QByteArray() : update;
}

QByteArray SyncDoc::setMeta(const QString& key, const QJsonValue& value) {
    const QByteArray before = stateVector();
    Impl& d = *d_;
    d.txn = ydoc_write_transaction(d.doc, 0, nullptr);
    const YInput v = d.arena.plain(value);
    ymap_insert(d.meta, d.txn, d.arena.str(key), &v);
    ytransaction_commit(d.txn);
    d.txn = nullptr;
    d.arena = Arena();
    return diffSince(before);
}

QByteArray SyncDoc::undo() {
    const QByteArray before = stateVector();
    if (!yundo_manager_undo(d_->undo)) return {};
    return diffSince(before);
}

QByteArray SyncDoc::redo() {
    const QByteArray before = stateVector();
    if (!yundo_manager_redo(d_->undo)) return {};
    return diffSince(before);
}

bool SyncDoc::canUndo() const { return yundo_manager_undo_stack_len(d_->undo) > 0; }
bool SyncDoc::canRedo() const { return yundo_manager_redo_stack_len(d_->undo) > 0; }

}  // namespace bld::sync
