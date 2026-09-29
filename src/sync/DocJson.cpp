#include "DocJson.h"

#include "DocJsonDetail.h"
#include "SyncDoc.h"

extern "C" {
#include "libyrs.h"
}

#include <QJsonArray>

#include <iterator>
#include <memory>

namespace bld::sync {

namespace {

struct OutputDeleter { void operator()(YOutput* o) const { if (o) youtput_destroy(o); } };
struct MapIterDeleter { void operator()(YMapIter* i) const { if (i) ymap_iter_destroy(i); } };
struct EntryDeleter { void operator()(YMapEntry* e) const { if (e) ymap_entry_destroy(e); } };

QJsonValue toJson(const YOutput* o, const YTransaction* txn, int depth);

QJsonObject mapToJson(const Branch* map, const YTransaction* txn, int depth) {
    QJsonObject out;
    std::unique_ptr<YMapIter, MapIterDeleter> it(ymap_iter(map, txn));
    while (std::unique_ptr<YMapEntry, EntryDeleter> e{ ymap_iter_next(it.get()) })
        out.insert(QString::fromUtf8(e->key), toJson(e->value, txn, depth + 1));
    return out;
}

QJsonArray arrayToJson(const Branch* array, const YTransaction* txn, int depth) {
    QJsonArray out;
    const uint32_t n = yarray_len(array);
    for (uint32_t i = 0; i < n; ++i) {
        std::unique_ptr<YOutput, OutputDeleter> v(yarray_get(array, txn, i));
        out.append(toJson(v.get(), txn, depth + 1));
    }
    return out;
}

QJsonValue toJson(const YOutput* o, const YTransaction* txn, int depth) {
    // Damaged documents can nest without end; the web model is 5 deep.
    if (!o || depth > 64) return QJsonValue::Null;
    switch (o->tag) {
    case Y_JSON_BOOL: return QJsonValue(*youtput_read_bool(o) != 0);
    case Y_JSON_NUM:  return QJsonValue(*youtput_read_float(o));
    case Y_JSON_INT:  return QJsonValue(static_cast<qint64>(*youtput_read_long(o)));
    case Y_JSON_STR:  return QJsonValue(QString::fromUtf8(youtput_read_string(o)));
    case Y_JSON_ARR: {
        QJsonArray out;
        const YOutput* items = youtput_read_json_array(o);
        for (uint32_t i = 0; i < o->len; ++i) out.append(toJson(&items[i], txn, depth + 1));
        return out;
    }
    case Y_JSON_MAP: {
        QJsonObject out;
        const YMapEntry* entries = youtput_read_json_map(o);
        for (uint32_t i = 0; i < o->len; ++i)
            out.insert(QString::fromUtf8(entries[i].key), toJson(entries[i].value, txn, depth + 1));
        return out;
    }
    case Y_MAP:   return mapToJson(youtput_read_ymap(o), txn, depth);
    case Y_ARRAY: return arrayToJson(youtput_read_yarray(o), txn, depth);
    default:      return QJsonValue::Null;  // null, undefined, binary, text, xml, docs
    }
}

}  // namespace

namespace detail {

QJsonObject rootsToJson(YDoc* doc) {
    // Declaring an existing root type returns it.
    const char* maps[] = { "meta", "layerData", "labels", "modules", "venue" };
    Branch* rootMaps[std::size(maps)];
    for (size_t i = 0; i < std::size(maps); ++i) rootMaps[i] = ymap(doc, maps[i]);
    Branch* layers = yarray(doc, "layers");
    YTransaction* txn = ydoc_read_transaction(doc);
    QJsonObject out;
    for (size_t i = 0; i < std::size(maps); ++i)
        out.insert(QString::fromLatin1(maps[i]), mapToJson(rootMaps[i], txn, 0));
    out.insert(QStringLiteral("layers"), arrayToJson(layers, txn, 0));
    ytransaction_commit(txn);
    return out;
}

}  // namespace detail

std::optional<QJsonObject> docToJson(const QByteArray& update, QString* error) {
    SyncDoc doc;
    if (!doc.applyUpdate(update, error)) return std::nullopt;
    return doc.toJson();
}

}  // namespace bld::sync
