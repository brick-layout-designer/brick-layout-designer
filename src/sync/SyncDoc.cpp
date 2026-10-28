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

#include <array>
#include <deque>
#include <iterator>
#include <algorithm>
#include <unordered_map>
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
    // (a QString key: a QLatin1String one is made into a QString each time)
    static const QString kId = QStringLiteral("id");
    QStringList out;
    out.reserve(items.size());
    for (const auto& v : items) out << v.toObject().value(kId).toString();
    return out;
}

// An array's ids, read once per write: on 5,000 parts each read of them
// took a millisecond or more, and a write read them a dozen times.
struct Ids {
    QStringList list;
    QSet<QString> set;
    bool distinct() const { return set.size() == list.size(); }
    bool unique() const { return distinct() && !set.contains(QString()); }
};

Ids idsIn(const QJsonArray& items) {
    Ids out;
    out.list = idsOf(items);
    out.set = QSet<QString>(out.list.cbegin(), out.list.cend());
    return out;
}

// Whether writing `want` over `cur` (a bricks or rulers array) needs z: it
// already has some, or the kept entries change order, or a new one goes
// anywhere but on top.
bool needsZ(const QJsonArray& cur, const Ids& curIds, const Ids& wantIds) {
    if (hasZOrder(cur)) return true;
    QStringList keptNow, keptWant;
    for (const QString& id : curIds.list)
        if (wantIds.set.contains(id)) keptNow << id;
    qsizetype firstNew = -1, lastKept = -1;
    for (qsizetype i = 0; i < wantIds.list.size(); ++i) {
        if (curIds.set.contains(wantIds.list[i])) {
            keptWant << wantIds.list[i];
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
int wholeReadCount = 0;   // (the same)

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
int SyncDoc::wholeReads() { return wholeReadCount; }

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
    quint64 clientId = 0;  // (0: a new random one; tests set it)

    // ------------------------------------------------- the kept JSON copy
    //
    // toJson used to read the whole document, which on a layout of 5,000
    // parts took about 30 ms, twice for every edit. Now the JSON is kept,
    // and deep observers on the root types say what each transaction
    // changed (local writes, remote updates, undo, redo alike). Right after
    // the transaction, absorb() reads just that again: the keys a map
    // changed, and the entries an array gained, the rest of it kept.
    struct Seg {
        QByteArray key;
        uint32_t index = 0;
        bool isKey = false;
    };
    struct Change {
        std::size_t root = 0;
        std::vector<Seg> path;  // from the root to the type that changed
        const Branch* target = nullptr;
        bool isMap = false;
        std::vector<QByteArray> keys;                     // a map's changed keys
        std::vector<std::pair<uint8_t, uint32_t>> delta;  // an array's retain / delete / insert runs
    };
    struct Watch {
        Impl* d = nullptr;
        std::size_t root = 0;
    };
    std::array<Watch, detail::kRootNames.size()> watches;
    std::array<Branch*, detail::kRootNames.size()> roots{};
    QJsonObject json;  // the document as JSON, while jsonValid
    bool jsonValid = false;
    bool keepJson = true;
    std::vector<Change> changes;  // seen by the observers since the last absorb

    // ------------------------------------------- parts nobody changed
    //
    // Comparing each of 5,000 parts with the document took most of what
    // was left of an edit. A part needs no comparing when neither side
    // changed since the last write left them the same: the editor's part
    // has the fields it had (PartJsonMemo), and nothing touched its entry
    // in the document (the observers say what did).
    PartJsonMemo parts;
    // Since the last write: by sheet and array, the ids of entries changed
    // or added, and the arrays (or with "" the whole sheet) set anew.
    QHash<QString, QHash<QString, QSet<QString>>> touched;
    QHash<QString, QSet<QString>> touchedArrays;
    bool baseline = false;  // the last write is what `touched` counts from
    // A brick sheet's ids now and wanted, from the shape pass to the write.
    QHash<QString, std::pair<Ids, Ids>> brickIds;

    // Whether this write can leave a part's entry in an array as it is.
    struct Unchanged {
        const PartJsonMemo* memo = nullptr;  // null: compare every part
        const QSet<QString>* touched = nullptr;
        bool operator()(const QString& id) const {
            return memo && !memo->changed(id) && !(touched && touched->contains(id));
        }
    };
    Unchanged unchangedIn(const QString& sheet, const QString& array) const {
        if (!baseline || array != QLatin1String("bricks")) return {};
        if (const auto t = touchedArrays.constFind(sheet);
            t != touchedArrays.constEnd() && (t->contains(QString()) || t->contains(array)))
            return {};
        Unchanged out{ &parts, nullptr };
        if (const auto t = touched.constFind(sheet); t != touched.constEnd())
            if (const auto ids = t->constFind(array); ids != t->constEnd()) out.touched = &*ids;
        return out;
    }

    // What a change absorbed touched, for the next write.
    void markTouched(const Change& c, const QStringList& added) {
        if (c.root != detail::kLayerDataRoot) return;
        const auto key = [&](std::size_t i) { return QString::fromUtf8(c.path[i].key); };
        const auto isKey = [&](std::size_t i) { return c.path.size() > i && c.path[i].isKey; };
        if (c.path.empty()) {
            for (const QByteArray& k : c.keys) touchedArrays[QString::fromUtf8(k)].insert(QString());
        } else if (c.path.size() == 1 && isKey(0)) {
            for (const QByteArray& k : c.keys) touchedArrays[key(0)].insert(QString::fromUtf8(k));
        } else if (c.path.size() == 2 && isKey(0) && isKey(1)) {
            QSet<QString>& ids = touched[key(0)][key(1)];
            for (const QString& id : added) ids.insert(id);
        } else if (isKey(0) && isKey(1) && !c.path[2].isKey) {
            // Inside an entry: the entry, by its id now.
            const QJsonArray arr = json.value(QLatin1String("layerData")).toObject().value(key(0)).toObject().value(key(1)).toArray();
            const auto i = static_cast<qsizetype>(c.path[2].index);
            if (i < arr.size()) touched[key(0)][key(1)].insert(arr.at(i).toObject().value(QLatin1String("id")).toString());
            else touchedArrays[key(0)].insert(key(1));
        } else {
            touchedArrays[isKey(0) ? key(0) : QString()].insert(QString());
        }
    }

    static void observed(void* state, uint32_t count, const YEvent* events) {
        const auto* w = static_cast<const Watch*>(state);
        for (uint32_t i = 0; i < count; ++i) w->d->record(w->root, events[i]);
    }

    void record(std::size_t root, const YEvent& e) {
        if (!jsonValid) return;  // read whole next time anyway
        Change c;
        c.root = root;
        YPathSegment* path = nullptr;
        uint32_t depth = 0;
        if (e.tag == Y_MAP) {
            c.isMap = true;
            c.target = ymap_event_target(&e.content.map);
            path = ymap_event_path(&e.content.map, &depth);
            uint32_t n = 0;
            YEventKeyChange* keys = ymap_event_keys(&e.content.map, &n);
            for (uint32_t i = 0; i < n; ++i) c.keys.emplace_back(keys[i].key);
            yevent_keys_destroy(keys, n);
        } else if (e.tag == Y_ARRAY) {
            c.target = yarray_event_target(&e.content.array);
            path = yarray_event_path(&e.content.array, &depth);
            uint32_t n = 0;
            YEventChange* delta = yarray_event_delta(&e.content.array, &n);
            for (uint32_t i = 0; i < n; ++i) c.delta.emplace_back(delta[i].tag, delta[i].len);
            yevent_delta_destroy(delta, n);
        } else {
            return;  // text and XML types read as null, whatever is in them
        }
        for (uint32_t i = 0; i < depth; ++i) {
            Seg seg;
            seg.isKey = path[i].tag == Y_EVENT_PATH_KEY;
            if (seg.isKey) seg.key = path[i].value.key;
            else seg.index = path[i].value.index;
            c.path.push_back(std::move(seg));
        }
        ypath_destroy(path, depth);
        changes.push_back(std::move(c));
    }

    // An array's entries, each read once per absorb.
    using Entries = std::unordered_map<const Branch*, std::vector<Output>>;
    static const std::vector<Output>& entriesIn(Entries& memo, const Branch* arr, YTransaction* rt) {
        auto it = memo.find(arr);
        if (it == memo.end()) it = memo.emplace(arr, entriesOf(arr, rt)).first;
        return it->second;
    }

    // Runs `fn` on the JSON at `path` below `node`, changing it in place:
    // each container on the way is taken out of its parent while it
    // changes, so nothing above it is copied. False when the JSON has no
    // such place.
    template <typename F>
    static bool editAt(QJsonValue& node, const std::vector<Seg>& path, std::size_t i, F& fn) {
        if (i == path.size()) return fn(node);
        const Seg& seg = path[i];
        if (seg.isKey) {
            if (!node.isObject()) return false;
            QJsonObject o = node.toObject();
            node = QJsonValue();
            const QString key = QString::fromUtf8(seg.key);
            const auto it = o.find(key);
            if (it == o.end()) return false;
            QJsonValue child = *it;
            *it = QJsonValue();
            const bool ok = editAt(child, path, i + 1, fn);
            o.insert(key, child);
            node = o;
            return ok;
        }
        if (!node.isArray()) return false;
        QJsonArray a = node.toArray();
        node = QJsonValue();
        if (seg.index >= static_cast<uint32_t>(a.size())) return false;
        const auto at = static_cast<qsizetype>(seg.index);
        QJsonValue child = a.at(at);
        a.replace(at, QJsonValue());
        const bool ok = editAt(child, path, i + 1, fn);
        a.replace(at, child);
        node = a;
        return ok;
    }

    // One change into the kept JSON. False when the JSON and the document
    // don't line up, and the whole document must be read again.
    bool absorbOne(const Change& c, YTransaction* rt, Entries& memo) {
        // Past 64 levels everything reads as null (DocJson.cpp).
        if (c.path.size() > 64) return true;
        // The changed type, found again from its root. When it isn't there
        // (it, or a type above it, was deleted or replaced in the same
        // transaction), what changed in it is out of sight: the change that
        // took it away is read instead.
        const Branch* b = roots[c.root];
        bool isMap = c.root != detail::kLayersRoot;
        for (const Seg& seg : c.path) {
            if (seg.isKey != isMap) return true;
            Output owned;
            const YOutput* v = nullptr;
            if (seg.isKey) {
                owned.reset(ymap_get(b, rt, seg.key.constData()));
                v = owned.get();
            } else {
                const auto& list = entriesIn(memo, b, rt);
                if (seg.index >= list.size()) return true;
                v = list[seg.index].get();
            }
            if (!v || (v->tag != Y_MAP && v->tag != Y_ARRAY)) return true;
            isMap = v->tag == Y_MAP;
            b = isMap ? youtput_read_ymap(v) : youtput_read_yarray(v);
        }
        if (b != c.target || isMap != c.isMap) return true;

        const int depth = static_cast<int>(c.path.size()) + 1;  // of the entries changed
        QStringList added;  // the ids of entries an array gained
        const auto edit = [&](QJsonValue& at) {
            if (c.isMap) {
                if (!at.isObject()) return false;
                QJsonObject o = at.toObject();
                at = QJsonValue();
                for (const QByteArray& key : c.keys) {
                    const Output v(ymap_get(b, rt, key.constData()));
                    if (v) o.insert(QString::fromUtf8(key), detail::valueToJson(v.get(), rt, depth));
                    else o.remove(QString::fromUtf8(key));
                }
                at = o;
                return true;
            }
            if (!at.isArray()) return false;
            const QJsonArray old = at.toArray();
            at = QJsonValue();
            const auto& now = entriesIn(memo, b, rt);
            QJsonArray out;
            qsizetype from = 0;
            std::size_t to = 0;
            for (const auto& [tag, len] : c.delta) {
                if (tag == Y_EVENT_CHANGE_ADD) {
                    if (to + len > now.size()) return false;
                    for (uint32_t k = 0; k < len; ++k) {
                        out.append(detail::valueToJson(now[to++].get(), rt, depth));
                        added << out.last().toObject().value(QLatin1String("id")).toString();
                    }
                    continue;
                }
                if (from + static_cast<qsizetype>(len) > old.size()) return false;
                if (tag == Y_EVENT_CHANGE_RETAIN) {
                    for (uint32_t k = 0; k < len; ++k) out.append(old.at(from++));
                    to += len;
                } else {
                    from += len;  // deleted
                }
            }
            for (; from < old.size(); ++from, ++to) out.append(old.at(from));
            if (to != now.size()) return false;
            at = out;
            return true;
        };
        const QString root = QString::fromLatin1(detail::kRootNames[c.root]);
        QJsonValue node = json.take(root);
        const bool ok = editAt(node, c.path, 0, edit);
        json.insert(root, node);
        if (ok) markTouched(c, added);
        return ok;
    }

    // Whether the document holds updates (or deletions) still waiting for
    // ones before them. yrs then already shows parts of them, but past the
    // state vector, and it says nothing of later changes inside those
    // parts: it takes them for types made in the same transaction. Until
    // the gap is filled, the kept JSON can't follow.
    static bool waiting(const YTransaction* rt) {
        if (YPendingUpdate* p = ytransaction_pending_update(rt)) {
            ypending_update_destroy(p);
            return true;
        }
        if (YIdSet* ds = ytransaction_pending_ds(rt)) {
            ydelete_set_destroy(ds);
            return true;
        }
        return false;
    }

    // Updates that start past what we have of one of their clients, held
    // back until the gap before them is filled (as Yjs keeps them pending).
    // yrs 0.28 shows such blocks at once, past the state vector, and then
    // says nothing of later changes inside them: it takes them for types
    // made in the same transaction.
    std::vector<QByteArray> held;
    qsizetype heldBytes = 0;
    static constexpr qsizetype kMaxHeldBytes = 64 * guard::kMiB;
    // An update applied with a gap anyway (a Skip block in it, or too much
    // held back): from then on the kept JSON is read whole after every
    // transaction, as yrs's events can't be trusted to cover everything.
    bool gapped = false;

    // Whether an update starts past what we have of one of its clients.
    bool leavesGap(const guard::UpdateClocks& starts) const {
        if (starts.skips) return true;
        if (starts.firstClock.isEmpty()) return false;
        const QHash<quint64, quint64> have = clocks();
        for (auto it = starts.firstClock.cbegin(); it != starts.firstClock.cend(); ++it)
            if (it.value() > have.value(it.key(), 0)) return true;
        return false;
    }

    // What applying an update takes (UpdateGuard.h): worked out exactly
    // only when the cheap bound says it might be past the limit.
    qint64 workOf(const QByteArray& update) const {
        const qint64 bound = guard::updateWorkBound(stateVectorBytes(), update);
        if (bound <= guard::kMaxUpdateWork) return bound;
        YTransaction* rt = ydoc_read_transaction(doc);
        uint32_t len = 0;
        char* data = ytransaction_state_diff_v1(rt, nullptr, 0, &len);
        ytransaction_commit(rt);
        return guard::updateWork(takeBinary(data, len), update);
    }

    uint8_t apply(const QByteArray& update) {
        YTransaction* t = ydoc_write_transaction(doc, 0, nullptr);
        const uint8_t rc = ytransaction_apply(t, update.constData(), static_cast<uint32_t>(update.size()));
        ytransaction_commit(t);
        absorb();
        return rc;
    }

    QByteArray stateVectorBytes() const {
        YTransaction* rt = ydoc_read_transaction(doc);
        uint32_t len = 0;
        char* data = ytransaction_state_vector_v1(rt, &len);
        ytransaction_commit(rt);
        return takeBinary(data, len);
    }

    QHash<quint64, quint64> clocks() const {
        QHash<quint64, quint64> out;
        guard::checkStateVector(stateVectorBytes(), &out);
        return out;
    }

    void forgetJson() {
        jsonValid = false;
        json = QJsonObject();
        baseline = false;
    }

    // After every transaction: what it changed into the kept JSON.
    void absorb() {
        std::vector<Change> todo;
        todo.swap(changes);
        if (!jsonValid) return;
        YTransaction* rt = ydoc_read_transaction(doc);
        if (gapped || waiting(rt)) {
            ytransaction_commit(rt);
            forgetJson();
            return;
        }
        if (todo.empty()) {
            ytransaction_commit(rt);
            return;
        }
        bool ok = true;
        {
            Entries memo;
            for (const Change& c : todo) {
                ok = absorbOne(c, rt, memo);
                if (!ok) break;
            }
        }
        ytransaction_commit(rt);
        if (!ok) forgetJson();
    }

    const QJsonObject& currentJson() {
        if (!jsonValid || !keepJson) {
            ++wholeReadCount;
            json = detail::rootsToJson(doc);
            YTransaction* rt = ydoc_read_transaction(doc);
            jsonValid = keepJson && !gapped && !waiting(rt);  // (see waiting, held)
            ytransaction_commit(rt);
            changes.clear();
            baseline = false;  // (what changed before isn't known)
        }
        return json;
    }

    // Before an edit that needs doc shape 2 (DocShape.h): turn a ruler
    // sheet's plain entries into Y.Maps and mark the doc 2, in a step of its
    // own that undo never takes back. Builds that read only shape 1 then
    // refuse the doc rather than misread it.
    void shapePass(const QJsonObject& cur, const QJsonObject& want) {
        const QJsonObject curData = cur.value(QLatin1String("layerData")).toObject();
        const QJsonObject wantData = want.value(QLatin1String("layerData")).toObject();
        txn = ydoc_write_transaction(doc, 0, nullptr);
        bool mark = false;
        const QJsonValue schema = cur.value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion"));
        // In shape 2 already: only ruler sheets still to turn into Y.Maps.
        const bool shaped = schema.isDouble() && schema.toInt() >= kDocShapeInPlace;
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
            } else if (shaped) {
                continue;
            } else if (type == QLatin1String("brick")) {
                const QJsonArray cb = c.value(QLatin1String("bricks")).toArray(),
                                 wb = w.value(QLatin1String("bricks")).toArray();
                // (the parts aren't changed here: the write after uses these ids too)
                auto& ids = brickIds[it.key()] = { idsIn(cb), idsIn(wb) };
                if (needsZ(cb, ids.first, ids.second)) mark = true;
            } else if (type == QLatin1String("area")) {
                const QJsonObject off = c.value(QLatin1String("cellOffset")).toObject();
                const QJsonArray rel =
                    shifted(w.value(QLatin1String("areas")).toArray(), -off.value(QLatin1String("x")).toInt(),
                            -off.value(QLatin1String("y")).toInt());
                if (sheetMove(c.value(QLatin1String("areas")).toArray(), rel)) mark = true;
            }
        }
        if (mark && !shaped) {
            const YInput sv = yinput_float(kDocShapeInPlace);
            ymap_insert(meta, txn, "schemaVersion", &sv);
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

    // Entries not kept, deleted from the end back, a run at a time.
    void removeAllBut(const Branch* arr, const std::vector<bool>& keep) {
        for (auto i = static_cast<qsizetype>(keep.size()) - 1; i >= 0; --i) {
            if (keep[static_cast<size_t>(i)]) continue;
            qsizetype from = i;
            while (from > 0 && !keep[static_cast<size_t>(from - 1)]) --from;
            yarray_remove_range(arr, txn, static_cast<uint32_t>(from), static_cast<uint32_t>(i - from + 1));
            ops += static_cast<int>(i - from + 1);
            i = from;
        }
    }

    // Bricks, groups or text cells: match by id, delete, update in place,
    // insert; rebuild the list if the kept items changed order. `entries`
    // are the array's, read before anything changed.
    void syncItems(const Branch* arr, const QJsonArray& cur, const Ids& curIds, const QJsonArray& want,
                   const Ids& wantIds, const std::vector<Output>& entries, const Unchanged& unchanged) {
        // Duplicate ids: no reliable matching.
        if (!wantIds.distinct() || !curIds.distinct()) {
            ++ops;
            yarray_remove_range(arr, txn, 0, yarray_len(arr));
            auto& items = arena.itemMaps(want);
            if (!items.empty()) insertAt(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
            return;
        }
        // Kept items out of order (Bring to Front, an altitude sort): move
        // only the fewest, the ones off the longest run already in order,
        // so someone else's edit to any other item survives (a moved item
        // is deleted and inserted again, which drops concurrent edits to it).
        QHash<QString, qsizetype> wantPos;
        for (qsizetype i = 0; i < wantIds.list.size(); ++i) wantPos.insert(wantIds.list[i], i);
        QList<qsizetype> wanted;  // indexes into cur of the ones still wanted
        QList<qsizetype> seq;     // and where each is wanted
        for (qsizetype i = 0; i < curIds.list.size(); ++i)
            if (const auto p = wantPos.constFind(curIds.list[i]); p != wantPos.constEnd()) {
                wanted << i;
                seq << *p;
            }
        const QSet<qsizetype> run = longestIncreasingRun(seq);
        std::vector<bool> keep(static_cast<size_t>(cur.size()), false);
        for (const qsizetype k : run) keep[static_cast<size_t>(wanted[k])] = true;
        removeAllBut(arr, keep);
        QList<qsizetype> kept;  // indexes into cur of the entries left, in order
        for (qsizetype i = 0; i < cur.size(); ++i)
            if (keep[static_cast<size_t>(i)]) kept << i;

        uint32_t pos = 0;
        qsizetype k = 0;
        // New items next to each other go in together: one at a time, each
        // insert walked the array again (a paste of 500 into a big layout).
        std::vector<YInput> added;
        uint32_t addAt = 0;
        const auto flush = [&] {
            if (added.empty()) return;
            insertAt(arr, txn, addAt, added.data(), static_cast<uint32_t>(added.size()));
            added.clear();
        };
        for (qsizetype j = 0; j < want.size(); ++j) {
            const QString& id = wantIds.list[j];
            if (k < kept.size() && curIds.list[kept[k]] == id) {
                flush();
                const qsizetype i = kept[k];
                const YOutput* o = i < static_cast<qsizetype>(entries.size()) ? entries[static_cast<size_t>(i)].get() : nullptr;
                if (o && o->tag == Y_MAP && !unchanged(id)) {
                    ++work.entriesCompared;
                    setFields(youtput_read_ymap(o), cur[i].toObject(), want[j].toObject(), false);
                }
                ++k;
            } else {
                if (added.empty()) addAt = pos;
                added.push_back(arena.itemMap(want[j].toObject()));
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

    // Bricks or rulers in doc shape 2: delete, update in place, add new
    // ones on the end, and put them in order by setting z, never by moving
    // an entry (a moved entry is a copy, which loses someone else's delete
    // or edit of it at the same moment). `entries` as for syncItems.
    void syncByZ(const Branch* arr, const QJsonArray& cur, const Ids& curIds, const QJsonArray& want,
                 const Ids& wantIds, const std::vector<Output>& entries, const Unchanged& unchanged) {
        std::vector<bool> keep(static_cast<size_t>(cur.size()), false);
        QList<qsizetype> kept;  // indexes into cur of the entries left, in order
        for (qsizetype i = 0; i < cur.size(); ++i)
            if (wantIds.set.contains(curIds.list[i])) {
                keep[static_cast<size_t>(i)] = true;
                kept << i;
            }
        removeAllBut(arr, keep);
        QHash<QString, qsizetype> at;  // id -> index into cur
        QList<std::optional<double>> zs;
        bool anyZ = false;
        for (const qsizetype i : kept) {
            at.insert(curIds.list[i], i);
            zs << zOf(cur[i]);
            anyZ = anyZ || zs.last().has_value();
        }
        QList<ZEntry> order;
        if (anyZ) {
            for (const qsizetype k : orderByZ(zs)) order.append({ curIds.list[kept[k]], zs[k] });
        } else {
            for (const qsizetype i : kept) order.append({ curIds.list[i], std::nullopt });
        }
        QHash<QString, double> z;
        for (const auto& [id, value] : assignZ(order, wantIds.list)) z.insert(id, value);
        const auto end = static_cast<uint32_t>(kept.size());
        std::vector<YInput> added;
        for (qsizetype j = 0; j < want.size(); ++j) {
            const QString& id = wantIds.list[j];
            const auto zAt = z.constFind(id);
            if (const auto i = at.constFind(id); i != at.constEnd()) {
                const YOutput* o = *i < static_cast<qsizetype>(entries.size()) ? entries[static_cast<size_t>(*i)].get() : nullptr;
                if (!o || o->tag != Y_MAP) continue;
                const Branch* m = youtput_read_ymap(o);
                if (!unchanged(id)) {
                    ++work.entriesCompared;
                    setFields(m, cur[*i].toObject(), want[j].toObject(), false);
                }
                if (zAt != z.constEnd()) {
                    const YInput zv = yinput_float(*zAt);
                    ymap_insert(m, txn, "z", &zv);
                    ++ops;
                }
            } else {
                QJsonObject w = want[j].toObject();
                if (zAt != z.constEnd()) w.insert(QStringLiteral("z"), *zAt);
                added.push_back(arena.itemMap(w));
                ++ops;
            }
        }
        // The new ones on the end together: one by one, each insert walked
        // the array again (a paste of 500 into a big layout, or its first
        // upload, took time growing with the square of the count).
        if (!added.empty()) insertAt(arr, txn, end, added.data(), static_cast<uint32_t>(added.size()));
    }

    void syncLayer(const QString& sheet, const Branch* map, const QJsonObject& cur, const QJsonObject& want) {
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
            } else {
                const Branch* arr = youtput_read_yarray(existing.get());
                const bool bricks = it.key() == QLatin1String("bricks");
                const auto stashed = bricks ? brickIds.constFind(sheet) : brickIds.constEnd();
                const auto [curIds, wantIds] = stashed != brickIds.constEnd()
                                                   ? *stashed
                                                   : std::pair<Ids, Ids>{ idsIn(curList), idsIn(wantList) };
                // Read once, before anything changes, for all of the below.
                const std::vector<Output> entries = entriesOf(arr, txn);
                const bool maps = std::all_of(entries.cbegin(), entries.cend(),
                                              [](const Output& o) { return o && o->tag == Y_MAP; });
                if (isZArray(it.key()) && curIds.unique() && wantIds.unique() && maps
                    && needsZ(curList, curIds, wantIds)) {
                    syncByZ(arr, curList, curIds, wantList, wantIds, entries, unchangedIn(sheet, it.key()));
                } else if (it.key() != QLatin1String("rulerItems") || maps) {
                    syncItems(arr, curList, curIds, wantList, wantIds, entries, unchangedIn(sheet, it.key()));
                } else if (!same(cur.value(it.key()), it.value())) {
                    ++ops;
                    yarray_remove_range(arr, txn, 0, yarray_len(arr));
                    auto& items = arena.plains(it.value().toArray());
                    if (!items.empty()) insertAt(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
                }
            }
        }
    }
};

SyncDoc::SyncDoc() : d_(std::make_unique<Impl>()) { init(); }

SyncDoc::SyncDoc(quint64 clientId) : d_(std::make_unique<Impl>()) {
    d_->clientId = clientId;
    init();
}

void SyncDoc::init() {
    Impl& d = *d_;
    YOptions o = yoptions();  // (a new random client id, unless a test gave one)
    if (d.clientId != 0) o.id = d.clientId;
    // Deleted content is kept, not collected: undo brings back what this
    // desktop deleted or changed while others edit around it, and keeping
    // what was deleted keeps those steps whole. Every live doc is made
    // here, whether loaded from the server or the cache file, or written
    // from a layout.
    o.flags |= Y_SKIP_GC;
    d.doc = ydoc_new_with_options(o);
    // (yoptions hands out its strings; the doc keeps copies)
    ystring_destroy(const_cast<char*>(o.guid));
    ystring_destroy(const_cast<char*>(o.collection_id));
    // Root types must be declared before use; DocJson.cpp reads the same ones.
    for (std::size_t i = 0; i < detail::kRootNames.size(); ++i)
        d.roots[i] = i == detail::kLayersRoot ? yarray(d.doc, detail::kRootNames[i]) : ymap(d.doc, detail::kRootNames[i]);
    d.meta = ymap(d.doc, "meta");
    d.layers = yarray(d.doc, "layers");
    d.layerData = ymap(d.doc, "layerData");
    d.sidecarItems = ymap(d.doc, "sidecarItems");

    // The kept JSON: an empty document's, and what changes from here.
    QJsonObject empty;
    for (std::size_t i = 0; i < detail::kRootNames.size(); ++i)
        empty.insert(QString::fromLatin1(detail::kRootNames[i]),
                     i == detail::kLayersRoot ? QJsonValue(QJsonArray()) : QJsonValue(QJsonObject()));
    d.json = empty;
    d.jsonValid = true;
    static constexpr char kWatch[] = "bld-json";
    for (std::size_t i = 0; i < detail::kRootNames.size(); ++i) {
        d.watches[i] = { &d, i };
        yobserve_deep(d.roots[i], sizeof(kWatch) - 1, kWatch, &d.watches[i], &Impl::observed);
    }

    // No capture window: every writeMap is its own undo step.
    const YUndoManagerOptions opts{ 0 };
    d.undo = yundo_manager(&opts);
    for (const Branch* root : { d.meta, d.layers, d.layerData, d.sidecarItems })
        yundo_manager_add_scope(d.undo, d.doc, root);
    yundo_manager_add_origin(d.undo, sizeof(kLocalOrigin) - 1, kLocalOrigin);
}

// Empty again, with a new client id: what a SyncDoc is when made.
void SyncDoc::reset() {
    const quint64 id = d_->clientId;
    const bool keep = d_->keepJson;
    yundo_manager_destroy(d_->undo);
    ydoc_destroy(d_->doc);
    d_ = std::make_unique<Impl>();
    d_->clientId = id != 0 ? id + 1 : 0;  // (never one the old doc wrote with)
    d_->keepJson = keep;
    init();
}

SyncDoc::~SyncDoc() {
    yundo_manager_destroy(d_->undo);
    ydoc_destroy(d_->doc);
}

bool SyncDoc::applyUpdate(const QByteArray& update, QString* error) {
    // Checked before yrs sees it (UpdateGuard.h); a refused update changes nothing.
    guard::UpdateClocks starts;
    if (const QString refused = guard::checkUpdate(update, &starts); !refused.isEmpty()) {
        if (error) *error = QStringLiteral("refused the document update: %1").arg(refused);
        return false;
    }
    Impl& d = *d_;
    if (const qint64 steps = d.workOf(update); steps > guard::kMaxUpdateWork) {
        if (error)
            *error = QStringLiteral("refused the document update: applying it would take %1 steps, more than %2")
                         .arg(steps)
                         .arg(guard::kMaxUpdateWork);
        return false;
    }
    if (d.leavesGap(starts)) {
        if (!starts.skips && d.heldBytes + update.size() <= Impl::kMaxHeldBytes) {
            d.held.push_back(update);
            d.heldBytes += update.size();
            return true;
        }
        d.gapped = true;
    }
    const uint8_t rc = d.apply(update);
    // Held updates whose gap this filled go in after it, in the order they
    // came (checked for work again: the document has changed since).
    for (bool progress = rc == 0; progress && !d.held.empty();) {
        progress = false;
        for (auto it = d.held.begin(); it != d.held.end(); ++it) {
            guard::UpdateClocks heldStarts;
            guard::checkUpdate(*it, &heldStarts);
            if (d.leavesGap(heldStarts)) continue;
            const QByteArray next = std::move(*it);
            d.heldBytes -= next.size();
            d.held.erase(it);
            if (d.workOf(next) <= guard::kMaxUpdateWork) d.apply(next);
            progress = true;
            break;
        }
    }
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

QJsonObject SyncDoc::toJson() const { return d_->currentJson(); }

QJsonObject SyncDoc::readWhole() const { return detail::rootsToJson(d_->doc); }

void SyncDoc::setKeepJson(bool keep) {
    d_->keepJson = keep;
    d_->baseline = false;
    d_->jsonValid = false;
    d_->json = QJsonObject();
    d_->changes.clear();
}

QByteArray SyncDoc::writeMap(const core::Map& map) {
    Impl& d = *d_;
    const QJsonObject want = docJsonFromMap(map, d.keepJson ? &d.parts : nullptr);
    const QByteArray before = stateVector();

    d.shapePass(d.currentJson(), want);
    d.absorb();
    const QJsonObject cur = d.currentJson();
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
            d.syncLayer(it.key(), youtput_read_ymap(existing.get()), c, w);
        }
    }
    ytransaction_commit(d.txn);
    d.txn = nullptr;
    d.arena = Arena();
    d.absorb();
    // What the next write's `touched` counts from.
    d.touched.clear();
    d.touchedArrays.clear();
    d.brickIds.clear();
    d.baseline = d.jsonValid && d.keepJson;
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
    d.absorb();
    return diffSince(before);
}

QByteArray SyncDoc::undo() {
    const QByteArray before = stateVector();
    const bool undone = yundo_manager_undo(d_->undo) != 0;
    d_->absorb();
    if (!undone) return {};
    return diffSince(before);
}

QByteArray SyncDoc::redo() {
    const QByteArray before = stateVector();
    const bool redone = yundo_manager_redo(d_->undo) != 0;
    d_->absorb();
    if (!redone) return {};
    return diffSince(before);
}

bool SyncDoc::canUndo() const { return yundo_manager_undo_stack_len(d_->undo) > 0; }
bool SyncDoc::canRedo() const { return yundo_manager_redo_stack_len(d_->undo) > 0; }

}  // namespace bld::sync
