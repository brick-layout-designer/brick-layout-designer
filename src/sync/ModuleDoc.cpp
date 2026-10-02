#include "ModuleDoc.h"

#include "DocJson.h"
#include "SyncDoc.h"
#include "WebModel.h"

#include "core/LayerBrick.h"
#include "core/Map.h"

namespace bld::sync {

std::unique_ptr<core::Map> mapFromModuleSnapshot(const QByteArray& snapshot, QString* error) {
    if (snapshot.isEmpty()) {
        if (error) *error = QStringLiteral("empty module");
        return nullptr;
    }
    const auto json = docToJson(snapshot, error);
    if (!json) return nullptr;
    return mapFromDocJson(*json, error);
}

QByteArray moduleSnapshotFor(const core::Map& module, const QByteArray& current, QString* error) {
    SyncDoc doc;
    if (!current.isEmpty() && !doc.applyUpdate(current, error)) return {};
    doc.writeMap(module);
    return doc.encodeState();
}

int partCount(const core::Map& map) {
    int n = 0;
    for (const auto& layer : map.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            n += static_cast<int>(static_cast<const core::LayerBrick&>(*layer).bricks.size());
    return n;
}

}  // namespace bld::sync
