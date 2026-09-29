#include "SyncDoc.h"

#include "DocJsonDetail.h"
#include "WebModelWriter.h"

extern "C" {
#include "libyrs.h"
}

#include <QJsonArray>
#include <QSet>

#include <deque>
#include <iterator>
#include <vector>

namespace bld::sync {

namespace {

// Keys of a layer's Y.Map that hold Y.Arrays: of Y.Maps (items matched by
// id) or of plain values (replaced as a whole).
const char* const kItemArrays[] = { "bricks", "groups", "textCells" };
const char* const kPlainArrays[] = { "areas", "rulerItems" };

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
    // Colours: { kind: 'known', name } | { kind: 'argb', argb }.
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
// keeps many as float), colours by value, a missing key equal to an empty
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

QByteArray takeBinary(char* data, uint32_t len) {
    QByteArray out(data, static_cast<qsizetype>(len));
    ybinary_destroy(data, len);
    return out;
}

}  // namespace

struct SyncDoc::Impl {
    YDoc* doc = nullptr;
    Branch* meta = nullptr;
    Branch* layers = nullptr;
    Branch* layerData = nullptr;
    YTransaction* txn = nullptr;
    Arena arena;
    int ops = 0;  // changes made by the current writeMap

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
            QStringList keptInWantOrder;
            const QSet<QString> curSet(curIds.begin(), curIds.end());
            for (const QString& id : wantIds) if (curSet.contains(id)) keptInWantOrder << id;
            rebuild = keptInWantOrder != curIds;
        }
        if (rebuild) {
            ++ops;
            yarray_remove_range(arr, txn, 0, yarray_len(arr));
            auto& items = arena.itemMaps(want);
            if (!items.empty()) yarray_insert_range(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
            return;
        }
        uint32_t pos = 0;
        qsizetype curIndex = 0;
        for (const auto& v : want) {
            const QJsonObject w = v.toObject();
            if (curIndex < curIds.size() && curIds[curIndex] == w.value(QLatin1String("id")).toString()) {
                Output o(yarray_get(arr, txn, pos));
                if (o && o->tag == Y_MAP) setFields(youtput_read_ymap(o.get()), cur[curIndex].toObject(), w, false);
                ++curIndex;
            } else {
                const YInput item = arena.itemMap(w);
                yarray_insert_range(arr, txn, pos, &item, 1);
                ++ops;
            }
            ++pos;
        }
    }

    void syncLayer(const Branch* map, const QJsonObject& cur, const QJsonObject& want) {
        setFields(map, cur, want, true);
        for (auto it = want.begin(); it != want.end(); ++it) {
            if (!isArrayKey(it.key())) continue;
            const QByteArray key = it.key().toUtf8();
            Output existing(ymap_get(map, txn, key.constData()));
            const bool plainList = it.key() == QLatin1String("areas") || it.key() == QLatin1String("rulerItems");
            if (!existing || existing->tag != Y_ARRAY) {
                const YInput v = arena.arrayOrPlain(it.key(), it.value());
                ymap_insert(map, txn, arena.str(it.key()), &v);
                ++ops;
            } else if (!plainList) {
                syncItems(youtput_read_yarray(existing.get()), cur.value(it.key()).toArray(), it.value().toArray());
            } else if (!same(cur.value(it.key()), it.value())) {
                ++ops;
                const Branch* arr = youtput_read_yarray(existing.get());
                yarray_remove_range(arr, txn, 0, yarray_len(arr));
                auto& items = arena.plains(it.value().toArray());
                if (!items.empty()) yarray_insert_range(arr, txn, 0, items.data(), static_cast<uint32_t>(items.size()));
            }
        }
    }
};

SyncDoc::SyncDoc() : d_(std::make_unique<Impl>()) {
    d_->doc = ydoc_new();
    // Root types must be declared before use; DocJson.cpp reads the same ones.
    d_->meta = ymap(d_->doc, "meta");
    d_->layers = yarray(d_->doc, "layers");
    d_->layerData = ymap(d_->doc, "layerData");
    for (const char* extra : { "labels", "modules", "venue" }) ymap(d_->doc, extra);
}

SyncDoc::~SyncDoc() { ydoc_destroy(d_->doc); }

bool SyncDoc::applyUpdate(const QByteArray& update, QString* error) {
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
    const QJsonObject cur = toJson();
    const QByteArray before = stateVector();

    Impl& d = *d_;
    d.ops = 0;
    d.txn = ydoc_write_transaction(d.doc, 0, nullptr);
    d.setFields(d.meta, cur.value(QLatin1String("meta")).toObject(), want.value(QLatin1String("meta")).toObject(), false);

    const QJsonArray order = want.value(QLatin1String("layers")).toArray();
    if (!same(cur.value(QLatin1String("layers")), order)) {
        ++d.ops;
        yarray_remove_range(d.layers, d.txn, 0, yarray_len(d.layers));
        auto& ids = d.arena.plains(order);
        if (!ids.empty()) yarray_insert_range(d.layers, d.txn, 0, ids.data(), static_cast<uint32_t>(ids.size()));
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

    YTransaction* read = ydoc_read_transaction(d.doc);
    uint32_t len = 0;
    char* diff = ytransaction_state_diff_v1(read, before.constData(), static_cast<uint32_t>(before.size()), &len);
    ytransaction_commit(read);
    QByteArray update = takeBinary(diff, len);
    // Deletions don't move the state vector, so count what was done instead.
    return d.ops == 0 ? QByteArray() : update;
}

}  // namespace bld::sync
