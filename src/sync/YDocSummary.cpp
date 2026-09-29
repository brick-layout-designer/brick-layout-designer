#include "YDocSummary.h"

extern "C" {
#include "libyrs.h"
}

#include <memory>

namespace bld::sync {

namespace {

struct DocDeleter { void operator()(YDoc* d) const { if (d) ydoc_destroy(d); } };
struct OutputDeleter { void operator()(YOutput* o) const { if (o) youtput_destroy(o); } };
using DocPtr = std::unique_ptr<YDoc, DocDeleter>;
using Output = std::unique_ptr<YOutput, OutputDeleter>;

QString str(const YOutput* o) {
    if (!o || o->tag != Y_JSON_STR) return {};
    return QString::fromUtf8(youtput_read_string(o));
}

// Yjs writes integral numbers as integers and others as doubles.
std::optional<double> number(const YOutput* o) {
    if (!o) return std::nullopt;
    if (o->tag == Y_JSON_NUM) return *youtput_read_float(o);
    if (o->tag == Y_JSON_INT) return static_cast<double>(*youtput_read_long(o));
    return std::nullopt;
}

Output get(const Branch* map, const YTransaction* txn, const char* key) {
    return Output(ymap_get(map, txn, key));
}

// A JSON object stored as one value in a Y.Map (e.g. displayArea).
std::optional<double> jsonField(const YOutput* o, const char* key) {
    if (!o || o->tag != Y_JSON_MAP) return std::nullopt;
    const YMapEntry* entries = youtput_read_json_map(o);
    for (uint32_t i = 0; i < o->len; ++i)
        if (qstrcmp(entries[i].key, key) == 0) return number(entries[i].value);
    return std::nullopt;
}

}  // namespace

std::optional<DocSummary> summarizeDoc(const QByteArray& update, QString* error) {
    DocPtr doc(ydoc_new());
    // Root types must be declared before they can be read.
    Branch* meta = ymap(doc.get(), "meta");
    Branch* layers = yarray(doc.get(), "layers");
    Branch* layerData = ymap(doc.get(), "layerData");

    YTransaction* write = ydoc_write_transaction(doc.get(), 0, nullptr);
    const uint8_t rc = ytransaction_apply(write, update.constData(), static_cast<uint32_t>(update.size()));
    ytransaction_commit(write);
    if (rc != 0) {
        if (error) *error = QStringLiteral("could not apply the document update (yrs error %1)").arg(rc);
        return std::nullopt;
    }

    DocSummary out;
    YTransaction* txn = ydoc_read_transaction(doc.get());
    if (const auto v = number(get(meta, txn, "schemaVersion").get())) out.schemaVersion = static_cast<int>(*v);

    const uint32_t n = yarray_len(layers);
    for (uint32_t i = 0; i < n; ++i) {
        Output idOut(yarray_get(layers, txn, i));
        LayerSummary layer;
        layer.id = str(idOut.get());
        const QByteArray id = layer.id.toUtf8();
        Output data = get(layerData, txn, id.constData());
        if (!data || data->tag != Y_MAP) continue;
        const Branch* ymapLayer = youtput_read_ymap(data.get());
        layer.type = str(get(ymapLayer, txn, "type").get());
        layer.name = str(get(ymapLayer, txn, "name").get());
        Output bricks = get(ymapLayer, txn, "bricks");
        if (bricks && bricks->tag == Y_ARRAY) {
            const Branch* arr = youtput_read_yarray(bricks.get());
            layer.brickCount = static_cast<int>(yarray_len(arr));
            if (layer.brickCount > 0) {
                Output first(yarray_get(arr, txn, 0));
                if (first && first->tag == Y_MAP) {
                    const Branch* b = youtput_read_ymap(first.get());
                    BrickSummary brick;
                    brick.id = str(get(b, txn, "id").get());
                    brick.partNumber = str(get(b, txn, "partNumber").get());
                    Output area = get(b, txn, "displayArea");
                    brick.displayArea = QRectF(jsonField(area.get(), "x").value_or(0), jsonField(area.get(), "y").value_or(0),
                                               jsonField(area.get(), "width").value_or(0), jsonField(area.get(), "height").value_or(0));
                    brick.orientation = number(get(b, txn, "orientation").get()).value_or(0);
                    layer.firstBrick = brick;
                }
            }
        }
        out.layers.push_back(std::move(layer));
    }
    ytransaction_commit(txn);
    return out;
}

}  // namespace bld::sync
